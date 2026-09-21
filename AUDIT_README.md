# AUDIT_README.md — Phase 0 Features, Modules, Tests, Bugs

This document justifies the Phase 0 audit: what the repository is, what was inspected, what was built/tested, what is correct vs broken, and what was **not** changed.

**Companion:** [AUDIT_REPORT.md](AUDIT_REPORT.md) (executive A–U).  
**Phase 0 rule:** no source refactors, no deletions, no Phase 1 implementation.  
**Audit build artifacts:** `audit_build/` (local binaries and logs).

---

## 1. What Phase 0 did

| Action | Result |
|--------|--------|
| Read implementations across synth + vendor_next + web + schemas | Module/math/semantic map |
| Discover toolchain | MinGW GCC 12.1.0; **no CMake/clang/MSVC** on PATH |
| Strict C11 compile | Success; warnings logged, not “fixed” |
| Run golden_runner + 8 vendor tests | **All PASS** (`failures=0`) |
| Probe ASan/UBSan | **NOT AVAILABLE** (no libasan/libubsan) |
| Write audit docs | Listed below |
| Modify product C/Python for features | **None** |
| Delete/rename code | **None** |

### Documents produced

| Path | Purpose |
|------|---------|
| [AUDIT_REPORT.md](AUDIT_REPORT.md) | Executive A–U + Phase 1 order |
| [AUDIT_README.md](AUDIT_README.md) | This file |
| [docs/math/MNA_AUDIT.md](docs/math/MNA_AUDIT.md) | Equations ↔ code |
| [docs/architecture/ARCHITECTURE_AUDIT.md](docs/architecture/ARCHITECTURE_AUDIT.md) | Layers, leaks, boundaries |
| [docs/architecture/CLEANUP_REPORT.md](docs/architecture/CLEANUP_REPORT.md) | Delete candidates (no deletes) |
| [docs/build/BUILD_AUDIT.md](docs/build/BUILD_AUDIT.md) | Compiler, warnings, tests |

---

## 2. Product features (as implemented)

### 2.1 CLI (`synth`)

| Command | Feature |
|---------|---------|
| `db import` | JLCPCB CSV → SQLite Parts |
| `db seed` | Topology JSON → DB |
| `generate` | Full pipeline: IR → bind → verify → DFM → emit |
| `generate --prompt` / `--prompt-text` | Gemini or offline fixtures → IR |
| `generate --spec` | SpecV1 → fixture design |
| `generate --compose-gate4` | Block composition expand |
| `compile` | Legacy divider → `.kicad_sch` |
| `dfm` | Block-compose DFM unit checks |
| `physics2` | Stub pointing at ctest |

### 2.2 Deterministic engineering core

- Schematic-IR validate (`schemas/schematic-ir.v1.json`)  
- SpecV1 offline corpus (Gate 3)  
- Scored passive binding + 2× derating preference  
- Physics2 linear MNA + BE C/L stamps  
- Analytical LED / RC / RL verify heuristics  
- Vendor manufacturing DFM (floating pin, footprint, height)  
- Emit: `.kicad_sch`, `.net`, `bom.csv`, design-snapshot, verification, mfg-dfm JSON  

### 2.3 LLM frontend

- Gemini `generateContent` → schematic-ir JSON only  
- Offline prompt→fixture map when no API key  
- **Does not** run MNA/Newton/DFM/KiCad  

### 2.4 Web UI

- `python web/server.py` → chat → sync `synth generate`  
- Artifact download; open `.kicad_sch` in desktop KiCad  
- Vercel: `api/index.py` + `maxDuration` 60; generate timeout **50s**  

### 2.5 Vendor library (`vendor_next/` / `electronics_core`)

- Design IR, component models, DFM engine, part provider, E-series, **MNA + diode Newton**  
- Used live for DFM bridge; MNA used in `mna_test` only  

---

## 3. Module map (summary)

**Full per-file map:** [docs/architecture/MODULE_MAP.md](docs/architecture/MODULE_MAP.md).  
Architecture narrative: [docs/architecture/ARCHITECTURE_AUDIT.md](docs/architecture/ARCHITECTURE_AUDIT.md).  
Below: condensed module groups.

### Host (`synth_core`)

