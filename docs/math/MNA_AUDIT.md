# MNA Mathematical Audit — Phase 0

**Rule:** Every equation is tied to a code location, or marked **NOT IMPLEMENTED**.  
**Stacks audited:** (1) Physics2 interpreter, (2) vendor_next MNA.

---

## 1. MNA unknown vector

### vendor_next (`vendor_next/src/mna.h`, `mna.c`)

\[
x = [V_1, V_2, \ldots, V_N, I_{\mathrm{vs},1}, \ldots, I_{\mathrm{vs},M}]^T
\]

- Size: `node_count + voltage_source_count` (`mna.c` ~795).
- Node \(i\) (1..N) → index \(i-1\); node 0 (GND) **not** in \(x\) (`node_value`, ~292–302).
- Code comment: `mna.h` ~190–195.

### Physics2 (`physics2_interpreter.c`)

\[
x = [V_0, V_1, \ldots, V_{n-1}, I_{b0}, \ldots]^T
\]

- Size: `program->next_node + program->branch_count` (~1778–1802).
- Reference node included, then forced to 0 via row/column replacement (~242–249).
- Branch current convention: from first terminal of that port to the second (controlled-source comments).

---

## 2. Global matrix structure

Canonical modified nodal form:

\[
\begin{bmatrix} G & B \\ C & D \end{bmatrix}
\begin{bmatrix} v \\ j \end{bmatrix}
=
\begin{bmatrix} i \\ e \end{bmatrix}
\]

| Stack | Storage | Equation |
|-------|---------|----------|
| vendor_next | Dense row-major `MnaMatrix` | \(A x = z\) (`mna.c` ~365–369) |
| Physics2 | Opaque `PhysicsAccumulator` dense `matrix`/`rhs` | \(A x = b\) via `physics2_accumulator_solve` |

Neither stack exposes sparse CSR/CSC to devices. Physics2 accumulator **hides** storage from stamp callers (`physics2_accumulator_add`). Vendor stamps write `MnaMatrix` directly.

---

## 3. KCL / KVL derivation

**KCL (both stacks):** currents **leaving** a node contribute positively to that node’s equation.

**KVL / branch constraints:** ideal voltage sources add unknown branch current \(I_b\) and equation \(V_+ - V_- = V_s\).

Polarity for V-source stamp (vendor): \(A_{a,k}{+}{=}1\), \(A_{k,a}{+}{=}1\), \(A_{b,k}{-}{=}1\), \(A_{k,b}{-}{=}1\), \(z_k {+}{=} V_s\).

Physics2 I-source: current from `terminals[0]`→`[1]` ⇒ `rhs[0] -= I`, `rhs[1] += I`.

---

## 4. Resistor stamp

**Law:** \(I = G(V_p - V_n)\), \(G = 1/R\).

**Local contribution:**

\[
\begin{bmatrix} +G & -G \\ -G & +G \end{bmatrix}
\begin{bmatrix} V_p \\ V_n \end{bmatrix}
\]

| Stack | Code |
|-------|------|
| Physics2 | `physics2_interpreter.c` ~346–362 |
| vendor_next | `stamp_resistor` `mna.c` ~390+; assembly ~526 |

**Status:** CORRECT for linear DC (golden g01–g03; mna divider test).

---

## 5. Capacitor stamp

**Time domain:** \(i = C \frac{dv}{dt}\).

**Backward Euler (Physics2 only):** \(G = C/\Delta t\), companion:

\[
i^{n+1} = G(v^{n+1} - v^n)
\]

Conductance stamp like resistor; RHS \(\pm G v_{\mathrm{prev}}\) (`physics2_interpreter.c` ~440–464).

**vendor_next:** **NOT IMPLEMENTED**.  
**Physics2 DC (\(\Delta t\to\infty\) / open C):** **NOT IMPLEMENTED** (requires `timestep > 0`).

---

## 6. Inductor stamp

**Time domain:** \(v = L \frac{di}{dt}\).

**Backward Euler (Physics2):** \(G = \Delta t/L\); update \(i = i_{\mathrm{prev}} + G(v_p-v_n)\); RHS \(\mp i_{\mathrm{prev}}\) (inductor stamp region ~500+).

**vendor_next:** **NOT IMPLEMENTED**.  
**DC short-L reduction:** **NOT IMPLEMENTED**.

---

## 7. Voltage-source stamp

