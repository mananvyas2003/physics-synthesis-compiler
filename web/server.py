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
else:
    RUNS = ROOT / "out_web"

HOST = os.environ.get("SYNTH_WEB_HOST", "127.0.0.1")
PORT = int(os.environ.get("SYNTH_WEB_PORT", "8765"))


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


def run_generate(prompt: str) -> dict:
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

    has_key = bool(env.get("GEMINI_API_KEY") or env.get("SYNTH_LLM_API_KEY"))
    cmd = [str(synth), "generate", "--prompt-text", prompt, "-o", str(out_dir)]
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
        return {
            "ok": False,
            "run_id": run_id,
            "error": f"generate timed out after {timeout_sec}s",
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
        "exit_code": proc.returncode,
        "stdout": proc.stdout[-4000:],
        "stderr": proc.stderr[-4000:],
        "artifacts": artifacts,
        "artifact_contents": artifact_contents,
        "verification_summary": summary,
        "error": None
        if ok
        else (proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}"),
    }


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

    def do_GET(self) -> None:  # noqa: N802
        clean_path = self.path.split("?")[0]
        if clean_path.startswith("/api/runs/") or clean_path.startswith("/runs/"):
            self._serve_run_file()
            return
        if clean_path in ("/api/health", "/health"):
            has_key = bool(os.environ.get("GEMINI_API_KEY") or os.environ.get("SYNTH_LLM_API_KEY"))
            try:
                synth_path = str(find_synth())
            except Exception:
                synth_path = None
            self._json(200, {"status": "ok", "has_key": has_key, "synth": synth_path})
            return
        if self.path in ("/", "/index.html"):
            self.path = "/index.html"
        return SimpleHTTPRequestHandler.do_GET(self)

    def _serve_run_file(self) -> None:
        parts = [p for p in unquote(self.path).split("?")[0].split("/") if p]
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
        clean_path = self.path.split("?")[0].rstrip("/")
        if clean_path not in ("/api/chat", "/chat"):
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
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

    has_key = bool(os.environ.get("GEMINI_API_KEY") or os.environ.get("SYNTH_LLM_API_KEY"))
    try:
        synth = find_synth()
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)

    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    url = f"http://{HOST}:{PORT}/"
    print(f"Physics Synthesis chat UI")
    print(f"  open:  {url}")
    print(f"  synth: {synth}")
    print(f"  gemini key: {'yes' if has_key else 'NO — add to .env'}")
    print(f"  runs:  {RUNS}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
