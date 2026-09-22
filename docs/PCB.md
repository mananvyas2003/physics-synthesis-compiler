# PCB backend — actual behavior (Phase 18)

**Emitter:** `emit_kicad_pcb` → `design.kicad_pcb` on `synth generate`.

## Phase one (implemented)

| Feature | Status |
|---------|--------|
| Same Physical IR / `CompiledSchematic` as schematic | yes |
| Same net names on pads | yes |
| Deterministic grid footprint placement | yes |
| Board outline (`Edge.Cuts` rectangle) | yes |
| Inline SMD pads (library-independent geometry) | yes |
| Footprint library IDs from package | yes (ratsnest via net ids) |

## Not implemented (do not claim)

- Autoroute / power or signal routing
- Clearance-aware placement optimizer
- Real KiCad footprint library geometry (pads are simplified)
- Copper pours, vias, differential pairs
- “Optimized PCB”

## Invariant

PCB nets ⊆ schematic nets (by name). Placement is deterministic for a given component order.