**Constraint:** \(V_p - V_n = V_s\); unknown \(I_b\).

Implemented in both stacks (Physics2 VSOURCE; vendor `MnaVoltageSource`).

---

## 8. Current-source stamp

**Law:** independent \(I\) from terminal 0 → 1 into RHS only.

| Stack | Status |
|-------|--------|
| Physics2 | Implemented |
| vendor_next | **NOT IMPLEMENTED** |

---

## 9–12. Controlled sources

| Device | Equation (Physics2) | vendor_next |
|--------|---------------------|-------------|
| VCVS | \(V_p-V_n - \mu(V_{cp}-V_{cn})=0\) + branch | **NOT IMPLEMENTED** |
| VCCS | \(I_{p\to n}=g_m(V_{cp}-V_{cn})\) | **NOT IMPLEMENTED** |
| CCVS | sense short + \(V_p-V_n - r_m i_s=0\) (2 branches) | **NOT IMPLEMENTED** |
| CCCS | sense short + \(I=\beta i_s\) | **NOT IMPLEMENTED** |

Physics2: stamps in `physics2_interpreter.c` (VCVS/VCCS/CCVS/CCCS regions ~700–1050).  
**Tests:** no dedicated golden for controlled sources. Treat as **PARTIALLY CORRECT** (code present, weak coverage).

---

## 13. DC formulation

\[
A x = b \quad \text{(linear)} \qquad
A(x_k)\, x_{k+1} = z(x_k) \quad \text{(vendor Newton companion)}
\]

| Capability | Physics2 | vendor_next |
|------------|----------|-------------|
| Linear R+V networks | CORRECT | CORRECT |
| Independent I | CORRECT | NOT IMPLEMENTED |
| Controlled sources | PARTIALLY CORRECT | NOT IMPLEMENTED |
| Diode DC | NOT IMPLEMENTED (stamp returns false ~1094–1112) | CORRECT (Newton) |
| Floating nodes | Singular / fail GE | Singular / `MNA_STATUS_SINGULAR_MATRIX` |
| Conflicting ideal sources | Singular | Singular |

---

## 14. AC formulation

Intended: \(A(j\omega)\, x(j\omega) = b(j\omega)\), \(Y_C = j\omega C\), \(Y_L = 1/(j\omega L)\).

| Requirement | Status |
|-------------|--------|
| Complex matrix / RHS / solution | **NOT IMPLEMENTED** (real `double` only) |
| Frequency sweep / phase / magnitude | **NOT IMPLEMENTED** |
| Small-signal from nonlinear OP | **NOT IMPLEMENTED** |

Physics2 accumulator could be generalized to complex later; vendor MNA likewise. **No current abstraction is complex-ready without API change.**

---

## 15. Transient formulation

Physics2: fixed-step Backward Euler for C and L via `physics2_context_step`.  
**Not:** trapezoidal, BDF, adaptive \(\Delta t\), LTE estimates, rejection.

vendor_next: **NOT IMPLEMENTED**.

State: previous \(v\) / \(i\) in `PhysicsExecutionContext.states[]`. ISA `PhysicsStatePool` allocated but **not read by stamps** — semantic disconnect.

---

## 16. Nonlinear residual formulation

vendor_next: after Newton accept, `evaluate_residuals` checks KCL and V-source equations.  
Physics2: **NOT IMPLEMENTED** (no residual vector API).

---

## 17. Jacobian formulation

vendor_next diode: \(G_d = dI/dV\) from Shockley; companion \(I \approx G_d V + I_{eq}\), \(I_{eq}=I(V_{\mathrm{old}})-G_d V_{\mathrm{old}}\) (`mna.c` ~444–449).  
Physics2: **NOT IMPLEMENTED** (no Jacobian ABI).

---

## 18. Newton algorithm

vendor_next `mna_solve`: assemble linearized system → GE → voltage-step damping (`maximum_voltage_step_v` default 0.25) → residual check → iterate (max 100).  
Physics2: **NOT IMPLEMENTED**.

---

## 19. Convergence criteria

Defaults (`mna_options_default` ~255–268):

- `absolute_current_tolerance_a = 1e-12`
- `relative_tolerance = 1e-9`
- `pivot_tolerance = 1e-12`

Convergence on true residuals (KCL current, V-source voltage).  
**Caveat:** relative scale derived from residual magnitude itself can collapse rel vs abs for KCL.

Physics2: single linear solve; no iteration tolerances.

