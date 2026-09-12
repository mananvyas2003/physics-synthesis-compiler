# Gate 3 — Spec IR (offline)

## Criteria

- `schemas/spec.v1.json` frozen; loader in `spec/spec_load.c`
- `SpecProvider` file provider + validate-and-retry (max 3) in `spec/llm_provider.c`
- Corpus: `fixtures/prompts/001..030.txt` + `fixtures/specs/001..030.json`
- Golden `g09_spec_corpus`: 30/30 schema-valid
- Offline e2e: `synth generate --spec fixtures/specs/001.json -o out/` (no network)

## Notes

Hand-written specs still resolve to `fixtures/seed/resistor_divider.json` for the offline Gate 3 path. Richer topologies come from `--compose-gate4` / `--prompt` (Gate 4 / Gate 7). HTTP provider remains stubbed.

## Closed

Gate 3 is complete when g09 is green and `--spec` generate succeeds offline on CI.
