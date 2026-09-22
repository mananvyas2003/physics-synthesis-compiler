# AC / DAE / Sparse — Contracts (implementation deferred)

**Prerequisite:** Physics2 diode Newton (done, g22) stable on live path.  
**Status:** contracts only — **NOT IMPLEMENTED** as analysis modes.

---

## 1. AC (complex MNA)

\[
A(j\omega)\, x(j\omega) = b(j\omega)
\]

| Device | Admittance / constraint |
|--------|-------------------------|
| R | Y=1/R |
| C | Y=jωC |
| L | Y=1/(jωL) |
| V/I / controlled | same topology as DC, complex coeffs |

**ABI change:** accumulator `double` → `double complex` (or paired re/im) behind opaque API so stamps stay storage-agnostic.

**Nonlinear small-signal:** DC OP → J(x0) → complex AC (separate from large-signal transient).

---

## 2. DAE / transient

Current: fixed-step **Backward Euler** for C/L only.

| Method | Support path |
|--------|----------------|
| BE | already |
| Trapezoidal | companion stamp change + state update |
| BDF2+ | history vectors in `PhysicsStatePool` |
| Adaptive Δt / LTE / reject | timestep controller outside stamps |

Need true DC OP for C (open) / L (short) before mixed DAE.

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
