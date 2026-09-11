# Gate 5 — Scored binding

## Criteria

- Compiler uses `bind_score_passive` (not bare closest-only)
- Derating floors: MLCC/resistor searches prefer V ≥ 2× applied, P ≥ 2× dissipation
- Every BOM line includes `rationale` + `alternate_mpn`
- Cost within 15% of hand reference (`fixtures/reference/gate5_bom_cost.json`)
- Golden `g11_bind_rationale`

## Closed

Gate 5 is complete when g11 is green and generate emits BOM rows with rationale and alternate.