| FILE | RESPONSIBILITY | CLASS | MATH ROLE | NOTES |
|------|----------------|-------|-----------|-------|
| `main.c` | CLI dispatch | A | none | thin |
| `cli/*` | Commands / orchestration | A | none | `cmd_generate` owns pipeline |
| `compiler.c/h` | Bind + KiCad sch emit + unused Physics2 lower | Q+T | none | KiCad leak into compiler |
| `bind/bind_scorer.*` | Closest part + cost heuristic | Q | discrete search | fake cost from MPN substrings |
| `part_lib.*` | Type→pins/KiCad defaults | O/Q | none | enum kicad_id collapse |
| `db.*` | SQLite Parts/Topology/FabRules | P | none | mixed physics/procurement |
| `catalogue.*` | E-series generators | P | E-series tables | generators mostly unused |
| `jlcparts_import.*` | CSV import | P | parse units | |
| `seed/seed_topology.*` | JSON→DB | E/P | none | inserts LLM parts |
| `unit_parse.*` | SI strings | X/D | none | |
| `diag_error.*` | last-error buffer | X | none | |
| `spec/schematic_load.*` | IR load/validate/prompt route | C/B | none | |
| `spec/gemini_schematic.*` | Gemini HTTP | B | none | |
| `spec/spec_load.*` | SpecV1 | C | none | |
| `spec/llm_provider.*` | Offline SpecProvider | C | none | name misleading |
| `compose/*` + `dfm_compose.*` | Block compose + port DFM | E/R | port V/I/Z | renamed from DFM.* |
| `mfg_dfm.*` + `vendor_bridge.*` | Mfg DFM adapter | R | height only numerical | invents ratings |
| `verify/verify_report.*` | Verification JSON | S | LED/RC/RL + Physics2 | IC auto-pass |
| `emit/*` | net/BOM/snapshot | T | none | |
| `physics2_isa.*` | Opcode/instruction/pools | H | ID spaces | pools unused by stamps |
| `physics2_interpreter.*` | Runtime + stamps + GE | I/J/K/M | linear MNA + BE | diode stub |
| `physics2_types/symbols/typecheck/print.*` | Expr/IR tooling | G | metadata | not live MNA path |

### Vendor (`electronics_core`)

| FILE | RESPONSIBILITY | CLASS | MATH ROLE |
|------|----------------|-------|-----------|
| `mna.*` | DC MNA + diode Newton | F/J/K/L | Shockley + GE |
| `dfm.*` | 3 builtin rules | R | height numerical |
| `design/component/net.*` | Design IR | E/O | connectivity |
| `component_model/model_registry.*` | Catalog models | O | ranges not equations |
| `part_provider` / `kicad_generic_provider` | Part search API | Q | unused in generate |
| `e_series.*` | E-series snap | X | unused in generate |
| `vec/intern/range/constraint/diagnostic.*` | Infra | X | — |
| `PartIdentity.h` | Dead duplicate | — | SAFE DELETE candidate |

### Web / infra

| FILE | CLASS | NOTES |
|------|-------|-------|
| `web/server.py`, `api/index.py` | V | sync generate; 50s/300s timeout |
| `public/*`, `web/static/*` | V | **drifted duplicates** |
| `CMakeLists.txt`, `build.sh` | X | CMake preferred; audit used gcc |
| `tests/golden_*` | W | 21 cases |
| `vendor_next/tests/*` | W | 8 unit tests |
| `third_party/*` | X | sqlite3, cJSON |

---

## 4. End-to-end data flow

```
prompt/JSON → schematic-ir → SQLite seed → bind → verify → mfg-DFM → KiCad/BOM
                              ↑
                     Gemini may author IR (topology + parts[])
```

**Probabilistic boundary (control):** Gemini stops at IR.  
**Boundary leak (data):** IR content drives all deterministic stages.

---

## 5. Tests — passing and what they actually prove

### 5.1 Audit run (2026-09-21)

| Suite | Result |
|-------|--------|
| `vec_test` … `mna_test` (8) | **PASS** |
| `golden_runner` g01–g21 | **PASS** (`failures=0`) |
| Sanitizer suite | **NOT RUN** (libs missing) |

### 5.2 Mathematical strength

| Test | Proves math? | Actually asserts |
|------|--------------|------------------|
| g01–g03 | **Yes** | G stamp / divider voltages |
| mna_test divider/diode | **Yes** | Vout/I within tolerances |
| g12 verify | Partial | JSON + Physics2 divider |
| g17 LED | Partial | Analytical LED path / generate |
| g18 RC / g20 RL | **Weak** | verify.passed — **not** \(f_c\) or \(\tau\) |
| g21 LDO | **Weak** | structural IC pass |
| g04–g16 | Structural/integration | emit, compose, bind, corpus |
| dfm_test | Structural DFM | floating pin / footprint / height |

**Failing tests in this audit run:** none.

**Missing tests (proposed, not implemented):** AC, singularity, controlled sources, diode on Physics2, residual norms on generate path, timeout/job API.

---

## 6. Bugs and correctness issues found (not fixed in Phase 0)

