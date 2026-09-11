# Physics Synthesis Compiler

C11 engineering synthesis / Physics2 runtime. External surface: CLI + JSON fixtures.

## Build

Requires CMake ≥ 3.16 and a C11 compiler (GCC/Clang/MSVC).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

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
./build/synth generate --compose-gate4 -o out/
```

## Layout

- `cli/` — command implementations
- `seed/` — JSON topology seed loader
- `emit/` — netlist, BOM, design-snapshot emitters
- `spec/` — Spec IR load/validate + LLM provider boundary
- `compose/` — block composition (DFM)
- `bind/` — scored part binding
- `verify/` — bound-netlist verification report
- `fixtures/seed/` — seed data files
- `fixtures/specs/`, `fixtures/prompts/` — Gate 3 corpus
- `fixtures/blocks/` — Gate 4 block catalog
- `tests/golden/` — golden-file expected outputs
- `third_party/` — sqlite3 and cJSON amalgams

## Gates

- [Gate 1](docs/GATE1.md) — foundation
- [Gate 2](docs/GATE2.md) — KiCad emit / `generate`
- [Gate 3](docs/GATE3.md) — Spec IR offline
- [Gate 4](docs/GATE4.md) — block composition
- [Gate 5](docs/GATE5.md) — scored binder
- [Gate 6](docs/GATE6.md) — verification gate
