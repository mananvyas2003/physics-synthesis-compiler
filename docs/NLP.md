# NLP frontend — actual behavior (Phases 19–20)

## Commands

```text
synth parse --prompt-text "Put a 10k resistor between 3V3 and ADC_SENSE" -o ir.json
synth parse --prompt prompt.txt -o ir.json
synth parse --prompt-text "..." --neural   # optional ONNX; fail-closed if absent
synth generate --prompt-text "..." -o out/          # offline NLP if no API key
synth generate --prompt fixtures/prompts/001.txt --offline-prompt -o out/  # corpus map
```

## Deterministic C NLP (`nlp/nlp.c`)

| Stage | Behavior |
|-------|----------|
| Lexer | SI quantities with type (`10k`→R, `100nF`→C, `3.3V`→V, `0603`→package) |
| Ambiguity | Bare `10m` → `NLP_Q_AMBIGUOUS` (not silent milli) |
| Rails | `3V3` / `5V0` kept as node tokens |
| IR patterns | R/C between A/B; divider; bypass/decouple C; RC LPF; pull-up; from/to |
| Fail-closed | Vague prompts → clarifying question; no invented topology |

Provenance: package defaults to `0603` when omitted (defaulted). Prefix-only `10k` resistance is inferred ohm.

Free-form corpus: `fixtures/nlp_freeform/p01.txt`…`p30.txt` (g77).

## Optional neural backend (`nlp/nlp_runtime.h`) — Phase 20

| Item | Status |
|------|--------|
| Header + stub adapter | yes |
| Default C11 build links ORT | **no** |
| Trained BiLSTM/BiGRU ONNX model | **not shipped** |
| `nlp_runtime_available()` | always `0` until ORT+model wired |
| `--neural` | fail-closed clarifying question |
| Physics / MPN / DFM / PCB | never owned by neural path |

CMake: `SYNTH_ENABLE_ONNX` (off by default) — currently only a compile define; ORT not vendored.

## Not NLP

`--offline-prompt` + `fixtures/prompts/NNN.txt` → `fixtures/schematics/NNN.json` corpus map only. Not the normal NLP engine.

## Not implemented

- Real ORT session + exported BiLSTM weights
- Full entity/relation graph for every engineering phrase
- Automatic recording of live Gemini → cassette (manual copy of `*.resp.json` for now)
