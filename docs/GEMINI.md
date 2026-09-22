# Live Gemini + replay (Phase 21)

## Modes (separate)

| Mode | Trigger | Network | API key | CI |
|------|---------|---------|---------|-----|
| **GEMINI_REPLAY** | `SYNTH_GEMINI_REPLAY=<cassette_dir>` | no | no | yes (g78) |
| **GEMINI_LIVE** | `GEMINI_API_KEY` set, no replay env | yes (curl) | yes | opt-in only |
| Offline NLP / corpus | no key, no replay | no | no | yes |

Ordinary goldens never call live Gemini. If live env is missing, g79 reports `status=unavailable` (not a silent live pass).

## Replay cassette

Directory (example shipped):

```text
fixtures/gemini_replay/divider_10k/
  prompt.txt              # original prompt (documentation)
  http_response.json      # recorded generateContent-shaped body
```

```powershell
$env:SYNTH_GEMINI_REPLAY = "fixtures/gemini_replay/divider_10k"
synth generate --prompt-text "anything" -o out/   # uses cassette, not curl
Remove-Item Env:SYNTH_GEMINI_REPLAY
```

Replay runs the same JSON extract + `schematic-ir.v1` validate path as live.

## Live (manual)

```powershell
$env:GEMINI_API_KEY = "AIza..."
# optional: $env:SYNTH_GEMINI_LIVE = "1"   # marks live_ready in g79
synth generate --prompt-text "2-resistor 10k divider VIN to GND" -o out/
```

See also [GEMINI_PROMPT.md](GEMINI_PROMPT.md) for keys/models.

## Rules

- Do not use Gemini to repair numerical / Physics2 failures.
- LLM output stops at Spec IR; compiler owns values, equations, DFM, PCB.
- Default C11 build does not require Gemini or Python.