---

## 20. Conditioning / singularity

| | Physics2 | vendor_next |
|--|----------|-------------|
| Method | Dense GE, partial pivoting | Dense GE, partial pivoting |
| Singularity | `|pivot| < 1e-14` absolute | scale-aware vs `pivot_tolerance` |
| Condition estimate | **NOT IMPLEMENTED** | **NOT IMPLEMENTED** |
| Scaling | **NOT IMPLEMENTED** | row scale for pivot test only |
| Factorization reuse | **NOT IMPLEMENTED** | **NOT IMPLEMENTED** |

Floating nodes / conflicting sources → singular → fail status / false.

---

## 21. Future sparse solver requirements

Devices must stamp only through an accumulator ABI (Physics2 already does). Needed:

- Symbolic structure from topology
- Numeric fill-in factorization
- Optional iterative backends

**Do not** expose CSR indices inside primitives. **NOT IMPLEMENTED** today.

---

## 22. Future nonlinear convergence aids

Reference points (Xyce/ngspice concepts — not copied): line search, trust region, gmin stepping, source stepping, homotopy, voltage limiting.

**Present today (vendor only):** voltage-step damping.  
**Absent:** gmin, source stepping, homotopy, line search, trust region.

---

## 23. Diode derivation

Shockley:

\[
I = I_s\left(e^{V_d/(n V_t)} - 1\right), \quad V_d = V_a - V_c, \quad V_t = kT/q
\]

\[
G_d = \frac{dI}{dV} = \frac{I_s}{n V_t}\, e^{V_d/(n V_t)}
\]

Companion: \(I(V) \approx I(V_0) + G_d(V-V_0) = G_d V + I_{eq}\).

| | Status |
|--|--------|
| Diode (vendor) | Implemented (`mna.c` Shockley + Newton); exp clamp ±40 nVt |
| Physics2 diode | **IMPLEMENTED** companion stamp + Newton in `physics2_context_step`; golden g22 |
| AD / dual numbers | **NOT IMPLEMENTED** |

`mna_validate` diode “residual” stores diode current and compares to `DBL_MAX` — **not a meaningful equation residual** (~1079–1089).

---

## 24. Transistor requirements

**Minimum runtime interface before BJT/MOS can execute correctly:**

1. Residual \(F(x)\) evaluation for multi-terminal nonlinear devices.
2. Sparse Jacobian contributions (partials w.r.t. terminal voltages / branch currents).
3. Global Newton owner (not inside the device).
4. Operating-region logic as constitutive evaluation only.
5. Optional limiters as solver policy, not opcode hacks.

| Model | Status |
|-------|--------|
| Physics2 TRANSISTOR / LOGIC_GATE | Stamp `return false` |
| `component_model` MOSFET/BJT | Catalog ranges (Vth, Rds_on), **not** \(I_D(V_{GS},V_{DS})\) |
| vendor MNA BJT/MOS | **NOT IMPLEMENTED** (stubs in tests only) |

---

## 25. Nonlinear small-signal AC derivation

Correct sequence:

\[
\text{nonlinear DC OP } x_0 \;\to\; J(x_0) \;\to\; \text{complex AC } A(j\omega;x_0)\, \tilde x = \tilde b
\]

**NOT IMPLEMENTED** end-to-end. Vendor has \(J\) pieces for diode DC only; no complex AC. Physics2 has neither nonlinear OP nor complex math.

---

## Primitive summary table

| Primitive | Physics2 | vendor MNA | DC | AC | Transient | Nonlinear |
|-----------|----------|------------|----|----|-----------|-----------|
| R | yes | yes | CORRECT | N/A | N/A | linear |
| C | BE | no | UNDEFINED DC | no | PARTIAL BE | linear |
| L | BE | no | UNDEFINED DC | no | PARTIAL BE | linear |
| V | yes | yes | CORRECT | no | N/A | linear |
| I | yes | no | CORRECT | no | N/A | linear |
| VCVS/VCCS/CCVS/CCCS | yes | no | PARTIAL | no | N/A | linear |
| Diode | stub | yes | vendor CORRECT | no | no | vendor Newton |
| BJT/MOS | stub | no | UNDEFINED | no | no | UNDEFINED |

---

## Sign convention (both)

Passive sign / KCL: current leaving node is positive in that node’s KCL row. Voltage source branch current enters the “+” node equation as \(+I_b\) and “−” as \(-I_b\).