| ID | Severity | Issue | Location |
|----|----------|-------|----------|
| B1 | High | LLM MPNs/topology seed trusted DB | gemini + seed_topology |
| B2 | High | Transistor/IC verify always structural pass | verify_report.c |
| B3 | Med | Dual MNA ground conventions | physics2 vs mna.h |
| B4 | Med | `mna_validate` diode residual vacuous | mna.c |
| B5 | Med | V-source residual vs current tolerance units | mna.c |
| B6 | Med | Missing rating ≡ 0 ≡ skip; bridge invents V | db + vendor_bridge |
| B7 | Med | Most DFM profile fields unused | vendor dfm |
| B8 | Med | Sync generate timeout architecture | web/server.py:351 |
| B9 | Low | `public/` vs `web/static/` drift | UI |
| B10 | Low | ISA param/state pools unused by stamps | physics2_isa vs interpreter |
| B11 | Low | Unused statics (`matrix_node_entry`, `find_connection_any`) | mna.c, compiler.c |
| B12 | Low | Header says 3 Gemini attempts; loop allows 6 | gemini_schematic |
| B13 | Low | Polymorphic `Parts.value` (ohm vs Vf/Vr) | db + jlcparts + verify |

---

## 7. C code quality / optimization notes (observe only)

### Compile hygiene

- Project compiles cleanly under `-std=c11` with pedantic set; **~30** project warnings mostly `-Wfloat-conversion` on `isfinite` (MinGW).  
- Dead statics should be removed or used (cleanup phase).  
- `ImportJlcPartsRow` missing prototype.

### Performance / numerics (not “optimized” in Phase 0)

| Topic | Observation |
|-------|-------------|
| Dense GE every solve | Fine for tiny nets; will not scale — need sparse later |
| No factorization reuse | Every Newton / step refactorizes |
| Physics2 pivot absolute 1e-14 | Fragile vs scaled systems |
| Vendor Newton damping | Good minimal aid; no gmin/source stepping |
| SQLite work DB per generate | OK for CLI; web sync amplifies latency |
| String/MPN cost heuristic | Not real optimization |

**Do not** micro-optimize stamps before unifying the solver contract.

---

## 8. Timeouts (do not blindly increase)

| Layer | Value | File |
|-------|-------|------|
| Web subprocess | 50s if `VERCEL` else 300s | `web/server.py` |
| Vercel function | `maxDuration` 60 | `vercel.json` |
| Gemini curl | 45s × up to 6 attempts | `gemini_schematic.c` |

**Root cause of “generate timed out”:** synchronous `subprocess.run(..., timeout=timeout_sec)` around the entire engineering pipeline.

**Correct fix direction:** async job + worker (documented in ARCHITECTURE_AUDIT), not a larger number alone.

---

## 9. KiCad integration (understood)

- **Not** browser KiCad Python.  
- C writes S-expression schematic text.  
- Optional `scripts/run_kicad_erc.sh` uses `kicad-cli`.  
- UI downloads artifacts for desktop KiCad.

---

## 10. Database semantics (understood)

| Column / concept | Should be | Today |
|------------------|-----------|-------|
| Resistance / C / L | Physics parameter | Mixed into `value` |
| MPN / package / stock | Procurement | Same `Parts` row |
| V/I/P ratings | Ratings / DFM inputs | Same row; 0 = missing |
| Fab geometry | Manufacturing | `FabRules` unused; JSON profiles used |

---

## 11. DFM (understood)

Three names:

1. **dfm_compose** — block port compatibility (structural + V/I/Z).  
2. **mfg_dfm → vendor dfm** — floating pin, missing footprint, component height.  
3. **FabRules** — SQLite orphan.

Physical feasibility ≠ manufacturing ≠ procurement — currently blurred.

---

## 12. What was *not* committed as “changes”

Phase 0 intentionally **did not**:

- Delete `synth.exe` / `out_*` / dead headers  
- Unify solvers  
- Raise timeouts  
- Implement AC/Newton on Physics2  
- Fix verify auto-pass  
- Sync `public/` and `web/static/`  

Those await Phase 1 after architecture review. Recommended order: **AUDIT_REPORT §U**.

---

## 13. How to reproduce the audit build

```bat
REM MinGW gcc on PATH
cd d:\physics-synthesis-compiler
REM objects/binaries already under audit_build\ from Phase 0
set SYNTH_FIXTURE_ROOT=d:\physics-synthesis-compiler
audit_build\golden_runner.exe
audit_build\mna_test.exe
```

Or follow [docs/build/BUILD_AUDIT.md](docs/build/BUILD_AUDIT.md).

Preferred long-term: install CMake and use README build instructions.

---

## 14. Bottom line

This repo is a **real, compiling, test-passing synthesis toolchain** with a **credible linear MNA + experimental diode Newton**, but it is **architecturally split**, **LLM-data-porous**, and **not yet** the deterministic multi-analysis physics compiler described in the long-term vision.

Phase 0 ends with documents and a clean compile/test baseline. **Phase 1 starts only after review.**
