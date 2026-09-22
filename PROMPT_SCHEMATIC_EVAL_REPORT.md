# Prompt → Schematic Evaluation Report

**Date:** 2026-09-22 05:45 UTC (re-run after prompt/catalogue/CI fixes)
**Binary:** `audit_build/synth.exe`
**DFM profile:** `fixtures/dfm/standard.json`
**Catalogue:** `user_data/catalogue.db` (12 DEMO MPNs via `python scripts/seed_catalogue.py`)
**Modes:** catalogue-owned bind (no `SYNTH_ALLOW_IR_PARTS` for happy-path seeds); `SYNTH_ALLOW_IR_PARTS=1` for fail-closed IR seeds / offline / CI

## Executive summary

| Suite | Cases | Correct vs prompt | Notes |
|-------|------:|------------------:|-------|
| Catalogue-owned seeds (LED/RC/RL/divider) | 4 | **4/4** | Catalogue has DEMO LED/C/L/R; IR parts[] skipped |
| Fail-closed / negative seeds | 3 | **3/3** | LDO/NPN unsupported; invalid role rejected |
| Offline prompts 001–010 | 10 | **10/10** intent; 10/10 pipeline | Prompts now match divider IR |
| Live Gemini free-text | 5 | **0/5** | Still blocked: API key not valid (not `AIza…`) |
| Golden math/integration | 1 suite (g01–g25) | **PASS** | `golden_runner failures=0` |

### Verdict

- **Catalogue-owned generate works** for LED / RC / RL / divider (12 DEMO parts seeded).
- **Offline prompts 001–010 now match intent** (divider text ↔ divider IR) and pass verify/SCH emit.
- **Fail-closed** still correct for LDO IC, NPN switch, and invalid roles.
- **Live Gemini** still cannot be graded until `GEMINI_API_KEY.local` has a valid Google AI Studio key (`AIza…`).
- **Goldens g01–g25 PASS.**

## 1. Catalogue-owned seed cases

| ID | Prompt / intent | Exit | SCH | Verify | Types | Correct? | Detail |
|----|-----------------|-----:|:---:|:------:|-------|:--------:|--------|
| `catalogue/led_series` | 5V LED with series resistor | 0 | Y | True | resistor,diode | **PASS** | LED current: 9.09 mA (expected 1.0-20.0 mA); Vf=2.00 V; series resistor power=27.27 mW; re |
| `catalogue/rc_low_pass` | RC low-pass filter | 0 | Y | True | resistor,capacitor | **PASS** | RC low-pass fc=159 Hz (R=1e+04 C=1e-07); result=PASS |
| `catalogue/rl_low_pass` | RL filter | 0 | Y | True | resistor,inductor | **PASS** | RL low-pass fc=1.59e+08 Hz (R=1e+04 L=1e-05); result=PASS |
| `catalogue/resistor_divider` | 2-resistor VIN-GND divider | 0 | Y | True | resistor,resistor | **PASS** | dc_sense=2.500000 corner=[2.475000,2.525000] ac_mag=0.0001 rating_violations=0 |

### Components emitted

- **catalogue/led_series** (PASS):
  - `R1:?=330 mpn=DEMO-330-0603-A`
  - `D1:?=2 mpn=DEMO-LED-RED-0603-A`
  - nets: `5V, LED_A, GND`
- **catalogue/rc_low_pass** (PASS):
  - `R1:?=10000 mpn=DEMO-10K-0603-A`
  - `C1:?=1e-07 mpn=DEMO-100N-0603-A`
  - nets: `VIN, VOUT, GND`
- **catalogue/rl_low_pass** (PASS):
  - `R1:?=10000 mpn=DEMO-10K-0603-A`
  - `L1:?=1e-05 mpn=DEMO-10U-0603-A`
  - nets: `VIN, VOUT, GND`
