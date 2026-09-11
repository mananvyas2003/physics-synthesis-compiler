# Gate 4 — Block composition

## Criteria

- Catalog blocks as data under `fixtures/blocks/`
- Composer `compose/compose.c` validates DFM composition with power fan-out + I2C links
- Scenario: USB-C sink → LDO → MCU min system → I2C temp → status LED
- Golden `g10_compose_gate4`: `blocks=5`, `composition_ok=1`
- E2E: `synth generate --compose-gate4 -o out/` emits ERC-path schematic from `fixtures/seed/usb_c_stm32.json`

## Blocks

1. `usb_c_sink`
2. `ldo_5v`
3. `mcu_min_system`
4. `i2c_temp_sensor`
5. `status_led`

## Closed

Gate 4 is complete when composition validates and generate from the Gate 4 seed succeeds with openable KiCad artifacts (same ERC path as Gate 2 for the passive stand-in).
