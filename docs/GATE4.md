# Gate 4 — Block composition

## Criteria

- Catalog blocks as data under `fixtures/blocks/` (each with ports + `expand` recipe)
- Composer validates DFM composition (power fan-out + I2C)
- Scenario: USB-C → LDO → MCU → I2C temp → status LED
- Golden `g10_compose_gate4`, `g15_compose_expand_sch`, `g16_structural_erc` (`compose_topology=1`, `compose_erc=1`)
- E2E: `synth generate --compose-gate4 -o out/` writes `composed_schematic.json` then bind/verify/emit
- Topology name must be `usb_c_stm32_composed` with **5** passives (not the deprecated divider stand-in)

## Deprecated

`fixtures/seed/usb_c_stm32.json` is a **deprecated** stand-in. Do not use it for Gate 4 exit proofs.

## KiCad ERC

Composed sheets use global labels + pin stub wires (no false vertical shorts between unrelated parts). Covered by `g16` and CI `kicad-erc` on `out_erc/compose/design.kicad_sch`.

## Closed

Gate 4 harden complete when compose-expand generate succeeds and g15/g16 compose checks are green.
