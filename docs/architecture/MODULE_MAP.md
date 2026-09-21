# Module Map — Phase 0

Per-file map for first-party sources (synth + vendor_next + tests).  
Third-party amalgams (`sqlite3`, `cJSON`) summarized only.  
Classes: A CLI, B NLP/LLM, C Spec, D Eng language, E Topology, F Physical model, G Physics IR, H Physics ISA, I Runtime, J Assembly, K Linear solver, L Nonlinear, M Dynamic, N AC, O Device lib, P DB, Q Binding, R DFM, S Verify, T KiCad, U PCB, V Web, W Tests, X Infra.

Legend fields: RESP / IN / OUT / API / DEPS / DEPENDENTS / STATE / MATH / COMPILER / RUNTIME / TESTS / DUP / DEAD / LEAK / REC

---

## Entry / CLI

### `main.c` — A
- **RESP:** argv dispatch to CLI commands  
- **IN:** argc/argv  
- **OUT:** exit code  
- **API:** `main`  
- **DEPS:** `cli.h`  
- **DEPENDENTS:** none  
- **STATE:** none  
- **MATH/COMPILER/RUNTIME:** none  
- **TESTS:** indirect via golden subprocess  
- **DUP/DEAD/LEAK:** none  
- **REC:** keep thin  

### `cli/cli.h` — A
- **RESP:** command declarations + shared helpers  
- **API:** `cmd_*`, `cli_*` helpers  
- **DEPS:** std headers  
- **REC:** keep  

### `cli/cli_common.c` — A/X
- **RESP:** path join, fixture root, file helpers  
- **DEPENDENTS:** compose, golden, cmd_*  
- **LEAK:** compose depends on CLI for paths  
- **REC:** move fixture root to X helper later  

### `cli/cmd_generate.c` — A
- **RESP:** full generate orchestration  
- **IN:** design/prompt/spec/compose flags  
- **OUT:** work DB + sch/net/bom/snapshot/verify/dfm/manifest  
- **DEPS:** compiler, seed, verify, mfg_dfm, emit, spec, compose  
- **LEAK:** Gate5 cost special-case `resistor_divider`  
- **REC:** keep as orchestrator  

### `cli/cmd_compile.c` — A/T
- **RESP:** legacy divider compile → `.kicad_sch`  
- **DEAD-ish:** superseded by generate  
- **REC:** deprecate after review  

### `cli/cmd_db.c` — A/P
- **RESP:** import/seed/inspect SQLite  
- **DEPS:** db, jlcparts_import, catalogue, seed  

### `cli/cmd_dfm.c` — A/R/W
- **RESP:** block-compose DFM self-checks  
- **DEPS:** `dfm_compose`  

### `cli/cmd_physics2.c` — A
- **RESP:** stub pointing at ctest  
- **REC:** wire real Physics2 demos or remove later  

---

## Database / catalogue / import

### `db.c` / `db.h` — P
- **RESP:** SQLite Parts, FabRules, Topology*  
- **IN:** SQL ops  
- **OUT:** `DBPart`, topology rows  
- **STATE:** owns `sqlite3*`  
- **LEAK:** physics + procurement + ratings in one `Parts` row; NULL→0  
- **TESTS:** via generate goldens  
- **REC:** split schema long-term  

### `catalogue.c` / `catalogue.h` — P
- **RESP:** E-series part generators + SeedFabRules  
- **DEAD:** GenerateE24/E96/E6* unused callers  
- **DUP:** vs `vendor_next/e_series.c`  
- **REC:** wire or LIKELY DELETE generators  

### `jlcparts_import.c` / `.h` — P
- **RESP:** JLCPCB CSV → Parts  
- **LEAK:** polymorphic `value` (Vr for diodes)  
- **TESTS:** manual/db import  

### `part_lib.c` / `.h` — O/Q
- **RESP:** static type registry pins/defaults/KiCad ids  
- **LEAK:** `part_lib_kicad_id` enum collapse diode→LED  
- **DEPENDENTS:** compiler, seed, schematic_load  

---

## Bind / compile / seed

