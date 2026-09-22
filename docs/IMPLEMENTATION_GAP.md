# Implementation Gap Report — Phase 1 (reconciled 2026-09-22)

**Companion:** [REPAIR_PLAN.md](REPAIR_PLAN.md)

Claimed vs actual for the live `synth generate` path after Phase 2 wiring.

| Capability | Claimed | Actual on generate | Evidence | Phase to close |
|------------|---------|-------------------|----------|----------------|
| Physical IR → Physics2 | Lowering API | **Live** via `compiler_schematic_to_phys_design` → `compiler_lower_to_physics2` | `verify_report.c` | **2 done** |
| Capacitor in Physics2 DC | open at DC | **Present**; stamp success, open (no conductance) | `capacitor_stamp` + verify integrity check | **2 done** |
| Inductor in Physics2 DC | short | **Present**; approx `G=1e9` short (`ponytail:`) | `inductor_stamp` | 6 (branch I later) |
| Node measurement by name | verification.v1 | `measured_node` / `measured_v`; no VIN fallback; `vout` only if sense found | **4 done** |
| Rating from solved stress | bind derating | Bind uses `infer_supply_v`; verify uses solved ΔV | **5 done** (full two-pass = 15) |
| R stamp | yes | yes | g01–g03 | done |
| C BE transient | yes | g25; not full generate AC/transient | g25 | 6/14 |
| Controlled sources | opcodes + stamps | not from schematic | interpreter only | 7 |
| Diode Newton | Physics2 | Mixed nets → Physics2; LED-only still analytical | verify | 8–9 |
| BJT / MOSFET | opcodes | fail-closed verify; no equations | verify | 10–11 |
| Switch / opamp / LDO / battery | KiCad stubs | no Physics2 models | compiler emit | 12 |
| Complex AC | docs proposal | **NOT IMPLEMENTED** | MNA_AUDIT | 13 |
| Transient DAE | BE C/L API | limited; not generate path | interpreter | 14 |
| Binding ↔ sim loop | intended | bind once with fake V | compiler | 15 |
| DFM full profile | JSON fields | ~3 rules active | vendor dfm | 16 |
| KiCad topology fidelity | sch emit | 2-pin symbols OK; multi-pin `pin_count≠2` still skips `lib_symbols` | `compiler.c` user WIP | 17 |
| PCB | intended | **absent** | MODULE_MAP | 18 |
| Offline NLP | “prompt” | filename → fixture | `schematic_load.c` | 19 |
| Gemini | live optional | curl → IR | gemini_schematic | 21 |
| IC/transistor auto-pass | old audit | **fail-closed** | verify | keep |
| 903 golden | required | **g26** — C×2 in Physics2; V(SENSOR_VDD)=3.0 | **3 done** |

## Parallel engines

| Engine | Role today | Target |
|--------|------------|--------|
| Physics2 | **Authoritative** on verify/generate | keep |
| vendor_next MNA | Tests / diode oracle | Oracle only |

## Remaining highest-priority edits

1. Phase 3: 903 golden (IR ⊇ C; Physics2 ⊇ CAPACITOR; V(SENSOR_VDD)≈3.0 + structured log)
2. Phase 4: no silent VIN fallback; measurements by node name; fail-closed pin contracts
3. Phase 5: delete universal `applied_v = 5.0` bind stress
