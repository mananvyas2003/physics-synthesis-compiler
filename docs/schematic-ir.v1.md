# schematic-ir.v1

Frozen grammar for free-form (Gate 7) schematics. Same shape as seed topology JSON under `fixtures/seed/`.

## Required fields

| Field | Meaning |
|-------|---------|
| `name` | Topology id used by DB / compile |
| `description`, `category` | Metadata |
| `parts[]` | Catalogue rows (mpn, type, value, package, optional ratings) |
| `components[]` | Roles with `part_type`, `quantity`; optional `target_value`, `package` for binder |
| `nodes[]` | Net names |
| `connections[]` | `{role, pin, node}` |

Optional `schema: "schematic-ir.v1"` for self-description.

Prompts under `fixtures/prompts/` map offline to `fixtures/schematics/NNN.json` via the file schematic provider.
