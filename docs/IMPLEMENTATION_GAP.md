# Implementation Gap Report (reconciled 2026-09-23)

**Companion:** [REPAIR_PLAN.md](REPAIR_PLAN.md). Claimed vs actual on the live `synth generate` path.

| Capability | Actual on generate | Evidence |
|------------|--------------------|----------|
| Physical IR → Physics2 | R, C, L, diode/LED, BJT, NMOS, PMOS, switch, connector, op-amp, LDO, battery + per-rail sources | g21, g26, g82 |
| Component loss | `PHYSICS_COMPONENT_LOSS` fails verify | g26 |
| Rails | IR `rails` explicit; name-derived; VIN/VCC/VBUS 5 V labelled defaulted | g81, g82 |
| Measurement | IR `measure` node + min/max; name heuristic only as labelled fallback | g82 |
| Newton | step-limited, residual-gated (true ‖F‖∞); backtracking on ‖F‖∞ | g82 |
| Ratings | V all pins; I/P for R, D, L, battery, switch, BJT, NMOS/PMOS, LDO; op-amp I_out + rail dissipation | g82, g83 |
| Tolerance corners | R at DC; R/C/L at AC, exhaustive ≤ 2^10 | g82 |
| AC | one rail; linear + nonlinear linearized at the DC OP | g82, g83 |
| Transient | IR `transient {stop_s,step_s}`: power-on step, Backward Euler | g82 |
| Power balance | when all device currents are known (op-amp stamp still returns I through VEE) | g82 |
| PMOS / switch / 2-pin connector | lowered (connector = 1 GΩ open); 3-pin connector fail closed | g82 |
| Device model params | `DEF_*` defaults; IR `parts[].model` overrides per MPN | g82 |
| DFM | 8 rules; typed footprints (R/C/L/LED/SOT) | g69–g71, g83 |
| KiCad schematic | multi-pin lib symbols | g31 |
| Generate emit | `.gen` staging; rename on success; drop on fail | g82 |
| PCB, Gemini | removed | — |
| Prompt frontend | offline C NLP with synthesis; every emitted IR must verify | g77, g83 |
| Test log | `audit_build/test-run.v1.json` (all cases) | golden_runner |
| Generate telemetry | build-manifest: stage, DB, bindings, DFM | cmd_generate.c |
