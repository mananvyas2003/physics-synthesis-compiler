# Cleanup Report — Phase 0

**Date:** 2026-09-21  
**Rule:** Document only. **Do not delete** in Phase 0.

For each candidate:

- SAFE DELETE / LIKELY DELETE / KEEP / KEEP FOR FUTURE CONTRACT / UNKNOWN

---

## SAFE DELETE (from version control)

| File / path | Why redundant | Who references | What replaces | Test evidence | Migration risk |
|-------------|---------------|----------------|---------------|---------------|----------------|
| Tracked `synth.exe` | Built binary; `/*.exe` already in `.gitignore` but still tracked | Local/CLI users building | `audit_build/synth.exe` or CMake `build/synth` | Rebuild + golden | Low |
| Tracked `out_c/`, `out_g4/`, `out_harden/`, `out_p/`, `out_spec/` | Generate artifacts (sch/net/bom/db/json) | None in build | Re-run `synth generate` | golden uses fixtures not these | Low |
| `vendor_next/src/PartIdentity.h` | Duplicate `PartRequest`; never `#include`d | None | `part_provider.h` | vendor tests ignore it | None |
| Finish git delete of `DFM.c`/`DFM.h` | Renamed to `dfm_compose.*` | Was compose/CLI | `dfm_compose.c/h` | g04_dfm_suite, cmd_dfm | None if rename complete |

Untracked local `out_led*`, `out_rc`, …: safe to delete locally; add `out_*/` to `.gitignore` when cleanup is approved.

---

## LIKELY DELETE / consolidate

| File / path | Why | References | Replacement | Risk |
|-------------|-----|------------|-------------|------|
| One of `public/` vs `web/static/` | Diverged copies; server prefers `public/` if present | Vercel `outputDirectory: public`; `web/server.py` fallback | Single source + sync in `build.sh` | Medium (deploy drift) |
| `catalogue.c` `GenerateE24/E96/E6*` + helpers | No callers outside catalogue.c | Header exports | JLCPCB import + fixtures; or wire to `db seed` | Low if unused confirmed |
| Duplicate DFM profile constants | Same numbers in `mfg_dfm.c`, `vendor_next/dfm.c`, `fixtures/dfm/*.json` | All three | Fixtures as SSOT | Low |
| `SeedFabRules` call path | Never called from generate | db/catalogue API | `mfg_dfm` profiles | Low–med if DB fab planned |
| Unused `e_series.c` from synth link *or* start using it | Linked but no generate callers | CMake electronics_core | bind/catalogue | Low |

---

## KEEP

| File / path | Reason |
|-------------|--------|
| `physics2_*` | Live ISA + runtime on verify path |
| `vendor_next/src/mna.*` | Unique Shockley Newton math; tests pass |
| `dfm_compose.*` | Block composition DFM (different product) |
| `mfg_dfm.*`, `vendor_bridge.*`, `vendor_next/dfm.*` | Live manufacturing DFM path |
| `db.*`, `part_lib.*`, `bind_scorer.*` | Core host pipeline |
| `jlcparts_import.*` | Active CSV import |
| `spec/*`, schemas, fixtures | IR contracts and corpora |
| `verify/verify_report.*` | Gate 6 verification |
| `emit/*` | BOM/net/snapshot |
| Web server + static UI | Product surface |
| `third_party/*` | Required amalgams |
| Ponytail / `.cursor` | Agent tooling |

---

## KEEP FOR FUTURE CONTRACT

| File / path | Mathematical / architectural capability |
|-------------|----------------------------------------|
| Physics2 diode/BJT/MOS/logic opcodes | Opcode slots for nonlinear & digital — stubs only |
| `CompilerPhysDesign` / `compiler_lower_to_physics2` | Lowering API unused by generate — future wire |
| `kicad_generic_provider.c` | Vendor part provider API; tests use mocks |
| `FabRules` table / CRUD | DB-backed fab limits (unused) |
| Controlled-source stamps in Physics2 | Linear MNA coverage beyond R |
| SpecV1 + `llm_provider` offline map | Gate 3 eng-spec front door |

**Do not delete** unique math because it is unused on the generate path.

---

## UNKNOWN (needs product decision)

| Item | Question |
|------|----------|
| `cmd_compile` divider path | Deprecate vs keep demo? |
| Keep both solvers long-term | Unify on Physics2 vs vendor MNA vs dual with clear roles? |
| `component_model` catalog vs Physics2 payloads | Single model parameter story? |

---

## Repeated semantic operations (consolidate later)

1. E-series mantissa tables: catalogue vs `e_series.c`.
2. Closest-value selection: bind_scorer vs part_provider.
3. DFM profile structs: three copies.
4. Frontend assets: public vs web/static.
5. Part type enums: `PartTypes` vs `ComponentKind` vs IR strings.

---

## Unreachable / dead helpers (code quality, not delete yet)

| Symbol | File | Note |
|--------|------|------|
| `matrix_node_entry` | `mna.c` | unused static; risky if called with node 0 |
| `find_connection_any` | `compiler.c` | unused static |
| `physics_param_get` / `physics_state_get` | `physics2_isa.c` | not used by stamps |
| `GenerateE24` family | `catalogue.c` | no callers |

---

## Phase 0 action

**None.** Await architecture review before any SAFE DELETE commit.
