# MNA Contract — Phase 1 (frozen)

**Live generate/verify solver:** Physics2 (`physics2_interpreter.c`)  
**Oracle / unit tests:** vendor_next MNA (`vendor_next/src/mna.c`) — not called from `verify_bound_schematic`

This document freezes conventions. Changing them requires a regression test and an AUDIT note.

---

## 1. Unknown vector (Physics2 — live)

\[
x = [V_0, V_1, \ldots, V_{n_{\mathrm{nodes}}-1},\, I_{b0}, \ldots]^T
\]

- Size: `next_node + branch_count`
- Reference node `gnd` is included in \(x\), then forced: row/col cleared, \(A_{gg}=1\), \(b_g=0\) ⇒ \(V_{\mathrm{gnd}}=0\)
- Branch current: from first terminal of that port to the second

**vendor_next (oracle only):** node 0 = GND omitted from \(x\); \(x=[V_1..V_N, I_{\mathrm{vs}}]\)

---

## 2. Sign convention (both)

KCL: current **leaving** a node is positive on that node’s equation.  
Ideal V-source: \(V_+ - V_- = V_s\); branch current unknown enters \(+\) node as \(+I_b\).

---

## 3. Supported live devices (Physics2 stamps)

| Device | Status on generate verify |
|--------|---------------------------|
| R | yes (DC path) |
| Ideal V (injected for VIN/GND) | yes |
| C / L | BE when stepped; **DC:** C open / L short (approx G) via PhysDesign→Physics2; capacitors remain in instruction stream |
| I, controlled sources | stamped in ISA; not yet built from `CompiledSchematic` |
| Diode | LED analytical **or** Physics2 Newton (g22); stamp companion |
| BJT / MOS / IC | **fail-closed** (unsupported) |

**Authoritative lowering:** `compiler_schematic_to_phys_design` → `compiler_lower_to_physics2` is the live verify path (2026-09-22 Phase 2). Vendor MNA remains oracle-only.

---

## 4. Linear system

\[
A x = b \quad \text{(dense GE, partial pivoting)}
\]

Pivot fail if absolute pivot \(< 10^{-14}\) (Physics2).

---

## 5. Nonlinear / AC / DAE

| Mode | Live | Oracle |
|------|------|--------|
| Newton diode | not on generate path | `mna_solve` |
| AC \(j\omega\) | NOT IMPLEMENTED | NOT IMPLEMENTED |
| Transient | BE C/L in Physics2 step API | NOT IMPLEMENTED |

---

## 6. Ownership

- Constitutive laws → stamp functions / analytical helpers  
- Global Newton (when added) → runtime, not inside a device  
- MPN / package → DB binder only, not Physics ISA  

See also [MNA_AUDIT.md](MNA_AUDIT.md).
