# Module Map — Full Dossiers (Phase 0+ completion)

Field legend for every first-party module:

- **FILE / RESPONSIBILITY / INPUTS / OUTPUTS / PUBLIC API / DEPENDENCIES / DEPENDENTS**
- **STATE OWNERSHIP / MATHEMATICAL ROLE / COMPILER ROLE / RUNTIME ROLE**
- **TEST COVERAGE / DUPLICATE / DEAD CODE / SEMANTIC LEAKS / RECOMMENDATION**
- **CLASS** A–X (see ARCHITECTURE_AUDIT)

Third-party amalgams (`sqlite3`, `cJSON`) summarized once.

---

## A — CLI

### `main.c`
| Field | Value |
|-------|-------|
| RESPONSIBILITY | Dispatch argv to CLI commands |
| INPUTS | argc/argv |
| OUTPUTS | process exit code |
| PUBLIC API | `main` |
| DEPENDENCIES | `cli.h` |
| DEPENDENTS | none |
| STATE | none |
| MATH / COMPILER / RUNTIME | none |
| TESTS | indirect via golden generate |
| DUP / DEAD / LEAK | none |
| REC | keep thin |
| CLASS | A |

### `cli/cli.h`, `cli/cli_common.c`
| Field | Value |
|-------|-------|
| RESPONSIBILITY | Command decls; path/fixture helpers |
| INPUTS | paths, env |
| OUTPUTS | joined paths, fixture root |
| PUBLIC API | `cmd_*`, `cli_join_path`, `cli_fixture_root` |
| DEPENDENCIES | stdio/stdlib |
| DEPENDENTS | all cmd_*, compose, golden |
| STATE | none (fixture root from env/cwd) |
| LEAK | compose depends on CLI for paths |
| REC | extract fixture helper to X later |
| CLASS | A/X |

### `cli/cmd_generate.c`
| Field | Value |
|-------|-------|
| RESPONSIBILITY | Full generate orchestration |
| INPUTS | design JSON / prompt / spec / compose flags |
| OUTPUTS | work DB, sch, net, bom, snapshot, verify, dfm, manifest |
| PUBLIC API | `cmd_generate` |
| DEPENDENCIES | compiler, seed, verify, mfg_dfm, emit, spec, compose |
| STATE | owns work DB lifetime |
| LEAK | Gate5 cost special-case name |
| CLASS | A |

### `cli/cmd_compile.c` / `cmd_db.c` / `cmd_dfm.c` / `cmd_physics2.c`
| File | RESP | CLASS | REC |
|------|------|-------|-----|
| cmd_compile | Legacy divider→sch | A/T | deprecate |
| cmd_db | import/seed/inspect | A/P | keep |
| cmd_dfm | block DFM self-test | A/R/W | keep |
| cmd_physics2 | stub → ctest | A | wire demos or drop |

---

## P/Q — DB, catalogue, bind, part_lib

### `db.c` / `db.h`
| Field | Value |
|-------|-------|
| RESPONSIBILITY | SQLite Parts, FabRules, Topology* |
| INPUTS | SQL CRUD |
| OUTPUTS | `DBPart`, topology rows |
| STATE | owns `sqlite3*` in `DB` |
| MATH | none |
| LEAK | physics+procurement+ratings one row; NULL→0 |
| TESTS | generate goldens |
| CLASS | P |

### `catalogue.c` / `jlcparts_import.c` / `part_lib.c`
| File | RESP | DEAD/DUP | CLASS |
|------|------|----------|-------|
| catalogue | E-series generators | GenerateE* mostly unused; DUP e_series | P |
| jlcparts_import | CSV→Parts | polymorphic value | P |
| part_lib | type→pins/KiCad | enum kicad_id collapse | O/Q |

### `bind/bind_scorer.c` / `compiler.c` / `seed/seed_topology.c`
| File | RESP | LEAK | CLASS |
|------|------|------|-------|
| bind_scorer | closest part + derating | fake MPN cost; DUP part_provider | Q |
| compiler | bind + KiCad emit + unused Physics2 lower | KiCad in Q; IR bind hints | Q+T |
| seed_topology | JSON→DB | may insert fixture parts[] | E/P |

### `unit_parse.c` / `diag_error.c`
| CLASS | RESP |
|-------|------|
| X/D-lite | SI parse |
| X | last-error buffer |

---

## B/C — Spec / LLM

| File | RESP | CLASS | LEAK |
|------|------|-------|------|
| schematic_load | IR validate; prompt route | C/B | power-net checks |
| gemini_schematic | curl→IR | B | was MPN invent; now `parts:[]` |
| spec_load | SpecV1 | C | — |
| llm_provider | offline prompt map | C | misleading name |

---

## E/R — Compose / DFM

| File | RESP | CLASS |
|------|------|-------|
| compose/* | Gate4 expand | E |
| dfm_compose | port V/I/Z | R |
| mfg_dfm + vendor_bridge | mfg DFM adapter | R |
| vendor dfm | 3 rules | R |

---

## S/T — Verify / emit

| File | RESP | MATH | CLASS |
|------|------|------|-------|
| verify_report | verification.v1 | LED/RC/RL analytical; Physics2 DC; IC fail-closed | S |
| emit_* | net/bom/snapshot | none | T |

---

## G/H/I/J/K/M — Physics2

| File | RESP | MATH | CLASS |
|------|------|------|-------|
| physics2_isa | opcodes, pools | ID spaces | H |
| physics2_interpreter | stamps, GE, BE C/L, **Newton diode** | linear MNA + Shockley companion | I/J/K/L/M |
| physics2_types/symbols/typecheck/print | expr tooling | metadata | G |

**Diode ABI (Phase 1):** stamp supplies Gd/Ieq; `physics2_context_step` owns Newton when any diode present. Test: `g22_diode_newton`.

---

## Vendor `electronics_core`

| File | RESP | MATH | CLASS | STATUS |
|------|------|------|-------|--------|
| mna.c/h | DC MNA + Newton diode | Shockley oracle | F/J/K/L | KEEP oracle |
| dfm | 3 rules | height | R | live via bridge |
| design/component/net | Design IR | — | E/O | DFM sidecar |
| component_model/registry | catalog models | not equations | O | |
| part_provider / kicad_generic | part search | unused generate | Q | FUTURE |
| e_series | snap | unused generate | X | |
| vec/intern/range/constraint/diagnostic | infra | — | X | |
| PartIdentity.h | dup | **SAFE DELETE** | — | remove |

---

## W — Tests

| File | MATH FACT |
|------|-----------|
| g01–g03 | G stamp / divider voltages |
| g22 | Physics2 diode OP ≈ 0.574 V |
| mna_test | vendor diode OP |
| g04–g21 | integration / fail-closed / emit |
| vendor unit | structure |

---

## V/X — Web / infra

| Path | CLASS | NOTE |
|------|-------|------|
| web/server.py | V | sync chat + **async jobs** (`/api/jobs`) |
| public / web/static | V | prefer public; sync in build.sh |
| CMakeLists / build.sh | X | libm on electronics_core |
| third_party | X | amalgams |

**U PCB / N AC:** no modules yet — see `docs/math/AC_DAE_SPARSE.md`.

---

## Dependency graph

```
main → synth_core → electronics_core PUBLIC m (non-MSVC)
                 → sqlite3, cJSON
golden_runner → synth_core
vendor_*_test → electronics_core
```

No hard include cycles. Soft: compose→cli.
