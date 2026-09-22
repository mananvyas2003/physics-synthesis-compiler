# AC / DAE / Sparse — Contracts (implementation deferred)

**Prerequisite:** Physics2 diode Newton (done, g22) stable on live path.  
**Status:** linear AC **IMPLEMENTED** (`physics2_ac_*`, `physics2_context_step_ac`, g60–g63). Nonlinear small-signal AC and frequency sweep still deferred.

---

## 1. AC (complex MNA)

\[
A(j\omega)\, x(j\omega) = b(j\omega)
\]

| Device | Admittance / constraint |
|--------|-------------------------|
| R | Y=1/R |
| C | Y=jωC |
| L | Vp−Vn = jωL·I (branch) |
| V/I / controlled | same topology as DC, complex coeffs |

**ABI:** opaque `PhysicsAcSystem` with paired re/im — stamps call `physics2_ac_add` / `add_rhs` only.

**Nonlinear small-signal:** DC OP → J(x0) → complex AC — **NOT YET**.

---

## 2. DAE / transient

Current: fixed-step **Backward Euler** for C/L with **transactional commit**.

| Method | Support path |
|--------|----------------|
| BE | `physics2_context_step` / `run_steps` (g25, g30, g64–g68) |
| Trapezoidal | companion stamp change + state update |
| BDF2+ | history vectors in `PhysicsStatePool` |
| Adaptive Δt / LTE / reject | timestep controller outside stamps |

On Newton/solve failure with `timestep>0`: restore solution, C/L history, and `time` (no commit).

Need true DC OP for C (open) / L (short) before mixed DAE — **done**.

---

## 3. Sparse linear algebra

Stamps must only use `physics2_accumulator_add*`.  
Backends: dense (today) | sparse direct | iterative — selected at solve time without rewriting devices.

Needs: symbolic pattern from topology, numeric factorize, reuse across Newton iters.

---

## Implementation order (unchanged)

1. Residual norms + more nonlinear devices on Physics2 ABI  
2. Wire diode topologies through verify (optional)  
3. Complex accumulator + AC smoke tests  
4. Sparse backend  
5. Adaptive DAE  
