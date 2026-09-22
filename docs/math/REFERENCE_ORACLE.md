# Reference Oracle Matrix

External simulators are **development oracles only**, not runtime dependencies.

## Agreement policy

| Circuit class | Our formulation | Reference | Expected agreement | Known differences |
|---------------|-----------------|-----------|--------------------|-------------------|
| Linear DC R network | Physics2 dense MNA | Hand algebra / ngspice `.op` | \|ΔV\| < 1e-9 relative | Ground index convention |
| Divider + V source | Physics2 | Hand / ngspice | exact for ideal R | — |
| Shockley diode + R | Physics2 Newton companion; vendor mna oracle | Hand (Lambert-W / iterative); ngspice D model | \|ΔV\| < 1e-3 V (g22) | Exp clamp ±40 nVt; Is/n/Vt param map |
| Floating / conflict V | Physics2 dense GE pivot fail | — | solve_failed (g23/g24) | Singular matrix |
| BE capacitor step | Physics2 companion G=C/dt | Hand V=Vin/(1+RC/dt) | 1e-6 (g25) | Vprev=0 first step |
| BE RC step | Physics2 BE | Hand discrete; ngspice trap/gear may differ | match BE closed form | Method ≠ SPICE default trap |
| Controlled sources | Physics2 stamps | Hand MNA; ngspice E/F/G/H | \|Δ\| < 1e-9 | Terminal order must match |
| AC RLC | **NOT IMPLEMENTED** | ngspice `.ac` | — | Need complex MNA |
| BJT/MOS OP | **NOT IMPLEMENTED** | Xyce/ngspice | — | Need residual ABI |
| Transient adaptive | **NOT IMPLEMENTED** | Xyce/ngspice | — | Fixed BE only |

## Hand references (built-in)

| Test | Closed form |
|------|-------------|
| g02 | equal R → Vout = Vin/2 |
| g03 | parallel divider algebra |
| g22 / mna_test diode | Vd ≈ 0.574147 V @ Is=1pA, n=1, T=300K, R=1k, Vs=5 |
| g23 | floating R island → solve fails |
| g24 | dual conflicting V → solve fails |
| g25 | BE RC: Vmid=2.5 V @ R=1k, C=1µF, dt=1ms, Vin=5 |

## How to use ngspice/Xyce later

1. Emit netlist from same topology (not KiCad as physics source).
2. Compare node voltages / branch currents at same OP.
3. Document any device-model parameter mapping in this file.
4. Never call external sim from `synth generate` runtime.
