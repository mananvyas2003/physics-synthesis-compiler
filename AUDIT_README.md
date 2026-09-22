# AUDIT_README.md — Features, Audit, Phase 1, CI History

Living document: Phase 0 audit + Phase 1 completion work through 2026-09-22.

**Companions:** [AUDIT_REPORT.md](AUDIT_REPORT.md) · [docs/architecture/MODULE_MAP.md](docs/architecture/MODULE_MAP.md) · [docs/math/](docs/math/)

---

## 1. What this product is

C11 **deterministic electronics synthesis compiler** + Physics2 MNA runtime + vendor DFM/MNA oracle, with optional Gemini IR frontend and a chat UI.

Not: LLM physics solver, Gemini→KiCad wrapper, toy SPICE clone.

Pipeline:

```
prompt/JSON → schematic-ir → seed/bind → verify (Physics2 / analytical) → mfg-DFM → KiCad/BOM text
```

---

## 2. Features (implemented)

| Area | Feature |
|------|---------|
| CLI | `generate`, `db`, `compile`, `dfm`, offline fixtures |
| Physics2 | Linear MNA; BE C/L; controlled sources; **Shockley diode Newton** |
| Vendor MNA | DC R+V+diode Newton (**oracle / mna_test**) |
| Verify | LED/RC/RL analytical; resistor DC; **IC/transistor fail-closed** |
| Gemini | IR only; **`parts: []`** — no invented MPNs |
| Web | Sync `/api/chat`; **async** `POST /api/generate` + `GET /api/jobs/{id}` |
| KiCad | C text emit + optional `kicad-cli` ERC |
| CI | ubuntu/windows build-test, asan, kicad-erc |

---

## 3. Phase 0 audit deliverables

| Doc | Content |
|------|---------|
| AUDIT_REPORT.md | Executive A–U |
| docs/math/MNA_AUDIT.md | Equations ↔ code |
| docs/math/MNA_CONTRACT.md | Frozen Physics2 live contract |
| docs/math/REFERENCE_ORACLE.md | ngspice/Xyce/hand matrix |
| docs/math/TEST_MATRIX.md | Primitive × analysis cells |
| docs/math/OPTIMIZATION.md | Bounded optimizers |
| docs/math/AC_DAE_SPARSE.md | Deferred contracts |
| docs/architecture/* | Architecture, cleanup, full module map |
| docs/build/BUILD_AUDIT.md | Toolchain / tests |

---

## 4. Phase 1 changes (code)

| Change | Why |
|--------|-----|
| Physics2 diode companion + Newton in `context_step` | Residual/Jacobian-style linearization; runtime owns iteration |
| Golden **g22_diode_newton** | Analytical OP agreement with vendor |
| IC/transistor verify fail-closed | No fake structural pass |
| Gemini `parts: []` | Seal LLM catalogue pollution |
| `mna_validate` voltage residual units | Oracle correctness |
| IR rail-to-rail resistor allow (R≥0.1) | Gate4 LDO feedback / ERC |
| CMake `electronics_core` → `m` | Linux `exp` link for mna_test |
| `part_lib.h` `#include <stddef.h>` | Linux `size_t` |
| `vec.h` portable alignment | MSVC C |
| Async generate jobs in `web/server.py` | Request ≠ simulation lifetime |
| SAFE DELETE untrack `synth.exe`, `out_*`, `PartIdentity.h` | Cleanup report |

---

## 5. Commits after Phase 0 (history)

| Commit | Summary |
|--------|---------|
| `0f316dd` | stddef + MSVC vec alignment (CI/Vercel compile) |
| `41ac935` | libm on electronics_core (Linux mna_test link) |
| `895aa3d` | Allow divider resistors across rails (kicad-erc) |
| *(this work)* | Diode Newton, docs depth, async jobs, SAFE DELETE |

Earlier Phase 1 body (fail-closed, Gemini parts, mna_validate) landed in working tree / prior `update` commits — see git log around `eba322e`/`4b00bd6`.

---

## 6. Tests

| Suite | Status |
|-------|--------|
| golden g01–g21 | PASS (g21 expects fail-closed) |
| golden **g22** | PASS diode OP |
| vendor mna_test + 7 others | PASS |
| CI asan / ubuntu / windows / kicad-erc | green on `895aa3d`+ |

---

## 7. Bugs addressed vs remaining

**Fixed:** vacuous diode validate units; IC auto-pass; Gemini MPN invent in prompt; Linux/MSVC/Vercel compile; mna_test `-lm`; Gate4 IR false positive.

**Remaining:** AC/DAE/sparse (contract only); verify still analytical for LED not Physics2 diode; async UI not wired in frontend poll yet; dual static UI trees; most DFM profile fields unused.

---

## 8. C notes

Newton for diodes is **global** in `physics2_context_step` (max 100 iters, 0.25 V damp). Stamps stay companion-only. Dense GE remains; sparse deferred.

---

## 9. Recommended next

1. Wire LED/diode fixtures through Physics2 verify  
2. Frontend poll `/api/jobs/{id}`  
3. Complex accumulator + AC smoke  
4. Data-driven DFM rules  

See AUDIT_REPORT §U and `docs/math/AC_DAE_SPARSE.md`.
