# Gate 7 — Grammar-constrained free-form schematics

## Criteria

- Frozen `schemas/schematic-ir.v1.json` (elevated seed shape)
- Offline `synth generate --prompt fixtures/prompts/NNN.txt --offline-prompt -o out/` maps to `fixtures/schematics/NNN.json`
- Live: set `GEMINI_API_KEY` then `synth generate --prompt-text "..."` / `--prompt` (see [GEMINI_PROMPT.md](GEMINI_PROMPT.md))
- Invalid prompt/IR → clarifying question; no emit
- `--compose-gate4` expands catalog block `expand` recipes into a real multi-passive schematic (not the divider stand-in)
- Compiler binds per-component `target_value` / `package`
- Goldens `g13_schematic_corpus`, `g14_prompt_generate`, `g15_compose_expand_sch`

## Usage

```text
synth generate --prompt fixtures/prompts/001.txt -o out/
synth generate --compose-gate4 -o out/
```

`out/composed_schematic.json` is written for Gate 4 expand before bind/verify/emit.

## Non-goals

Raw LLM → `.kicad_sch`, autorouter, IC symbol libraries, unconstrained SPICE.

## Closed

Gate 7 is complete when g13–g15 are green and prompt/compose-expand generate succeed offline.
