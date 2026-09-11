# Gate 6 — Verification gate

## Criteria

- After bind, DC MNA on the bound netlist via Physics2
- Assert branch voltages/dissipation against part ratings
- Emit `out/verification.v1.json` on every generate attempt
- `generate` returns non-zero if verification fails (refuse emit of net/BOM/sch on fail)
- Corner spread + simple AC magnitude stub recorded in the report
- Golden `g12_verify_report`

## Deliberate refuse proof

Lower a seed part `v_rating` below the ~5 V divider drop and re-run generate: expect non-zero exit and `verification.v1.json` with `passed: false` before netlist emit.

## Closed

Gate 6 is complete when g12 is green and successful generates always include a passing verification report.
