# Gate 2 — KiCad emit (`synth generate`)

## Criteria

- `synth generate fixtures/seed/resistor_divider.json -o out/` succeeds
- Artifacts: `design.net`, `bom.csv`, `design.kicad_sch`, `design-snapshot.v1.json`
- Snapshot validates against frozen `schemas/design-snapshot.v1.json` shape
- Goldens `g06_generate_net`, `g07_generate_bom`, `g08_snapshot` pass
- Structural ERC golden `g16_structural_erc` (`divider_erc=1`)
- CI job `kicad-erc` runs `scripts/run_kicad_erc.sh` via `kicad-cli sch erc --severity-error --exit-code-violations`

## Automated ERC proof

```bash
cmake -S . -B build && cmake --build build
export SYNTH_FIXTURE_ROOT=$PWD SYNTH_BIN=$PWD/build/synth
./scripts/run_kicad_erc.sh out_erc
```

Requires KiCad 8+ (`kicad-cli` on PATH). Without KiCad, `g16` still proves pin/label/wire structure for the divider sheet.

## Manual open (optional)

1. Open `out/design.kicad_sch` in KiCad 8/9.
2. Run Schematic Editor → Inspect → Electrical Rules Checker.
3. Expect no error-level violations on the VIN/VOUT/GND divider.

## Snapshot validate

`generate` calls `emit_snapshot_validate_file` before success.

## Closed

Gate 2 harden complete when g06–g08 + g16 divider path are green and CI `kicad-erc` is clean on the divider artifact.
