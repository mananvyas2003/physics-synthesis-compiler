#!/usr/bin/env python3
"""
Local chat UI backend for physics-synthesis-compiler.
Loads GEMINI_API_KEY from environment or repo-root .env, runs synth generate.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import threading
import time
import uuid
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
if (ROOT / "public").is_dir():
    STATIC = ROOT / "public"
else:
    STATIC = Path(__file__).resolve().parent / "static"

if os.environ.get("VERCEL") or os.environ.get("AWS_LAMBDA_FUNCTION_NAME"):
    RUNS = Path("/tmp/out_web")
    USER_DATA = Path("/tmp/user_data")
else:
    RUNS = ROOT / "out_web"
    USER_DATA = ROOT / "user_data"

CATALOGUE_DB = USER_DATA / "catalogue.db"
DFM_PROFILE = USER_DATA / "dfm_profile.json"
DEFAULT_DFM = ROOT / "fixtures" / "dfm" / "standard.json"

HOST = os.environ.get("SYNTH_WEB_HOST", "127.0.0.1")
PORT = int(os.environ.get("SYNTH_WEB_PORT", "8765"))

# Async generate jobs: web request must not be the lifetime of synth.
_JOBS: dict[str, dict] = {}
_JOBS_LOCK = threading.Lock()


def load_dotenv(path: Path) -> None:
    if not path.is_file():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        name, val = line.split("=", 1)
        name = name.strip()
        val = val.strip().strip('"').strip("'")
        if name and val and name not in os.environ:
            os.environ[name] = val


def load_gemini_key_local(path: Path) -> None:
    """Cursor often blocks opening .env; allow a plain local key file."""
    if not path.is_file():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line:
            name, val = line.split("=", 1)
            name = name.strip()
            val = val.strip().strip('"').strip("'")
            if name and val and name not in os.environ:
                os.environ[name] = val
        elif not os.environ.get("GEMINI_API_KEY") and not os.environ.get("SYNTH_LLM_API_KEY"):
            os.environ["GEMINI_API_KEY"] = line


def find_synth() -> Path:
    # If /tmp/synth already exists and is executable, prefer it
    tmp_synth = Path("/tmp/synth")
    if tmp_synth.is_file() and (os.name == "nt" or os.access(tmp_synth, os.X_OK)):
        return tmp_synth

    candidates = [
        ROOT / "synth",
        ROOT / "api" / "synth",
        Path(__file__).resolve().parent / "synth",
        Path(__file__).resolve().parents[1] / "synth",
        ROOT / "synth.exe",
        tmp_synth,
        ROOT / "build" / "synth",
        ROOT / "build" / "synth.exe",
        ROOT / "build" / "Release" / "synth.exe",
        ROOT / "build" / "Debug" / "synth.exe",
    ]
    for c in candidates:
        if c.is_file():
            if os.name == "nt":
                return c
            if os.access(c, os.X_OK):
                return c
            # Try to chmod in place
            try:
                os.chmod(c, 0o755)
                if os.access(c, os.X_OK):
                    return c
            except OSError:
                pass
            # If chmod in place failed (e.g. read-only filesystem in Lambda), copy to /tmp/synth
            try:
                import shutil
                shutil.copy2(c, tmp_synth)
                os.chmod(tmp_synth, 0o755)
                if os.access(tmp_synth, os.X_OK):
                    return tmp_synth
            except Exception:
                pass
            return c
    raise FileNotFoundError(
        "synth binary not found; build synth first (e.g. run build.sh or build with cmake/gcc)"
    )


def ensure_user_data() -> None:
    """Seed/merge shared catalogue from project DEMO fixture parts; default DFM."""
    # Keep logic in scripts/seed_catalogue.py (single place).
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "seed_catalogue", ROOT / "scripts" / "seed_catalogue.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("scripts/seed_catalogue.py missing")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if mod.seed() != 0:
        raise RuntimeError("catalogue seed failed")


def catalogue_status() -> dict:
    ensure_user_data()
    parts = 0
    if CATALOGUE_DB.is_file():
        try:
            import sqlite3

            con = sqlite3.connect(str(CATALOGUE_DB))
            row = con.execute("SELECT COUNT(*) FROM Parts").fetchone()
            parts = int(row[0]) if row else 0
            con.close()
        except Exception:
            parts = -1
    profile_name = "standard"
    if DFM_PROFILE.is_file():
        try:
            profile_name = json.loads(DFM_PROFILE.read_text(encoding="utf-8")).get(
                "name", "standard"
            )
        except json.JSONDecodeError:
            pass
    return {
        "parts": parts,
        "catalogue": str(CATALOGUE_DB),
        "dfm_profile": str(DFM_PROFILE if DFM_PROFILE.is_file() else DEFAULT_DFM),
        "dfm_name": profile_name,
        "source": "project DEMO fixtures (+ optional user CSV uploads)",
    }


def import_parts_csv(csv_bytes: bytes) -> dict:
    ensure_user_data()
    synth = find_synth()
    USER_DATA.mkdir(parents=True, exist_ok=True)
    text = csv_bytes.decode("utf-8", errors="replace")
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if not lines:
        return {"ok": False, "error": "CSV is empty"}
    header = [h.strip().lower() for h in lines[0].split(",")]
    required = {"mpn", "type", "value", "package"}
    missing = required - set(header)
    if missing:
        return {
            "ok": False,
            "error": f"CSV missing required columns: {', '.join(sorted(missing))}",
            "preview": [],
        }
    preview = []
    for ln in lines[1:6]:
        cols = [c.strip() for c in ln.split(",")]
        row = {header[i]: cols[i] if i < len(cols) else "" for i in range(len(header))}
        preview.append(row)
    # Duplicate MPN detection in upload
    mpns = []
    for ln in lines[1:]:
        cols = [c.strip() for c in ln.split(",")]
        if cols:
            mpns.append(cols[0])
    dupes = sorted({m for m in mpns if mpns.count(m) > 1})
    csv_path = USER_DATA / "upload_parts.csv"
    csv_path.write_bytes(csv_bytes)
    # Merge into existing catalogue: import into temp db then merge via DB_MergePartsFrom
    # synth db import replaces db — so import to temp then merge with Python/sqlite OR
    # use: import to temp, then C merge. Easiest: if no catalogue, import directly;
    # else import to temp and copy rows.
    import sqlite3
    import shutil

    temp_db = USER_DATA / "_upload_parts.db"
    if temp_db.exists():
        temp_db.unlink()
    proc = subprocess.run(
        [str(synth), "db", "import", str(csv_path), str(temp_db)],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        timeout=120,
    )
    if proc.returncode != 0:
        return {
            "ok": False,
            "error": proc.stderr.strip() or proc.stdout.strip() or "import failed",
        }
    if not CATALOGUE_DB.is_file():
        shutil.copy2(temp_db, CATALOGUE_DB)
    else:
        src = sqlite3.connect(str(temp_db))
        dst = sqlite3.connect(str(CATALOGUE_DB))
        n = 0
        for row in src.execute(
            "SELECT mpn, type, value, package, v_rating, i_rating, "
            "esr_ohms, power_rating_w, tolerance_class FROM Parts"
        ):
            try:
                dst.execute(
                    "INSERT OR IGNORE INTO Parts "
                    "(mpn, type, value, package, v_rating, i_rating, "
                    "esr_ohms, power_rating_w, tolerance_class) "
                    "VALUES (?,?,?,?,?,?,?,?,?)",
                    row,
                )
                n += dst.total_changes
            except sqlite3.Error:
                pass
        dst.commit()
        dst.close()
        src.close()
    status = catalogue_status()
    status["ok"] = True
    status["imported"] = True
    status["preview"] = preview
    if dupes:
        status["duplicate_mpns_in_upload"] = dupes
    return status


def save_dfm_profile(raw: bytes) -> dict:
    ensure_user_data()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        return {"ok": False, "error": f"invalid DFM JSON: {exc}"}
    if not isinstance(data, dict):
        return {"ok": False, "error": "DFM profile must be a JSON object"}
    # Fill defaults from standard profile fields
    base = json.loads(DEFAULT_DFM.read_text(encoding="utf-8")) if DEFAULT_DFM.is_file() else {}
    base.update(data)
    DFM_PROFILE.write_text(json.dumps(base, indent=2) + "\n", encoding="utf-8")
    return {"ok": True, **catalogue_status()}


def _gemini_key() -> str:
    return (os.environ.get("GEMINI_API_KEY") or os.environ.get("SYNTH_LLM_API_KEY") or "").strip()


def _gemini_key_looks_valid(key: str | None = None) -> bool:
    """Google AI Studio keys are typically AIza… (length varies; require prefix)."""
    k = (key if key is not None else _gemini_key()).strip()
    return k.startswith("AIza") and len(k) >= 20


def run_generate(prompt: str) -> dict:
    ensure_user_data()
    synth = find_synth()
    run_id = time.strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:8]
    out_dir = RUNS / run_id
    try:
        out_dir.mkdir(parents=True, exist_ok=True)
    except OSError:
        out_dir = Path("/tmp/out_web") / run_id
        out_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["SYNTH_FIXTURE_ROOT"] = str(ROOT)

    key = _gemini_key()
    has_key = bool(key)
    cmd = [str(synth), "generate", "--prompt-text", prompt, "-o", str(out_dir)]
    if CATALOGUE_DB.is_file():
        cmd.extend(["--catalogue", str(CATALOGUE_DB)])
    dfm = DFM_PROFILE if DFM_PROFILE.is_file() else DEFAULT_DFM
    if dfm.is_file():
        cmd.extend(["--dfm-profile", str(dfm)])
    if not has_key:
        return {
            "ok": False,
            "run_id": run_id,
            "error": (
                "GEMINI_API_KEY is not set. In Vercel, set GEMINI_API_KEY in "
                "Project Settings → Environment Variables. Locally, put it in GEMINI_API_KEY.local."
            ),
            "live": False,
        }
    if not _gemini_key_looks_valid(key):
        return {
            "ok": False,
            "run_id": run_id,
            "live": False,
            "error": (
                "GEMINI_API_KEY does not look like a Google AI Studio key "
                "(expected to start with AIza). Get a key at "
                "https://aistudio.google.com/apikey and put it in "
                "GEMINI_API_KEY / GEMINI_API_KEY.local (Vercel: Project -> "
                "Environment Variables). A wrong key used to burn the 50s "
                "Vercel generate timeout instead of failing fast."
            ),
        }

    timeout_sec = 50 if os.environ.get("VERCEL") else 300
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(out_dir),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout_sec,
        )
    except subprocess.TimeoutExpired:
        hint = ""
        if os.environ.get("VERCEL"):
            hint = (
                " On Vercel the generate subprocess limit is 50s. "
                "Check GEMINI_API_KEY is a valid AIza… key and that Gemini "
                "is reachable; invalid keys / long retries used to hit this."
            )
        return {
            "ok": False,
            "run_id": run_id,
            "error": f"generate timed out after {timeout_sec}s.{hint}",
            "live": True,
        }
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "run_id": run_id, "error": str(exc), "live": True}

    artifacts = {}
    artifact_contents = {}
    names = [
        "design.kicad_sch",
        "design.net",
        "bom.csv",
        "design-snapshot.v1.json",
        "verification.v1.json",
        "mfg-dfm.v1.json",
        "prompt_schematic.json",
        "composed_schematic.json",
    ]
    for name in names:
        p = out_dir / name
        if p.is_file():
            artifacts[name] = f"/api/runs/{run_id}/{name}"
            try:
                if p.stat().st_size <= 1_500_000:
                    artifact_contents[name] = p.read_text(encoding="utf-8", errors="replace")
            except Exception:
                pass

    ok = proc.returncode == 0 and "design.kicad_sch" in artifacts
    summary = ""
    ver = out_dir / "verification.v1.json"
    if ver.is_file():
        try:
            summary = json.loads(ver.read_text(encoding="utf-8")).get("summary", "")
        except json.JSONDecodeError:
            pass

    return {
        "ok": ok,
        "run_id": run_id,
        "live": True,
        "summary": summary,
        "verification_summary": summary,
        "artifacts": artifacts,
        "artifact_contents": artifact_contents,
        "catalogue": catalogue_status(),
        "stdout": (proc.stdout or "")[-4000:],
        "stderr": (proc.stderr or "")[-4000:],
        "error": None if ok else ((proc.stderr or proc.stdout or "generate failed")[-2000:]),
    }


def _job_set(job_id: str, **fields: object) -> None:
    with _JOBS_LOCK:
        job = _JOBS.setdefault(job_id, {})
        job.update(fields)


def start_generate_job(prompt: str) -> dict:
    """Enqueue generate on a worker thread; return job_id immediately."""
    job_id = uuid.uuid4().hex

    def worker() -> None:
        _job_set(job_id, status="running", started_at=time.time())
        try:
            result = run_generate(prompt)
            _job_set(
                job_id,
                status="done" if result.get("ok") else "error",
                finished_at=time.time(),
                result=result,
            )
        except Exception as exc:  # noqa: BLE001
            _job_set(
                job_id,
                status="error",
                finished_at=time.time(),
                result={"ok": False, "error": str(exc)},
            )

    _job_set(job_id, status="queued", prompt=prompt[:200], created_at=time.time())
    threading.Thread(target=worker, daemon=True).start()
    return {"ok": True, "job_id": job_id, "status": "queued"}


def get_generate_job(job_id: str) -> dict | None:
    with _JOBS_LOCK:
        job = _JOBS.get(job_id)
        if not job:
            return None
        return dict(job)


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        static_dir = str(STATIC) if STATIC.is_dir() else str(ROOT)
        super().__init__(*args, directory=static_dir, **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[web] " + (fmt % args) + "\n")

    def _json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _resolve_path(self) -> str:
        # 1. Check reverse proxy / Vercel rewrite headers
        for hdr in ("x-forwarded-url", "x-real-url", "x-matched-path"):
            val = self.headers.get(hdr)
            if val:
                return val.split("?")[0].rstrip("/")
        # 2. Check path or __path query parameter from rewrites
        if "?" in self.path:
            query = self.path.split("?", 1)[1]
            for part in query.split("&"):
                if part.startswith("path=") or part.startswith("__path="):
                    val = unquote(part.split("=", 1)[1])
                    if not val.startswith("/"):
                        val = "/" + val
                    if not val.startswith("/api/"):
                        val = "/api" + val
                    return val.rstrip("/")
        return self.path.split("?")[0].rstrip("/")

    def do_GET(self) -> None:  # noqa: N802
        resolved = self._resolve_path()
        if resolved.startswith("/api/runs/") or resolved.startswith("/runs/"):
            self._serve_run_file(resolved)
            return
        if resolved in ("/api/health", "/health") or (
            resolved in ("/api/index.py", "/api") and "health" in self.path
        ):
            has_key = bool(_gemini_key())
            try:
                synth_path = str(find_synth())
            except Exception:
                synth_path = None
            self._json(
                200,
                {
                    "status": "ok",
                    "has_key": has_key,
                    "key_looks_valid": _gemini_key_looks_valid() if has_key else False,
                    "synth": synth_path,
                    "catalogue": catalogue_status(),
                },
            )
            return
        if resolved in ("/api/catalogue", "/catalogue"):
            self._json(200, catalogue_status())
            return
        if resolved.startswith("/api/jobs/"):
            job_id = resolved.split("/api/jobs/", 1)[-1].strip("/")
            if not re.fullmatch(r"[A-Fa-f0-9]{16,64}", job_id):
                self._json(400, {"ok": False, "error": "invalid job_id"})
                return
            job = get_generate_job(job_id)
            if not job:
                self._json(404, {"ok": False, "error": "job not found"})
                return
            self._json(200, {"ok": True, "job_id": job_id, **job})
            return
        if self.path in ("/", "/index.html"):
            self.path = "/index.html"
        return SimpleHTTPRequestHandler.do_GET(self)

    def _serve_run_file(self, target_path: str | None = None) -> None:
        target = target_path or self._resolve_path()
        parts = [p for p in unquote(target).split("?")[0].split("/") if p]
        # Matches ["api", "runs", run_id, filename] or ["runs", run_id, filename]
        if len(parts) >= 4 and parts[0] == "api" and parts[1] == "runs":
            run_id = parts[2]
            name = parts[3]
        elif len(parts) >= 3 and parts[0] == "runs":
            run_id = parts[1]
            name = parts[2]
        else:
            self.send_error(404)
            return

        if not re.fullmatch(r"[A-Za-z0-9_\-]+", run_id) or ".." in name or "/" in name or "\\" in name:
            self.send_error(400)
            return

        path = RUNS / run_id / name
        if not path.is_file():
            fallback = Path("/tmp/out_web") / run_id / name
            if fallback.is_file():
                path = fallback
            else:
                self.send_error(404)
                return

        data = path.read_bytes()
        ctype = "application/octet-stream"
        if name.endswith(".json"):
            ctype = "application/json"
        elif name.endswith(".csv"):
            ctype = "text/csv"
        elif name.endswith(".kicad_sch") or name.endswith(".net"):
            ctype = "text/plain; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Disposition", f'inline; filename="{name}"')
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self) -> None:  # noqa: N802
        resolved = self._resolve_path()
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b""

        if resolved in ("/api/upload/parts", "/upload/parts"):
            ctype = self.headers.get("Content-Type", "")
            if "multipart/form-data" in ctype:
                # Minimal multipart: find filename= and following blank line + body
                # Prefer raw body if client sends application/octet-stream / text/csv
                self._json(400, {"ok": False, "error": "send CSV as raw body (text/csv)"})
                return
            if not raw:
                self._json(400, {"ok": False, "error": "empty CSV body"})
                return
            result = import_parts_csv(raw)
            self._json(200 if result.get("ok") else 400, result)
            return

        if resolved in ("/api/upload/dfm", "/upload/dfm"):
            if not raw:
                self._json(400, {"ok": False, "error": "empty DFM JSON body"})
                return
            result = save_dfm_profile(raw)
            self._json(200 if result.get("ok") else 400, result)
            return

        if resolved in ("/api/generate", "/generate"):
            if not raw:
                raw = b"{}"
            try:
                payload = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError:
                self._json(400, {"ok": False, "error": "invalid JSON"})
                return
            prompt = (payload.get("prompt") or "").strip()
            if not prompt:
                self._json(400, {"ok": False, "error": "prompt required"})
                return
            if len(prompt) > 8000:
                self._json(400, {"ok": False, "error": "prompt too long"})
                return
            self._json(202, start_generate_job(prompt))
            return

        if resolved not in ("/api/chat", "/chat", "/api/index.py", "/api/index", "/api"):
            self.send_error(404)
            return
        if not raw:
            raw = b"{}"
        try:
            payload = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            self._json(400, {"ok": False, "error": "invalid JSON"})
            return
        prompt = (payload.get("prompt") or "").strip()
        if not prompt:
            self._json(400, {"ok": False, "error": "prompt required"})
            return
        if len(prompt) > 8000:
            self._json(400, {"ok": False, "error": "prompt too long"})
            return
        result = run_generate(prompt)
        self._json(200 if result.get("ok") else 502, result)


def main() -> None:
    load_dotenv(ROOT / ".env")
    load_gemini_key_local(ROOT / "GEMINI_API_KEY.local")
    RUNS.mkdir(parents=True, exist_ok=True)
    STATIC.mkdir(parents=True, exist_ok=True)
    try:
        ensure_user_data()
    except Exception as exc:  # noqa: BLE001
        print(f"catalogue seed warning: {exc}", file=sys.stderr)

    has_key = bool(os.environ.get("GEMINI_API_KEY") or os.environ.get("SYNTH_LLM_API_KEY"))
    try:
        synth = find_synth()
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)

    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    url = f"http://{HOST}:{PORT}/"
    cat = catalogue_status()
    print(f"Physics Synthesis chat UI")
    print(f"  open:  {url}")
    print(f"  synth: {synth}")
    print(f"  gemini key: {'yes' if has_key else 'NO — add to .env'}")
    print(f"  catalogue parts: {cat.get('parts')} ({cat.get('dfm_name')} DFM)")
    print(f"  runs:  {RUNS}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
