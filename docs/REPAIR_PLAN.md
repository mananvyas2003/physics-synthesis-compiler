# Repair Plan — Physics Synthesis Compiler

**Date:** 2026-09-22  
**Rule:** File/function-level. No claimed support without tests.  
**User local change noted:** `compiler.c` KiCad multi-pin / symbol_prefix split (keep; incomplete for pin_count≠2 library symbols).

---

## 0. Actual generate call graph (traced 2026-09-22, post Phase 2)

```
main → cmd_generate
  ├─ schematic_provider_from_prompt / --spec / --compose / design.json
  │     offline: offline_from_prompt → fixtures/schematics/NNN.json  (NOT NLP)
  │     live:    gemini_schematic_from_prompt
  ├─ seed_load_topology_json_ex  (SQLite Topology* + optional IR parts[])
  ├─ compiler_compile_from_design
  │     bind_score_passive(..., applied_v=5.0, ...)   ← still Phase 5 debt
  │     → CompiledSchematic  (KiCad + BOM)
  ├─ verify_bound_schematic
  │     LED-only (no C/L) → analytical
  │     transistor/IC → fail-closed
  │     else: schematic → PhysDesign → lower → Physics2 DC (R/C/L/diode+Vsrc)
  ├─ mfg_dfm_check_schematic → vendor_bridge → vendor_next dfm (~3 rules)
  └─ emit net / bom / kicad_sch / snapshot
```

**Not on generate path:** vendor `mna_solve`, `part_provider`, `e_series`, SQLite `FabRules`.  
**On generate path now:** `compiler_schematic_to_phys_design`, `compiler_lower_to_physics2`.

---

## 1. Bug map (exact locations)

### 1.1 903 capacitors disappear — **FIXED (Phase 2)**

Was: `verify_report.c` `continue` omitting C.  
Now: every bound R/C/L/diode enters PhysDesign; integrity check fails on instruction_count loss; DC C stamp returns success (open).  
**Still needed:** dedicated 903 golden (Phase 3).

### 1.2 SENSOR_VDD reported as supply — **PARTIAL**

Was: `is_sense` ∈ {VOUT, 3V3}.  
Now: looks up `SENSOR_VDD` / `ADC_SENSE` / `VOUT` by name; still falls back to VIN if none; JSON key still `"vout"`.  
**Phase 4:** fail if requested node missing; never silent VIN.

### 1.3 Hardcoded rating / bias voltage — **OPEN (Phase 5)**

| Site | Code |
|------|------|
| `compiler.c` ~228 | `double applied_v = 5.0;` fed to `bind_score_passive` + bind-time dissip |
| `verify_report.c` | DC ratings use solved ΔV (good); bind path still fake |
| LED analytical | `vf` fallback 2.0 when catalogue Vf missing |

### 1.4 Transistor / IC “auto-pass”

**fail-closed** with `analysis=unsupported`. Do not reintroduce structural pass.

### 1.5 Physics2 lowering bypassed — **FIXED (Phase 2)**

Verify builds PhysDesign → `compiler_lower_to_physics2`. No second ad-hoc stamp loop.

### 1.6 Offline “NLP” is fixture lookup — **FIXED (Phase 19)**

Deterministic C NLP in `nlp/nlp.c`; `synth parse --prompt-text`. Fixture map only with `--offline-prompt`.

### 1.7 Silent pin-contract fallback — **OPEN (Phase 4)**

`compiler.c` still invents `"1","2"` when no pin contract.

### 1.8 User `compiler.c` KiCad edits (local, keep)

- Separates `ref_prefix` vs `symbol_prefix` — correct
- Multi-pin grid emit from `pins[]`/`nodes[]` — good
- `write_twoterm_library_symbol` still **returns early if pin_count ≠ 2** → Q/LDO/OpAmp lack `lib_symbols` — Phase 17

---

## 2. Source-of-truth layers (target)

| Layer | Owner (target) | Today |
|-------|----------------|-------|
| Spec IR | NLP / Gemini / offline parse | schematic-ir / SpecV1 |
| Topology IR | typed graph | SQLite Topology* + IR JSON |
| Physical IR | bound device + model params | `CompiledSchematic` (MPN mixed in) |
| Physics2 | executable equations | partial; verify bypass |
| Numerical | Physics2 runtime | Physics2 (+ vendor oracle) |
| Verification | node/quantity measurements | `verify_report` heuristics |
| Manufacturing | DFM rule engine | 3 vendor rules |
| Output | KiCad/BOM serializers | emit / `compiler_write_kicad_sch` |

