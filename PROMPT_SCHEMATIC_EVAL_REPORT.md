# Prompt → Schematic Evaluation Report

**Date:** 2026-09-22 05:29 UTC
**Binary:** `audit_build/synth.exe`
**DFM profile:** `fixtures/dfm/standard.json`
**Binding mode (seeds):** `SYNTH_ALLOW_IR_PARTS=1` (IR parts[] used; catalogue not forced)

## Executive summary

| Suite | Cases | Correct vs prompt | Notes |
|-------|------:|------------------:|-------|
| Seed IR designs | 7 | **7/7** | Deterministic fixtures with known intent |
| Offline prompt corpus 001–010 | 10 | **0/10** intent; 10/10 pipeline | Pipeline OK; **intent mismatch** (rails≠divider) |
| Live Gemini free-text | 5 | **0/5** | **API key rejected** by Google (400 invalid key) |
| Golden math/integration | 1 suite (g01–g25) | **PASS** | `golden_runner failures=0` |

### Verdict

- **Deterministic path works** for LED / RC / RL / divider when IR carries parts (or catalogue has matching MPNs).
- **Fail-closed works** for LDO IC, invalid roles, and NPN switch (no false “success” emit).
- **Offline Gate7 prompts** emit schematics and pass verify, but the text asks for current rails while mapped IR is always a **2-resistor divider** — not correct vs prompt wording.
- **Live Gemini** could not be graded: `GEMINI_API_KEY.local` is present but Google returns `API key not valid`.
- With catalogue DB set and without `SYNTH_ALLOW_IR_PARTS=1`, seed fixtures that rely on IR `parts[]` fail bind (catalogue-owned boundary).

## 1. Seed IR cases (prompt = design intent)

| ID | Prompt / intent | Exit | SCH | Verify | Types bound | Correct? | Detail |
|----|-----------------|-----:|:---:|:------:|-------------|:--------:|--------|
| `seed/led_series` | 5V LED with series resistor | 0 | Y | True | resistor,diode | **PASS** | LED current: 9.09 mA (expected 1.0-20.0 mA); Vf=2.00 V; series resistor power=27.27 mW; re |
| `seed/rc_low_pass` | RC low-pass filter | 0 | Y | True | resistor,capacitor | **PASS** | RC low-pass fc=159 Hz (R=1e+04 C=1e-07); result=PASS |
| `seed/rl_low_pass` | RL filter | 0 | Y | True | resistor,inductor | **PASS** | RL low-pass fc=1.59e+08 Hz (R=1e+04 L=1e-05); result=PASS |
| `seed/resistor_divider` | 2-resistor VIN-GND divider | 0 | Y | True | resistor,resistor | **PASS** | dc_sense=2.500000 corner=[2.475000,2.525000] ac_mag=0.0001 rating_violations=0 |
| `seed/ldo_3v3` | 3.3V LDO IC from 5V | 2 | N | False | — | **PASS** | unsupported: IC/regulator network (no DC bias model yet) |
| `seed/invalid_role` | invalid role negative test | 1 | N | None | — | **PASS** | OK |
| `seed/npn_switch_led` | NPN switch driving LED | 2 | N | False | — | **PASS** | unsupported: transistor network (no DC bias model yet) |

### Components emitted

- **seed/led_series** (PASS):
  - `R1:?=330 mpn=DEMO-330-0603-A`
  - `D1:?=2 mpn=DEMO-LED-RED-0603-A`
  - nets: `5V, LED_A, GND`
- **seed/rc_low_pass** (PASS):
  - `R1:?=10000 mpn=DEMO-10K-0603-A`
  - `C1:?=1e-07 mpn=DEMO-100N-0603-A`
  - nets: `VIN, VOUT, GND`
- **seed/rl_low_pass** (PASS):
  - `R1:?=10000 mpn=DEMO-10K-0603-A`
  - `L1:?=1e-05 mpn=DEMO-10U-0603-A`
  - nets: `VIN, VOUT, GND`
- **seed/resistor_divider** (PASS):
  - `r1:?=10000 mpn=DEMO-10K-0603-A`
  - `r2:?=10000 mpn=DEMO-10K-0603-A`
  - nets: `VIN, VOUT, GND`
- **seed/ldo_3v3**: no snapshot emit. OK
- **seed/invalid_role**: no snapshot emit. OK
- **seed/npn_switch_led**: no snapshot emit. Correct fail-closed: transistor path not supported for emit

## 2. Offline prompt corpus (`fixtures/prompts` → `fixtures/schematics`)

