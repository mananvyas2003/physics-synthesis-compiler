# AUDIT_REPORT.md — Phase 0 Executive Summary

**Date:** 2026-09-21  
**Scope:** Full mathematical, semantic, architectural, build, and code-quality audit.  
**Phase 0:** documentation + clean compile/test only.  
**Phase 1 (in progress):** Physics2 live contract frozen; verify fail-closed for IC/transistor; Gemini `parts: []`; `mna_validate` voltage residual fix. See [docs/math/MNA_CONTRACT.md](docs/math/MNA_CONTRACT.md).

Detailed companions:

- [AUDIT_README.md](AUDIT_README.md) — features, module map, tests, bugs, C notes  
- [docs/math/MNA_AUDIT.md](docs/math/MNA_AUDIT.md)  
- [docs/architecture/ARCHITECTURE_AUDIT.md](docs/architecture/ARCHITECTURE_AUDIT.md)  
- [docs/architecture/MODULE_MAP.md](docs/architecture/MODULE_MAP.md) — per-file module map  
- [docs/architecture/CLEANUP_REPORT.md](docs/architecture/CLEANUP_REPORT.md)  
- [docs/build/BUILD_AUDIT.md](docs/build/BUILD_AUDIT.md)

---

## A. What this repository actually is today

A **C11 deterministic electronics synthesis CLI** with:

- JSON schematic-IR / SpecV1 / seed fixtures  
- Optional Gemini prompt → schematic-IR  
- SQLite part DB + scored binder  
- Verification (analytical LED/RC/RL + Physics2 linear DC)  
- Manufacturing DFM sidecar (vendor_next, 3 rules)  
- C-emitted KiCad schematic / netlist / BOM  
- Browser UI that shells `synth generate` synchronously  

It is **not** yet a next-gen DAE/AC/nonlinear production simulator, nor a clean eng-DSL compiler with a sealed LLM boundary on data.

---

## B. What the mathematical engine actually solves today

**Two engines:**

1. **Physics2** (live verify path): real dense \(A x = b\) linear MNA; BE companion for C/L when stepped; controlled sources present; diode/BJT/MOS **non-executable**.  
2. **vendor_next MNA** (unit tests): DC \(A x = z\) with R, V, Shockley diode Newton + residual checks.

**Unknowns:** see MNA_AUDIT — different ground conventions between stacks.

---

## C. What the current primitives mean mathematically

| Primitive | Meaning in code |
|-----------|-----------------|
| R | \(G=1/R\) conductance stamp |
| C/L | BE discrete companion (Physics2); not DC open/short |
| V / I | Ideal source constraint / RHS injection |
| Controlled | Linear dependent sources (Physics2) |
| Diode (vendor) | Shockley + Newton companion \(G_d, I_{eq}\) |
| Diode/BJT/MOS (Physics2) | Data placeholders; stamp fails |
| `component_model` | Catalog metadata, not circuit equations |

---

## D. What is correct

- Linear resistor networks (goldens g01–g03; vendor divider).  
- Vendor Shockley diode operating point (mna_test analytic-ish check).  
- Opaque Physics2 accumulator (hides dense storage from stamps).  
- Offline deterministic path without Gemini.  
- Gemini does not call solvers/DFM/KiCad APIs.  
- Build: all golden + vendor tests **PASS** under MinGW GCC 12.1 strict flags (see BUILD_AUDIT).

---

## E. What is incomplete

- AC / complex MNA  
- True DAE (adaptive BE/trap/BDF, LTE)  
- Physics2 nonlinear residual/Jacobian ABI  
- Generate-path use of vendor Newton  
- Most DFM profile numerical rules  
- PCB backend  
- Async job architecture  
- Engineering DSL above IR  
- Analytical tests for RC/RL τ, controlled sources, singularities  

---

## F. What is mathematically incorrect / fragile

- `mna_validate` diode residual vacuous (`<= DBL_MAX`).  
- Voltage-source residual compared using current tolerance (unit mismatch).  
- Exp clamp \(\pm 40 n V_t\) alters Shockley far from bias.  
- Physics2 absolute pivot \(10^{-14}\) vs vendor scale-aware — fragile scaling.  
- Missing ratings → 0 → skip checks; bridge invents 50 V / 5 V.  
- IC/transistor verify **structural auto-pass** (not a physics pass).

---

## G. What is architecturally dangerous

1. LLM-authored `parts[]` / topology trusted as seed for “deterministic” pipeline.  
2. Dual solvers with divergent ground/unknown conventions.  
3. Sync web timeout (50s Vercel) as lifetime of full generate+Gemini.  
4. False verify confidence on IC/transistor.  
5. Manufacturer identity living on compile IR (`DBPart` in components).  