Invariant: same Physical IR → sim / BOM / sch / PCB.

---

## 3. Phase plan (files / functions)

### PHASE 1 — Docs vs code (this file + audits)

| Deliverable | Action |
|-------------|--------|
| `docs/REPAIR_PLAN.md` | This document |
| `docs/architecture/ARCHITECTURE_AUDIT.md` | Patch §2/§4 for fail-closed IC, capacitor drop site |
| `docs/math/MNA_CONTRACT.md` | Note verify omits C; lowering unused |
| `docs/IMPLEMENTATION_GAP.md` | Short gap table (Phase 1 artifact) |

No product-feature code in Phase 1.

### PHASE 2 — Physical IR → Physics2 authoritative

| Change | File / symbol |
|--------|----------------|
| Add `COMPILER_PHYS_DIODE` (+ optional later kinds) | `compiler.h` |
| `compiler_schematic_to_phys_design(CompiledSchematic*, CompilerPhysDesign*, …)` | `compiler.c` — map every bindable R/C/L/diode; inject ideal V from power net; **fail closed** on unsupported |
| Extend element params for diode Is/n/Vt | `CompilerPhysElement` or side params |
| DC open capacitor: stamp no-op **success** when `timestep==0` (or `dc` flag) | `physics2_interpreter.c` `capacitor_stamp` |
| Allow DC context init (`timestep==0`) | `physics2_context_init` |
| DC inductor: short (ideal V=0 branch preferred; temporary large-G only if documented `ponytail:`) | `inductor_stamp` |
| Verify: build PhysDesign → `compiler_lower_to_physics2` → step → measure | `verify/verify_report.c` |
| Delete silent `continue` for unknown types | same — fail closed |
| Keep analytical LED/RC/RL as **oracle tests only**, not generate path | move or gate |
| Golden: lowering preserves C/L opcodes in instruction stream | `tests/golden_*` |

**Exit criteria:** generate/verify Physics2 `instruction_count` ≥ bound passive+diode count (+ Vsrc); no device dropped without `PHYSICS_UNSUPPORTED`.

### PHASE 3 — Component-loss regression (903)

| Artifact | Content |
|----------|---------|
| Fixture | `fixtures/schematics/903.json` (or seed) sensor rail + LED |
| Golden | IR ⊇ C1,C2; bound ⊇ C1,C2; PhysDesign ⊇ C; Physics2 opcodes ⊇ CAPACITOR; DC C open; V(SENSOR_VDD)≈3.0 |
| Structured log | `test-run.v1.json` sections INPUT/DB/TOPOLOGY/PHYSICS/MNA/MEASUREMENTS/DFM |

Depends on Phase 2 + Phase 4 measurement name fix.

### PHASE 4 — Node-aware measurement + topology fail-closed

- Measurement API: `node` + `quantity` → `solution[node_id]`
- Expand / replace `is_sense`; never fall back to VIN silently when a named net exists
- Remove `req_pins` silent `"1","2"` fallback
- Multi-pin nodes already partially on `CompiledComponent.nodes[]`

### PHASE 5 — Remove hardcoded rating voltage

- Bind after coarse estimate or two-pass: bind → solve → re-check ratings with solved V/I/P
- Delete `applied_v = 5.0` as universal stress

### PHASES 6–14 — Device / Newton / AC / transient

Per super-prompt §8–28. Physics2 remains sole live runtime; vendor MNA = oracle.  
Do not advance while Phase 2–3 invariants fail.

### PHASES 15–18 — Binding loop, DFM rules, KiCad round-trip, PCB

- Finish multi-pin KiCad symbols (user WIP)
- Topology fingerprint round-trip
- PCB nets = schematic nets

### PHASES 19–21 — Offline C NLP, optional ONNX, Gemini live/replay

- Real `synth parse --prompt-text`
- Fixture map remains corpus only, not default NLP

### PHASE 22 — Industrial macros