### `bind/bind_scorer.c` / `.h` — Q
- **RESP:** scored closest part + derating + fake cost  
- **IN:** target value/package/ratings floors  
- **OUT:** `DBPart` choice + rationale  
- **DUP:** vs vendor `part_provider` (unused on generate)  
- **LEAK:** MPN substring cost  

### `compiler.c` / `compiler.h` — Q+T (+ dead G)
- **RESP:** topology→`CompiledSchematic`; KiCad sch emit; Physics2 lower API  
- **STATE:** allocates components  
- **DEAD:** `compiler_lower_to_physics2` unused by generate  
- **LEAK:** KiCad in compiler; defaults 10k/0603; bind hints from IR  
- **REC:** split emit; wire or drop lowering  

### `seed/seed_topology.c` / `.h` — E/P
- **RESP:** JSON IR → Parts + Topology tables  
- **LEAK:** inserts LLM `parts[]` as catalogue rows  
- **REC:** stop seeding MPNs from IR  

### `unit_parse.c` / `.h` — X/D-lite
- **RESP:** engineering unit strings → double  

### `diag_error.c` / `.h` — X
- **RESP:** process-global last error string  
- **STATE:** single buffer (CLI single-thread)  

---

## Spec / LLM

### `spec/schematic_load.c` / `.h` — C/B
- **RESP:** load/validate schematic-ir; route prompt→Gemini|offline  
- **OUT:** validated IR path  

### `spec/gemini_schematic.c` / `.h` — B
- **RESP:** curl Gemini → IR JSON  
- **LEAK:** recipes + DEMO MPNs in system prompt  
- **RUNTIME:** not linked into solvers  

### `spec/spec_load.c` / `.h` — C
- **RESP:** SpecV1 validate → fixture design path  

### `spec/llm_provider.c` / `.h` — C
- **RESP:** offline prompt→spec file map  
- **DUP name:** not an LLM  
- **REC:** rename  

---

## Compose / DFM host

### `compose/compose.c` / `.h` — E
- **RESP:** Gate4 block list, port auto-connect, expand to IR  
- **HARDCODE:** fixed five-block scenario  
- **LEAK:** depends on cli fixture root  

### `dfm_compose.c` / `.h` — R
- **RESP:** port kind + V/I/Z compatibility (was `DFM.*`)  
- **DUP name history:** vs vendor `dfm.h`  
- **TESTS:** g04, cmd_dfm  

### `mfg_dfm.c` / `.h` — R
- **RESP:** profiles + call vendor DFM  
- **DUP:** profile constants vs fixtures vs vendor defaults  

### `vendor_bridge.c` / `.h` — R/Q
- **RESP:** `CompiledSchematic` → vendor `Design` → `dfm_run_all`  
- **LEAK:** forces resistor-ish models; invents 50 V / 5 V ratings  

---

## Verify / emit

### `verify/verify_report.c` / `.h` — S
- **RESP:** bound schematic → `verification.v1.json`  
- **MATH:** LED/RC/RL analytical; else Physics2 DC; IC/xstr structural pass  
- **LEAK:** auto-pass IC/transistor  
- **TESTS:** g12, g17–g21  

### `emit/emit.h` — T
- **RESP:** emit API declarations  

### `emit/emit_bom.c` — T
- **OUT:** `bom.csv`  

### `emit/emit_netlist.c` — T
- **OUT:** legacy `.net` (Device/R-centric)  

### `emit/emit_snapshot.c` — T/S-adj
- **OUT:** `design-snapshot.v1.json`  

---

## Physics2

### `physics2_isa.c` / `.h` — H
- **RESP:** opcodes, instruction struct, param/state pools  
- **DEAD runtime:** pools unused by stamps  
- **REC:** stamps should read pools  

### `physics2_interpreter.c` / `.h` — I/J/K/M
- **RESP:** accumulator, stamps, dense GE, BE C/L, step  
- **MATH:** linear MNA; diode/BJT/MOS/logic return false  
- **STATE:** context solution + device history arrays  
- **TESTS:** g01–g03, g12  

### `physics2_types.c` / `.h` — G
- **RESP:** quantity/expr sketches  
- **RUNTIME:** not live generate MNA path  

