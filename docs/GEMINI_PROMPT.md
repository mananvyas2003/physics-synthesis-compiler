# Live Gemini prompt → schematic

## Where to put your API key

Set an **environment variable** (do not put the key in source or chat):

| Variable | Required | Purpose |
|----------|----------|---------|
| `GEMINI_API_KEY` | yes for live mode | Google AI Studio / Gemini API key |
| `SYNTH_LLM_API_KEY` | alt | Same as above if you prefer this name |
| `SYNTH_GEMINI_MODEL` | no | Default `gemini-3.8-flash` |
| `SYNTH_GEMINI_FALLBACK_MODEL` | no | Fallback sequence: `gemini-3.7-flash` -> `gemini-3.1-flash-lite` -> `gemini-3.5-flash` |

### Windows PowerShell (current session)

```powershell
$env:GEMINI_API_KEY = "YOUR_KEY_HERE"
# optional:
# $env:SYNTH_GEMINI_MODEL = "gemini-3.8-flash"
```

### Windows persistent user env

Settings → System → About → Advanced system settings → Environment Variables → User → New → `GEMINI_API_KEY`.

### Linux / macOS

```bash
export GEMINI_API_KEY=YOUR_KEY_HERE
```

A template file [`.env.example`](../.env.example) exists for reference; the binary reads **process environment**, not `.env` automatically unless you export those vars yourself.

## Usage

Requires `curl` / `curl.exe` on PATH.

```text
# Live (uses Gemini when GEMINI_API_KEY is set)
synth generate --prompt-text "2-resistor 10k divider VIN to GND" -o out/

# Or from a file
synth generate --prompt my_prompt.txt -o out/

# Force offline corpus mapping (CI / no network)
synth generate --prompt fixtures/prompts/001.txt --offline-prompt -o out/
```

Live mode writes `out/prompt_schematic.json`, validates `schematic-ir.v1` (up to 3 retries with feedback), then runs bind → verify → KiCad emit.

## Notes

- Without `GEMINI_API_KEY`, prompt text uses **deterministic offline NLP** (Phase 19), not fixture filenames.
- Fixture map: `--offline-prompt` + `fixtures/prompts/NNN.txt` only.
- **Replay (CI):** `SYNTH_GEMINI_REPLAY=fixtures/gemini_replay/divider_10k` — see [GEMINI.md](GEMINI.md).
- Keys from [Google AI Studio](https://aistudio.google.com/apikey) usually start with `AIza`. Put the key in the environment or in gitignored `GEMINI_API_KEY.local` (one line).
- Seed DEMO catalogue parts used by fixtures: `python scripts/seed_catalogue.py`
- For seed/fixture generates that embed `parts[]` while a catalogue is active: `SYNTH_ALLOW_IR_PARTS=1` (set in CI / `scripts/run_kicad_erc.sh`).
- Compiler remains resistor-focused; ask the model for small passive networks for best results.