**DONE** — `fixtures/macros/catalog.json`, new blocks, `--compose <scenario>`, g80, `docs/MACROS.md`.

---

## 4. PHASE 1 / 2 immediate work order

1. ~~Write this plan + `IMPLEMENTATION_GAP.md`~~ **DONE**
2. ~~Patch audit/contract docs to match code~~ **DONE** (reconciled again after user KiCad WIP)
3. ~~Implement PhysDesign from schematic + DC-open C + verify via lower~~ **DONE**
4. ~~Build + run existing goldens~~ **DONE** (`golden_runner failures=0`; g18 shows C in Physics2 stream)
5. ~~Delete dead `verify_rc_analytical`~~ **DONE** (was unused after Phase 2)
6. **Stop** — next was Phase 3–5; status below.

### Phase 3–5 status (2026-09-22)

| Phase | Status |
|-------|--------|
| 3 — 903 golden | **DONE** — `g26_903_sensor_power`, fixture `fixtures/seed/sensor_903.json` |
| 4 — named measure + pin fail-closed | **DONE** — no VIN fallback; `measured_node`/`measured_v`; silent `1`/`2` pins removed |
| 5 — `applied_v=5.0` | **DONE** — `infer_supply_v` from topology nets; 0 → skip fake derating |
| 6 — R/C/L/V/I | **DONE** — L branch DC short+BE; g27–g30 isource/C-open/L-short/RL-BE |
| 7 — controlled sources | **DONE** — g32–g35 hand oracles; g36 PhysDesign lower; node-before-branch fix |
| 8 — global Newton | **DONE** — report/damping/limits; g37–g40 |
| 9 — diode | **DONE** — junction soft-limit; reverse/C-BE/KCL; g38 harsh V=10; g41–g43 |
| 10 — BJT | **DONE** — Ebers-Moll stamp+Jacobian; Newton; g44–g47 cutoff/active/sat/KCL |
| 11 — MOSFET | **DONE** — Level-1 Shichman-Hodges NMOS/PMOS; Newton; g48–g51 cutoff/linear/sat/KCL |
| 12 — switch/opamp/LDO/battery | **DONE** — Ron/Roff; behavioral op-amp VCVS; lumped LDO; Thevenin battery; g52–g59 |
| 13 — true AC | **DONE** — opaque paired re/im MNA; R/C/L/V/I (+); g60–g63 RC/RL/divider |
| 14 — robust transient | **DONE** — BE transactional snapshot/restore; `run_steps`; g64–g68 RC/RL/RLC/diode/reject |
| 15 — two-pass bind | **DONE** — `rebind_with_solved_stress` after compile |
| 16 — DFM | **DONE** — 8 rules; honest `rules_checked`/`profile_fields_used`; g69–g71; `docs/DFM.md` |
| 17 — multi-pin KiCad | **DONE** — `write_library_symbol` + g31 |
| 18 — PCB | **DONE** — phase-one `emit_kicad_pcb` (place/outline/nets); g72; `docs/PCB.md` |
| 19 — offline NLP | **DONE** — C lexer+patterns; `synth parse`; corpus `--offline-prompt` only; g73–g75; `docs/NLP.md` |
| 20 — optional ONNX | **DONE** — `nlp_runtime` stub fail-closed; `--neural`; 30 free-form prompts g76–g77; no fake BiLSTM |
| 21 — Gemini live/replay | **DONE** — `SYNTH_GEMINI_REPLAY` cassette; g78 replay; g79 live=unavailable gate; `docs/GEMINI.md` |
| 22 — industrial macros | **DONE** — catalog+blocks; `industrial_sensor`/`power_tree_12v`; g80; `docs/MACROS.md` |

User local KiCad multi-pin / `symbol_prefix` split: **kept** and completed for Q/LDO/OpAmp.


---

## 5. Explicit non-goals for Phase 1–2

- PCB backend  
- BiLSTM/ONNX  
- Gemini suite expansion  
- Fake BJT/MOS  
- Claiming AC/complex MNA  
- Special-case “if 903 then …”

---

## 6. Definition of done (architecture) — tracked

See super-prompt §70. Phase 2 closes items **1** (partial: no silent drop on R/C/L/diode path) and **2** (Physical IR drives sim). Items **3–4** need Phases 4–5.
