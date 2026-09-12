# Gate 6 — Verification gate

## Criteria

- After bind, DC MNA on the bound netlist via Physics2
- Assert branch voltages/dissipation against part ratings
- Emit `out/verification.v1.json` on every generate attempt
- `generate` returns non-zero if verification fails (**refuse** net/BOM/sch emit on fail)
- Corner spread + simple AC magnitude stub recorded in the report
- Golden `g12_verify_report`
- Composed Gate 4 designs also produce a verification report (VBUS/3V3 sense path)

## Deliberate refuse proof

1. Copy `fixtures/seed/resistor_divider.json` to a temp file.
2. Set a part `v_rating` below the ~5 V divider drop (e.g. `1.0`).
3. `synth generate <temp.json> -o out_bad/`
4. Expect non-zero exit; `verification.v1.json` with `passed: false`; no successful netlist emit path.

## Closed

Gate 6 is complete when g12 is green and successful generates always include a passing verification report.
