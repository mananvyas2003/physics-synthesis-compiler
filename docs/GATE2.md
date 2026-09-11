# Gate 2 — KiCad emit (`synth generate`)

## Criteria

- `synth generate fixtures/seed/resistor_divider.json -o out/` succeeds
- Artifacts: `design.net`, `bom.csv`, `design.kicad_sch`, `design-snapshot.v1.json`
- Snapshot validates against frozen `schemas/design-snapshot.v1.json` shape
- Goldens `g06_generate_net`, `g07_generate_bom`, `g08_snapshot` pass

## KiCad open / ERC (manual)

1. Build `synth` (CMake or the local gcc command in the README).
2. Run:

```text
synth generate fixtures/seed/resistor_divider.json -o out/
```

3. Open `out/design.kicad_sch` in KiCad 8/9.
4. Open `out/design.net` via Schematic Editor → Import Netlist (or Tools → Update PCB from Schematic after associating the netlist export).
5. Run Electrical Rules Check on this fixture. Expected: no ERC errors for the two-resistor VIN/VOUT/GND divider (labels present; no unconnected pins on the placed Device:R symbols).

## Snapshot validate

`generate` calls `emit_snapshot_validate_file` before success. Structural checks:

- `schema == "design-snapshot.v1"`
- `topology`, `generator`, `components[]`, `nets[]` present

## Closed

Gate 2 implementation is complete in-tree when goldens g01–g08 are green on CI.
