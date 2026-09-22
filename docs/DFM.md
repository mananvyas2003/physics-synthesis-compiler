# DFM — actual behavior (Phase 16)

**Engine:** `vendor_next` DFM via `vendor_dfm_check_schematic` → `mfg_dfm_check_schematic`.  
**Report:** `mfg-dfm.v1.json`

## Rules that run (`rules_checked`)

| Rule | What it checks |
|------|----------------|
| `floating_pin` | pin with net_id == -1 |
| `missing_footprint` | empty footprint string |
| `missing_package` | empty package code |
| `unsupported_package` | package not in allow-list 0402/0603/0805/1206/SOT-23/TO-92/SOD-123/SMA/SMB |
| `component_height` | height_mm vs `max_component_height_mm` |
| `profile_consistency` | layer/via/drill/annular/trace/clearance/edge geometry |
| `package_vs_profile` | known package body/pad vs min_clearance / min_trace_width |
| `package_current` | resistor √(Pmax/R) vs crude package ampacity |

## Profile fields

**Used:** `layer_count`, `min_trace_width_mm`, `min_clearance_mm`, `min_via_diameter_mm`, `min_drill_mm`, `min_annular_ring_mm`, `board_edge_clearance_mm`, `max_component_height_mm`.

**Stored but unused:** `copper_weight_oz` (noted in JSON; no current-density/trace IPC rule yet).

## Not implemented (do not claim)

- PCB routing DFM (real clearance/trace from layout)
- Board outline / edge copper from geometry
- Composition-compat DFM (see `cmd_dfm` suite — separate electrical composition checks)

## Telemetry keys in report

`rules_checked`, `rules_checked_count`, `profile_fields_used`, `missing_metadata`, `passed`, `error_count`, `rejected`, `errors[]`.
