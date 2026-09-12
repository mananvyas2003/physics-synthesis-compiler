# Gate 5 — Scored binding

## Criteria

- Compiler uses `bind_score_passive` (not bare closest-only)
- Derating floors: searches prefer V ≥ 2× applied, P ≥ 2× dissipation
- Every BOM line includes `rationale` + `alternate_mpn`
- Cost within 15% of hand reference for **`resistor_divider` only** (`fixtures/reference/gate5_bom_cost.json`)
- Golden `g11_bind_rationale`

## Notes

Cost gate is intentionally scoped to `resistor_divider` so Gate 4 composed BOMs are not falsely rejected.

## Closed

Gate 5 is complete when g11 is green and divider generate emits rationale + alternate columns.
