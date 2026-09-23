# NLP frontend — actual behavior (Phases 19–20)

## Commands

```text
synth parse --prompt-text "Put a 10k resistor between 3V3 and ADC_SENSE" -o ir.json
synth parse --prompt prompt.txt -o ir.json
synth parse --prompt-text "..." --neural   # optional ONNX; fail-closed if absent
synth generate --prompt-text "..." -o out/          # always the offline NLP (Gemini removed)
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

No CMake option for ONNX; ORT is not vendored.

## Not NLP

`--offline-prompt` + `fixtures/prompts/NNN.txt` → `fixtures/schematics/NNN.json` corpus map only. Not the normal NLP engine.

## Not implemented

- Real ORT session + exported BiLSTM weights
- Full entity/relation graph for every engineering phrase
- Thermistor / sensor front-ends, rail-only requests ("3.3 V rail capable of 200 mA"),
  multi-stage filters: refused with a clarifying question.

## Value synthesis (deterministic, `nlp/nlp.c`)

| Pattern | Synthesis | Provenance written |
|---------|-----------|--------------------|
| Divider Vin → ~Vout | R2 = 10k, R1 = E24(R2·(Vin/Vout − 1)); measure ±5 % | r_bottom defaulted, r_top inferred |
| RC low-pass at f | R = 10k (or stated), C = E24(1/(2πRf)) | c inferred |
| LED indicator | R = E24((Vrail − 2.0)/I), I = 2 mA unless stated | led_vf, led_current defaulted |
| N-MOS low-side switch | load R = E24(Vload/I), I = 100 mA unless stated; rated 2×P; package by ampacity | load_current defaulted |
| LDO Vin → Vout | behavioral LDO + 10 µF; measure ±2 % | output_cap defaulted |
| I2C pull-ups | R on SDA and SCL to the rail | signals inferred |
| Battery reverse polarity | 3.7 V unless stated, Schottky series diode, 10 µF | battery_voltage defaulted |
| Decoupling | every stated capacitor rail → GND; stated load current → `unmodeled` | — |

Any design whose nets carry no supply (no rail name, voltage, or battery) is refused.

## Measured coverage

See [REPAIR_PLAN.md §5](REPAIR_PLAN.md): §62 prompts (g83) and the 30-prompt corpus (g77,
14 verify end to end, 16 refused, 0 emitted-but-invalid).
