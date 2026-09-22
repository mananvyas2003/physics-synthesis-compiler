# Industrial macro corpus (Phase 22)

## What exists

Reusable **block** JSON under `fixtures/blocks/*.json` with:

- DFM `ports` (voltage/current compatibility for composition)
- deterministic `expand` → `schematic-ir.v1` fragments (passives)

Catalog: `fixtures/macros/catalog.json` (14 macros).

## Families (catalog tags)

| Family | Examples |
|--------|----------|
| POWER | `battery_input`, `bat_ldo_3v3`, `ldo_5v`, `vin_12v`, `buck_5v_stub` |
| PROTECTION | `rev_polarity` (series stub, not a diode model claim) |
| ANALOG | `divider_10k`, `rc_lpf` |
| DIGITAL | `mcu_min_system`, `decouple_100n`, `i2c_pullups` |
| SENSING | `i2c_temp_sensor` |
| ACTUATION | `status_led` |
| INTERFACE | `usb_c_sink` |

## Scenarios

```text
synth generate --compose industrial_sensor -o out/
synth generate --compose power_tree_12v -o out/
synth generate --compose gate4 -o out/          # same as --compose-gate4
```

| Scenario | Blocks |
|----------|--------|
| `industrial_sensor` | battery → bat_ldo_3v3 → MCU + I2C sensor + LED + decouple |
| `power_tree_12v` | 12V → buck stub → LDO → MCU + LED |
| `gate4` / `usb_iot` | USB-C → LDO → MCU + I2C + LED |

## Honesty

- Expands are **topology placeholders** (R/C). `buck_5v_stub` is **not** a switching converter.
- `rev_polarity` is a series resistor stub until diode expand is bound in IR.
- Composition proves port compatibility + expand merge; Physics2 still owns equations.
- Do not claim full industrial product coverage from this corpus alone.