Each prompt looks like: *"design a board needing rails and X A"*.
Offline map loads `fixtures/schematics/NNN.json`, a **VIN–VOUT–GND 10k/10k divider**, not a power-rail / current-budget design.

| ID | Prompt | Pipeline (bind/verify/SCH) | Correct vs prompt wording? |
|----|--------|:--------------------------:|:--------------------------:|
| `offline/001` | Prompt 001: design a board needing rails and 0.05A | PASS | **FAIL** |
| `offline/002` | Prompt 002: design a board needing rails and 0.1A | PASS | **FAIL** |
| `offline/003` | Prompt 003: design a board needing rails and 0.15A | PASS | **FAIL** |
| `offline/004` | Prompt 004: design a board needing rails and 0.2A | PASS | **FAIL** |
| `offline/005` | Prompt 005: design a board needing rails and 0.25A | PASS | **FAIL** |
| `offline/006` | Prompt 006: design a board needing rails and 0.3A | PASS | **FAIL** |
| `offline/007` | Prompt 007: design a board needing rails and 0.35A | PASS | **FAIL** |
| `offline/008` | Prompt 008: design a board needing rails and 0.4A | PASS | **FAIL** |
| `offline/009` | Prompt 009: design a board needing rails and 0.45A | PASS | **FAIL** |
| `offline/010` | Prompt 010: design a board needing rails and 0.5A | PASS | **FAIL** |

**Interpretation:** Offline generate proves the compiler pipeline; it does **not** prove NLP understanding of those prompts.

## 3. Live Gemini free-text prompts

| ID | Prompt | Exit | Result |
|----|--------|-----:|--------|
| `live/led` | 5V red LED with a 330 ohm series resistor to GND | 1 | Gemini HTTP 400: API key not valid |
| `live/rc` | RC low-pass filter using 10k resistor and 100nF capacitor | 1 | Gemini HTTP 400: API key not valid |
| `live/divider` | Two 10k resistors forming a voltage divider from VIN to GND with mid node VOUT | 1 | Gemini HTTP 400: API key not valid |
| `live/rl` | RL series filter 10k resistor and 10uH inductor | 1 | Gemini HTTP 400: API key not valid |
| `live/diode` | 5V supply with 1k series resistor into a silicon diode to ground (not an LED) | 1 | Gemini HTTP 400: API key not valid |

**Blocked on:** invalid Gemini API key. Put a valid key in `GEMINI_API_KEY.local` / env, then re-run live cases.

## 4. Automated golden suite

- Result: **PASS** (`golden_runner` exit 0)
- Covers stamps, divider OP, DFM suite, generate corpus, LED/RC/RL/LDO fail-closed, diode Newton, singular/conflict/BE (g23–g25).

## 5. Catalogue-boundary finding

When `SYNTH_CATALOGUE_DB=user_data/catalogue.db` was set (~5 DEMO parts) **without** `SYNTH_ALLOW_IR_PARTS=1`:

- `led_series`, `rc_low_pass`, `rl_low_pass`, `ldo_3v3` → **compile/bind FAIL** (IR parts[] skipped; catalogue missing values).
- `resistor_divider` → **PASS** (10k parts exist in catalogue).

Intended for LLM generate (catalogue owns MPNs). For fixture eval, either allow IR parts or expand the catalogue.

## 6. Recommendations (status)

1. **Done:** Offline prompts 001–010 text aligned with divider IR (10k/10k for 001–005, 4.7k/4.7k for 006–010).
2. **Blocked locally:** `GEMINI_API_KEY.local` is present but is **not** a valid Google AI Studio key (does not start with `AIza`; API returns 400). Replace with a real key from https://aistudio.google.com/apikey (see `GEMINI_API_KEY.local.example`), then re-run the five live prompts. A valid key cannot be invented in-repo.
3. **Done:** `python scripts/seed_catalogue.py` seeds DEMO LED/C/L/… into `user_data/catalogue.db` (12 MPNs). `web.server.ensure_user_data` calls the same script. Catalogue-owned LED/RC generate verified PASS without `SYNTH_ALLOW_IR_PARTS`.
4. **Done:** `SYNTH_ALLOW_IR_PARTS=1` set in CI (`build-test`, `asan`, `kicad-erc`), `scripts/run_kicad_erc.sh`, and `scripts/run_prompt.ps1`.

## Artifacts

- Raw JSON: `out_prompt_eval/results_rerun.json`, `out_prompt_eval/results.json`
- Per-case outputs: `out_prompt_eval/seed2_*`, `out_prompt_eval/offline2_*`, `out_prompt_eval/live_*`
