# Physics Synthesis Compiler

C11 engineering synthesis / Physics2 runtime + teammate engineering core
([electronics_vendor_v2_next](https://github.com/Abheesht04/electronics_vendor_v2_next)).
External surface: CLI + JSON fixtures + chat UI.

**Phase 0 audit (2026-09-21):** see [AUDIT_README.md](AUDIT_README.md) and [AUDIT_REPORT.md](AUDIT_REPORT.md).

**Phase 1:** Physics2 Shockley diode Newton + g22; IC fail-closed; Gemini `parts: []`; async `POST /api/generate` + `GET /api/jobs/{id}`; audit depth docs (`REFERENCE_ORACLE`, `TEST_MATRIX`, `OPTIMIZATION`, `AC_DAE_SPARSE`); SAFE DELETE of tracked binaries/`out_*`.

## Architecture

```
prompt / design JSON
  → schematic-ir (Gemini or fixtures)
  → bind + Physics2 verify
  → vendor Design IR + DFM (electronics_core)
  → KiCad / BOM / netlist emit
```

Vendor sources live in `vendor_next/` (`electronics_core` library): vec, intern,
range, constraint, component, net, design, models, diagnostic, dfm, part_provider,
mna, e_series, kicad_generic_provider. Bridge: `vendor_bridge.c`.

Block-composition DFM (port compatibility) is `dfm_compose.*` (renamed from
`DFM.*` so it does not collide with vendor `dfm.h` on Windows).

## Build

Requires CMake ≥ 3.16 and a C11 compiler (GCC/Clang/MSVC).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` runs golden_runner plus vendor unit tests (`dfm_test`, `mna_test`, …).

Sanitizers (GCC/Clang):

```bash
cmake -S . -B build-asan -DSYNTH_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

CSV import always takes the CSV path as a CLI argument — never a hardcoded machine path:

```bash
./build/synth db import jlcpcb-components-basic-preferred.csv board.db
./build/synth db seed fixtures/seed/resistor_divider.json board.db
./build/synth generate fixtures/seed/resistor_divider.json -o out/
./build/synth generate --spec fixtures/specs/001.json -o out/
./build/synth generate --prompt fixtures/prompts/001.txt -o out/
./build/synth generate --prompt-text "10k resistor divider" -o out/
./build/synth generate --compose-gate4 -o out/
./build/synth generate fixtures/seed/resistor_divider.json -o out/ --dfm-profile fixtures/dfm/standard.json --catalogue user_data/catalogue.db
export SYNTH_BIN=$PWD/build/synth
./scripts/run_kicad_erc.sh out_erc   # requires kicad-cli
```

Live Gemini prompt→schematic: set `GEMINI_API_KEY` (see [docs/GEMINI_PROMPT.md](docs/GEMINI_PROMPT.md)).

## Chat UI (browser)

```bash
python web/server.py
# open http://127.0.0.1:8765/
```

Put `GEMINI_API_KEY` in a repo-root `.env` file. See [docs/WEB_UI.md](docs/WEB_UI.md).
Library panel: upload JLCPCB parts CSV + DFM JSON; defaults use project DEMO parts
and vendor `standard` manufacturing profile.

## Ponytail (Cursor agent mode)

Lazy-senior coding rules for Cursor in this repo (`ponytail/` checkout + project hooks).

Already installed for this workspace:

- `.cursor/hooks.json` — injects Ponytail on `sessionStart` and handles level switches
- `.cursor/skills/` — `ponytail`, `ponytail-review`, `ponytail-audit`, `ponytail-debt`, `ponytail-gain`, `ponytail-help`

**Start a new Cursor chat** after clone/move so hooks reload. Then:

- Type `/ponytail` as a plain message to see the active level
- `/ponytail lite` · `/ponytail full` (default) · `/ponytail ultra` · `/ponytail off`
- Or say `stop ponytail` / `normal mode`

Re-install after moving the checkout:

```bash
node ponytail/scripts/cursor-hooks.js install --project
```

## Layout

- `cli/` — command implementations
- `seed/` — JSON topology seed loader
- `emit/` — netlist, BOM, design-snapshot emitters
- `spec/` — Spec IR + schematic-ir load/validate + LLM/prompt provider boundary
- `compose/` — block composition (DFM) + expand-to-schematic
- `bind/` — scored part binding
- `verify/` — bound-netlist verification report
- `scripts/run_kicad_erc.sh` — real KiCad ERC for divider + compose-gate4
- `fixtures/seed/` — seed data files
- `fixtures/schematics/` — Gate 7 schematic IR corpus
- `fixtures/specs/`, `fixtures/prompts/` — Gate 3 corpus (prompts also drive Gate 7 IR)
- `fixtures/blocks/` — Gate 4 block catalog (with expand recipes)
- `tests/golden/` — golden-file expected outputs
- `third_party/` — sqlite3 and cJSON amalgams

## Gates

- [Gate 1](docs/GATE1.md) — foundation
- [Gate 2](docs/GATE2.md) — KiCad emit / `generate` (+ ERC proof)
- [Gate 3](docs/GATE3.md) — Spec IR offline
- [Gate 4](docs/GATE4.md) — block composition (+ expand emit)
- [Gate 5](docs/GATE5.md) — scored binder
- [Gate 6](docs/GATE6.md) — verification gate
- [Gate 7](docs/GATE7.md) — free-form schematic IR (grammar-constrained)
