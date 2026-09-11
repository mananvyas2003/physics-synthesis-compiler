# Gate 3 — Spec IR (offline)

## Criteria

- `schemas/spec.v1.json` frozen; loader in `spec/spec_load.c`
- `SpecProvider` file provider + validate-and-retry (max 3) in `spec/llm_provider.c`
- Corpus: `fixtures/prompts/001..030.txt` + `fixtures/specs/001..030.json`
- Golden `g09_spec_corpus`: 30/30 schema-valid
- Offline e2e: `synth generate --spec fixtures/specs/001.json -o out/` (no network)

## Notes

Hand-written specs resolve to `fixtures/seed/resistor_divider.json` for M1 emitters until composition selects richer seeds. HTTP provider is intentionally stubbed and not required for Gate 3.

## Closed

Gate 3 is complete in-tree when g09 is green and `--spec` generate succeeds offline.