---

## H. What files are redundant

See CLEANUP_REPORT: tracked `synth.exe`, tracked `out_*`, `PartIdentity.h`, duplicate static UI, unused catalogue E-series generators, dead FabRules usage, unused static helpers.

---

## I. What should be deleted (after review)

SAFE DELETE from git: `synth.exe`, committed `out_*`, `PartIdentity.h`, complete `DFM.*` rename.  
LIKELY: consolidate `public/` vs `web/static/`; unused catalogue generators.

---

## J. What should NOT be deleted

- Physics2 ISA + controlled-source stamps  
- vendor MNA diode Newton  
- `dfm_compose` (different job from mfg DFM)  
- Lowering APIs / opcode stubs kept as future contracts until replaced  

---

## K. What the Physics ISA should mean

A **small machine-like instruction stream**: opcode + typed node/branch/param/state IDs.  
Parameters ≠ manufacturer. State ≠ parameters. No KiCad, no MPN, no solver memory layout.  
Same instruction executable from LLM IR, handwritten tests, or future optimizer.

Today: structure exists; param/state pools not driving stamps; nonlinear opcodes non-executable.

---

## L. What the future engineering language should mean

Express requirements, rails, components, nets, ports, topology, constraints, interfaces, analysis requests, manufacturing requirements.  
**Not** matrix indexes, Newton loops, KiCad S-expr, private MPN physics.

Today: schematic-ir + SpecV1 JSON only.

---

## M. What Gemini should and should not do

| Should | Should not |
|--------|------------|
| Prompt → structured eng intent / IR without catalogue MPNs | Solve MNA / Newton / integration |
| Stay outside verify/DFM/emit | Direct KiCad |
| Optional frontend only | Pick arbitrary MPNs as truth |
| | Repair failed physics by mutating state |
| | Decide convergence |

---

## N. What the MNA engine needs next

1. Single documented ground/unknown contract.  
2. One live backend on generate verify.  
3. Accumulator-only stamps (vendor should match Physics2 opacity).  
4. Residual-based validation with correct units.  
5. Analytical regression for divider + diode OP on live path.

---

## O. What nonlinear support needs next

- Global Newton owner  
- Device residual + Jacobian contributions  
- Port vendor Shockley into that ABI (no private loop in device long-term)  
- gmin / source stepping later  

---

## P. What AC support needs next

- Complex accumulator  
- \(Y_C=j\omega C\), \(Y_L=1/(j\omega L)\)  
- Small-signal from \(J(x_0)\) after nonlinear DC  

Do not confuse with large-signal transient.

---

## Q. What transient support needs next

- Document BE as current method  
- DC operating point for C/L  
- Optional trap/BDF + adaptive \(\Delta t\) after DC/nonlinear solid  

---

## R. What sparse linear algebra needs next

Keep stamp ABI backend-agnostic; add sparse direct (then iterative) behind accumulator. No device rewrites.

---

## S. What database / DFM need next

- Separate physics params / model params / procurement / DFM constraints  
- Never treat missing as 0 rating  
- Data-driven constraint rules; implement profile fields actually used  
- Decide fate of FabRules vs JSON profiles  

---

## T. Tractable optimization problems (identify only)

| Problem | Decision vars | Objective | Hard constraints |
|---------|---------------|-----------|------------------|
| Resistor E-series pick | discrete R | error to target | derating, package |
| LED series R | R | current error | power, Vf window |
| RC cutoff | R,C discrete | \(\|f_c - f^*\|\) | ratings |
| Bind cost | MPN choice | minimize fake/real cost | 2× derating |
| Height/package | footprint | feasibility | max height DFM |
| BOM cost | MPN set | Σ cost | electrical + DFM |

No general global topology optimizer yet.

---

## U. Exact recommended implementation order (Phase 1+)

1. **Freeze math contracts** (one ground convention; stamp bible in docs/math).  
2. **Unify solver ownership** on generate verify (Physics2 *or* vendor MNA); keep the other as oracle/tests.  
3. **Residual/Jacobian ABI**; executable diode on that ABI.  
4. **Analytical tests** on live path (divider, diode OP, singularity fail-closed).  
5. **LLM boundary:** IR without catalogue MPNs; bind from DB only.  
6. **Fail-closed verify** for unsupported topologies (no IC auto-pass).  
7. **Async generate jobs** (fix 50s architecture without blind timeout bumps).  
8. SAFE DELETE cleanup from CLEANUP_REPORT.  
9. Only then: AC, richer DAE, sparse, data-driven DFM, bounded optimizers.

**Stop here for architecture review. Do not enter Phase 1 automatically.**
