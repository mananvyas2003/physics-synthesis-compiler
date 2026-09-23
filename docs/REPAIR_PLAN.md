# Repair Plan — Physics Synthesis Compiler

**Reconciled:** 2026-09-23 against the working tree (not against earlier claims).
**Rule:** a row says "done" only if a golden in `tests/golden_cases.c` proves it.
**Companion:** [IMPLEMENTATION_GAP.md](IMPLEMENTATION_GAP.md) (claimed vs actual).

---

## 0. Scope decisions (owner, 2026-09-23)

| Decision | Effect |
|----------|--------|
| Gemini removed | Prompt text always goes through the offline C NLP (`nlp/nlp.c`). |
| PCB backend removed | `synth generate` emits no `.kicad_pcb`. |
| Compiler-level controlled sources removed | Physics2 interpreter primitives (g32–g35) remain. |
| Docker / Railway removed | owner change. |

---

## 1. Actual generate call graph

```
main → cmd_generate (cli/cmd_generate.c)
  ├─ --prompt / --prompt-text → nlp_text_to_schematic_ir (nlp/nlp.c)
  │     patterns + closed-form value synthesis → schematic-ir.v1 (+ measure,
  │     provenance, unmodeled). --offline-prompt → corpus fixture map only.
  ├─ --spec / --compose / design.json
  └─ cmd_generate_design
       ├─ schematic_ir_validate_file (rails / measure / power source checked)
       ├─ seed_load_topology_json_ex → SQLite work DB in out_dir/.gen
       ├─ compiler_compile_from_design (compiler.c)
       │     IR part_type → CompiledComponent.kind; IR rails / measure / model /
       │     transient → schematic
       │     bind_score_passive (first pass) → rebind_with_solved_stress (logged)
       ├─ verify_bound_schematic (verify/verify_report.c)  — Physics2 only
       │     compiler_schematic_to_phys_design: R C L D BJT NMOS/PMOS switch
       │       connector op-amp LDO battery + one ideal source per rail
       │     → Newton (residual-gated + ‖F‖∞ line search) → measure
       │     → ratings → R DC corners → AC (linearized at OP) + R/C/L AC corners
       │     → IR transient (power-on BE) → power balance
       ├─ mfg_dfm_check_schematic → vendor_bridge (real model kinds) → 8 rules
       ├─ emit net/BOM/sch/snap/erc into .gen; rename into out_dir on success
       └─ build-manifest.v1.json: stage, [DB], [BIND], [DFM]; failure drops emit
```

Not on the generate path: vendor `mna_solve` (oracle).

---

## 2. Bug map (super-prompt §71.4)

| Symptom | Status | Proof |
|---------|--------|-------|
| 903 capacitors dropped | fixed; verify fails on `PHYSICS_COMPONENT_LOSS` | g26 |
| SENSOR_VDD reported as supply | fixed | g26 |
| Measurement picked by name list | IR `measure` drives it (`measure_source: spec`); name list only as labelled fallback | g82 |
| Hardcoded 5 V stress | gone; IR `rails` overrides names; VIN/VCC/VBUS default 5 V labelled `defaulted` | g81, g82 |
| Transistor / IC auto-pass | replaced by real lowering (BJT, NMOS/PMOS, op-amp, LDO, battery, switch, connector); others fail closed | g21, g82 |
| LED analytical bypass | deleted; LED through Physics2 | g17, g26, g82 |
| Offline NLP = fixture lookup | real patterns + synthesis; every emitted IR must verify | g77, g83 |

---

## 3. Done (each with a test)

