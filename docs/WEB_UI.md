# Chat UI (local)

Browser front-end that feels like a chat product: you type a prompt, Gemini drafts `schematic-ir.v1`, then `synth generate` binds / verifies / emits KiCad artifacts.

## 1. Put your API key

Cursor may refuse to open `.env`. Use either:

**Option A (recommended in Cursor):** open [`GEMINI_API_KEY.local`](../GEMINI_API_KEY.local) and paste the key alone on one line.

**Option B:** create `.env` in the repo root (outside Cursor if needed):

```env
GEMINI_API_KEY=your_key_here
```

Both files are gitignored. Do not put real keys in `.env.example`.

## 2. Start the UI

```powershell
cd d:\physics-synthesis-compiler
python web\server.py
```

Or:

```powershell
.\scripts\start_web.ps1
```

Open **http://127.0.0.1:8765/** in your browser.

## 3. Use it

Type a circuit prompt → **Generate**. Download links appear for:

- `design.kicad_sch`
- `design.net`
- `bom.csv`
- `design-snapshot.v1.json`
- `verification.v1.json`
- `prompt_schematic.json` (Gemini IR)

Runs are stored under `out_web/<run_id>/`.