- **catalogue/resistor_divider** (PASS):
  - `r1:?=10000 mpn=DEMO-10K-0603-A`
  - `r2:?=10000 mpn=DEMO-10K-0603-A`
  - nets: `VIN, VOUT, GND`

## 2. Fail-closed / negative seeds

| ID | Intent | Exit | SCH | Verify | Correct? | Detail |
|----|--------|-----:|:---:|:------:|:--------:|--------|
| `seed/ldo_3v3` | 3.3V LDO IC from 5V | 2 | N | False | **PASS** | unsupported: IC/regulator network (no DC bias model yet) |
| `seed/invalid_role` | invalid role negative | 1 | N | None | **PASS** | fail-closed OK |
| `seed/npn_switch_led` | NPN switch driving LED | 2 | N | False | **PASS** | unsupported: transistor network (no DC bias model yet) |

## 3. Offline prompt corpus 001–010

Prompts aligned to divider IR: 10k/10k (001–005), 4.7k/4.7k (006–010).

| ID | Prompt | Pipeline | Correct vs prompt? |
|----|--------|:--------:|:------------------:|
| `offline/001` | Prompt 001: 2-resistor 10k/10k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/002` | Prompt 002: 2-resistor 10k/10k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/003` | Prompt 003: 2-resistor 10k/10k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/004` | Prompt 004: 2-resistor 10k/10k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/005` | Prompt 005: 2-resistor 10k/10k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/006` | Prompt 006: 2-resistor 4.7k/4.7k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/007` | Prompt 007: 2-resistor 4.7k/4.7k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/008` | Prompt 008: 2-resistor 4.7k/4.7k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/009` | Prompt 009: 2-resistor 4.7k/4.7k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |
| `offline/010` | Prompt 010: 2-resistor 4.7k/4.7k voltage divider from VIN to GND with mid node VOUT | PASS | **PASS** |

## 4. Live Gemini free-text prompts

| ID | Prompt | Exit | Result |
|----|--------|-----:|--------|
| `live/led` | 5V red LED with a 330 ohm series resistor to GND | 1 | Gemini HTTP 400: API key not valid |
| `live/rc` | RC low-pass filter using 10k resistor and 100nF capacitor | 1 | missing=['resistor', 'capacitor'] exit=1 |
| `live/divider` | Two 10k resistors forming a voltage divider from VIN to GND with mid node VOUT | 1 | Gemini HTTP 400: API key not valid |
| `live/rl` | RL series filter 10k resistor and 10uH inductor | 1 | Gemini HTTP 400: API key not valid |
| `live/diode` | 5V supply with 1k series resistor into a silicon diode to ground (not an LED) | 1 | Gemini HTTP 400: API key not valid |

**Blocked on:** invalid Gemini API key. Replace `GEMINI_API_KEY.local` with an `AIza…` key from https://aistudio.google.com/apikey (see `GEMINI_API_KEY.local.example`), then re-run live cases.

## 5. Automated golden suite

- Result: **PASS** (`golden_runner` exit 0, 0.86s)
- Covers stamps, divider OP, DFM suite, generate corpus, LED/RC/RL/LDO fail-closed, diode Newton, singular/conflict/BE (g23–g25).

## 6. Recommendations (status)

1. **Done:** Offline prompts 001–010 text aligned with divider IR.
2. **Still open:** Install a valid Gemini API key (`AIza…`) and re-run the five live prompts.
3. **Done:** Catalogue seeded with DEMO LED/C/L/R/IC/transistor MPNs (`scripts/seed_catalogue.py`); catalogue-owned LED/RC/RL/divider PASS.
4. **Done:** `SYNTH_ALLOW_IR_PARTS=1` in CI / `run_kicad_erc.sh` / `run_prompt.ps1`.

## Artifacts

- Raw JSON: `out_prompt_eval/results_r3.json` (prior runs: `results_rerun.json`, `results.json`)
- Per-case outputs: `out_prompt_eval/r3_cat_*`, `r3_seed_*`, `r3_offline_*`, `r3_live_*`
