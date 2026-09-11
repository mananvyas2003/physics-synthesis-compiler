# design-snapshot.v1

Frozen minimal snapshot emitted by `synth generate` into `out/design-snapshot.v1.json`.

Required fields:

- `schema` = `"design-snapshot.v1"`
- `generator`, `generator_version`
- `topology`
- `components[]` with `role`, `refdes`, `mpn`, `value`, `package`, `qty`
- `nets[]` with `name`, `pins[]` (`"R1.1"` style)

Machine validation: `emit_snapshot_validate_file()` (structural). JSON Schema file: [`schemas/design-snapshot.v1.json`](../schemas/design-snapshot.v1.json).