| Item | Files | Test |
|------|-------|------|
| Build after partial revert; Gemini removed | many | full build |
| Rail sources per rail, fail closed without supply | compiler.c | g81 |
| Exact junction law (diode/BJT), LED Is from Vf @ 20 mA | physics2_interpreter.c, compiler.c | g26 LED 1.919 V |
| LED+R through Physics2 (analytical path deleted) | verify_report.c | g17 |
| IR `measure {node,min,max}` + limit violations | compiler.c, schematic_load.c, verify_report.c | g82 `measure_limit_rejects` |
| IR `rails {"VIN": 12}` | same | g82 `ir_rails_measure_ok` |
| Lowering: BJT (Ebers-Moll), NMOS (Level-1), op-amp (finite gain), LDO (behavioral), battery (Thevenin) | compiler.c | g82 bisection oracles, g21 |
| LDO current-limit Jacobian regularized (was singular unloaded) | physics2_interpreter.c | g21 |
| AC fails closed for devices without a small-signal stamp (was: silently open) | physics2_interpreter.c | — |
| Newton accepts only when ‖A(x)x−b(x)‖∞ ≤ abs + rel·scale | physics2_interpreter.c | g82 residual < 1e-9 |
| Ratings: current + power for R/D/L/battery; voltage across every pin pair | verify_report.c | g82, g83 prompt 4 |
| Resistor tolerance corners, exhaustive to 2^10, per-corner values reported | verify_report.c | g82 closed-form corners |
| AC transfer on generate (linear + linearized nonlinear, single rail), 10 Hz–1 MHz | verify_report.c | g82, g83 prompt 5 |
| AC R/C/L corners, exhaustive to 2^10 | verify_report.c | g82 |
| Transient on generate when IR has `transient` | verify_report.c | g82 |
| IR `parts[].model` overrides `DEF_*` | compiler.c | g82 `model_override_ok` |
| 2-pin connector = 1 GΩ open; 3-pin fail closed | compiler.c | g82 `connector_ok` |
| LDO I_in = I_out (stamp + ratings); op-amp I_out + rail dissipation | verify_report.c | g82, g21 |
| Residual line search on ‖F‖∞ (α = 1 … 1/64) | physics2_interpreter.c | g82 `newton.damping` |
| Emit to `.gen`, rename on success, drop on fail | cmd_generate.c | g82 `generate_transactional_ok` |
| Power balance report | verify_report.c | g82 (~1e-17 W) |
| DFM bridge used a resistor model for every non-R/C/D part; ≤3 pins | vendor_bridge.c | g83 prompt 6 (battery) |
| Seed rejected valid `battery` parts | seed/seed_topology.c | g77 p08 |
| NLP value synthesis: divider ratio, RC cutoff, LED current, load R + package, LDO, I2C pull-ups, battery reverse polarity, decoupling | nlp/nlp.c | g83 |
| NLP corpus: every emitted IR must validate, bind and verify | tests | g77 (14 verify / 16 refuse, per-prompt) |
| Structured test log | tests/golden_runner.c | `audit_build/test-run.v1.json` |
| Generate telemetry: DB, bindings, DFM, stage | cmd_generate.c | manifest |
| Failure drops emitted design files (staging + published) | cmd_generate.c | g82 |

---

## 4. Open work (honest ceilings)

1. **Catalogue has no model columns** — per-part params come from IR `parts[].model`; `DEF_*` remain the fallback.
2. **Op-amp stamp** still returns output current through VEE; ratings split VCC/VEE, power balance stays off for op-amps.
3. **AC / transient** need an IR measure node (and `transient` for BE). No arbitrary stimulus sources.
4. **CMake** is not installed on this machine (`where cmake` empty). Goldens: gcc -O2, 79/79.

Closed this pass: IR model override; 2-pin connector; LDO I_in; op-amp rail ratings; AC linearization + R/C/L AC corners; IR transient; residual line search; `.gen` publish.

---

## 5. NLP frontend — measured

`synth generate --prompt-text`, super-prompt §62 (g83):

| Prompt | Result |
|--------|--------|
| 3.3V rail 250mA, 100nF + 10uF decoupling | 2 caps on 3V3 in Physics2; 250 mA recorded as `unmodeled` |
| ADC divider 5V → roughly 1V | R1 = E24(40k) = 39k, V(ADC_SENSE) = 1.0204 V, limits ±5 % |
| 4.7k pull-up for I2C at 3.3V | R_SDA, R_SCL to 3V3 |
| MOSFET switch 12V load from 3.3V | load 120 Ω (100 mA defaulted), 0805 at 2.4 W, V(DRAIN) matches triode KCL |
| Low-pass around 1 kHz | C = E24(15.9 nF) = 16 nF, |H(1 kHz)| = 0.705 |
| Battery input with reverse-polarity protection | 3.7 V defaulted, Schottky, V(VSYS) = 3.7 V |
| LED indicator from the regulated rail | refused: rail voltage not stated |
| 12 V to 5 V regulator | LDO, V(5V) = 5.0 V |

Corpus `fixtures/nlp_freeform/p01–p30` (g77): 14 verify end to end, 16 refused with a clarifying question, 0 emitted-but-invalid.
