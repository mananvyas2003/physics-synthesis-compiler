# Chat UI (local)

Browser front-end that feels like a chat product: you type a prompt, the offline C NLP frontend (`synth generate --prompt-text`) drafts `schematic-ir.v1`, then the compiler binds / verifies / emits KiCad artifacts. No API key or network access. Prompts the NLP cannot parse come back as a clarifying question (see [NLP.md](NLP.md) for supported patterns).

## 1. Start the UI

```powershell
cd d:\physics-synthesis-compiler
python web\server.py
```

Or:

```powershell
.\scripts\start_web.ps1
```

Open **http://127.0.0.1:8765/** in your browser.

## 2. Use it

Type a circuit prompt → **Generate**. Download links appear for:

- `design.kicad_sch`
- `design.net`
- `bom.csv`
- `design-snapshot.v1.json`
- `verification.v1.json`
- `mfg-dfm.v1.json` (manufacturing DFM report)
- `prompt_schematic.json` (NLP-generated IR)

## Library uploads (parts + DFM)

The left **Library** panel uses the **project DEMO parts** by default (seeded from `fixtures/seed` + `fixtures/schematics` into `user_data/catalogue.db`). No separate test database.

You can also upload:

| Upload | Endpoint | Format |
|--------|----------|--------|
| Parts CSV | `POST /api/upload/parts` | JLCPCB-style CSV (same as `synth db import`) |
| DFM profile | `POST /api/upload/dfm` | JSON matching `fixtures/dfm/standard.json` |

Default DFM profile is **standard** from [electronics_vendor_v2_next `src/dfm.c`](https://github.com/Abheesht04/electronics_vendor_v2_next/blob/main/src/dfm.c) (also `fixtures/dfm/wearable.json`). Checks: floating pins, missing footprints, component height, profile consistency (via/drill/annular/trace/clearance), and package body/pad vs min clearance/trace.

Generate merges the shared catalogue and applies the active DFM profile automatically. Catalogue DEMO parts (LED/C/L/…) are seeded via `python scripts/seed_catalogue.py` (also on web start).

Chat uses **async jobs**: `POST /api/generate` → `job_id`, then poll `GET /api/jobs/{id}` until `done`/`error`. Sync `POST /api/chat` remains for scripts.

Runs are stored under `out_web/<run_id>/`.