### `physics2_symbols.c` / `.h` — G
- **RESP:** symbol table for expr IR  

### `physics2_typecheck.c` / `.h` — G
- **RESP:** typecheck helpers  

### `physics2_print.c` / `.h` — G
- **RESP:** debug print  

---

## Vendor next (`electronics_core`)

### `vendor_next/src/mna.c` / `mna.h` — F/J/K/L
- **RESP:** dense DC MNA; R/V/diode; Newton  
- **MATH:** Shockley + companion; GE partial pivot  
- **TESTS:** `mna_test` PASS  
- **DEAD:** `matrix_node_entry` unused  
- **REC:** KEEP; candidate live verify backend  

### `vendor_next/src/dfm.c` / `dfm.h` — R
- **RESP:** floating_pin, missing_footprint, component_height  
- **MATH:** height only numerical  
- **TESTS:** `dfm_test`  

### `vendor_next/src/design.c` / `design.h` — E
- **RESP:** Design container components/nets  

### `vendor_next/src/component.c` / `component.h` — O/E
- **RESP:** component instances, pins, footprints  

### `vendor_next/src/net.c` / `net.h` — E
- **RESP:** net objects  

### `vendor_next/src/component_model.c` / `.h` — O
- **RESP:** catalog electrical/physical model structs  
- **MATH:** **not** device equations  

### `vendor_next/src/model_registry.c` / `.h` — O
- **RESP:** register/lookup models  

### `vendor_next/src/part_provider.c` / `.h` — Q
- **RESP:** part search interface  
- **DEAD on generate:** unused  

### `vendor_next/src/kicad_generic_provider.c` — Q/T-adj
- **RESP:** generic KiCad-oriented provider  
- **REC:** KEEP FOR FUTURE  

### `vendor_next/src/e_series.c` / `.h` — X
- **RESP:** E-series snap  
- **DEAD on generate:** linked unused  

### `vendor_next/src/vec.c` / `.h` — X
- **RESP:** dynamic array  

### `vendor_next/src/intern.c` / `.h` — X
- **RESP:** string interning  

### `vendor_next/src/range.c` / `.h` — X
- **RESP:** numeric ranges  

### `vendor_next/src/constraint.c` / `.h` — X/D-adj
- **RESP:** constraint helpers  

### `vendor_next/src/diagnostic.c` / `.h` — X
- **RESP:** diagnostic messages  

### `vendor_next/src/PartIdentity.h` — —
- **RESP:** duplicate PartRequest  
- **DEAD:** never included  
- **REC:** SAFE DELETE  

---

## Tests

### `tests/golden_runner.c` / `golden_cases.c` / `.h` — W
- **RESP:** g01–g21 cases  
- **MATH FACT:** strong only g01–g03 (+ mna_test separately)  
- **WEAK:** g18/g20 no τ; g21 structural  

### `vendor_next/tests/*_test.c` — W
- **RESP:** vec, intern, design, models, dfm, provider, mna  
- **RESULT (re-verified):** all PASS  

---

## Third party / web (summary)

| Path | Class | Note |
|------|-------|------|
| `third_party/sqlite3/*` | X | amalgamation; relaxed warnings |
| `third_party/cJSON/*` | X | JSON |
| `web/server.py`, `api/index.py` | V | sync generate; 50s/300s timeout |
| `public/*`, `web/static/*` | V | drifted UI copies |
| `audit_build/probe.c` | X | ASan probe leftover; ignore |

**U PCB:** no first-party PCB modules.  
**N AC:** no modules.

---

## Dependency graph (libraries)

```
main → synth_core → electronics_core
                 → sqlite3, cJSON
golden_runner → synth_core
vendor_*_test → electronics_core
```

**Cycles:** none hard. Soft: compose→cli; bridge→compiler.h→physics2 headers.

---

## Re-verification

| Date | Suite | Result |
|------|-------|--------|
| 2026-09-21 (initial) | golden + vendor | PASS |
| 2026-09-21 (re-check) | golden + vendor | PASS (`failures=0`) |
