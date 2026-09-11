# Gate 1 — Foundation hardening

## Criteria

- CMake builds `synth` and `golden_runner`
- Vendors under `third_party/` only
- Seed data is file-based (`fixtures/seed/`)
- Dual-OS CI + Ubuntu ASan/UBSan workflow present
- Five golden-file tests decide pass/fail by diff (not `printf("[PASS]")`)

## Deliberate failure proof

Date: 2026-09-11

Procedure (does not mutate tracked files):

1. Copy `tests/golden` + `fixtures/seed` into a temporary fixture root.
2. Corrupt only `g02_divider_5v/expected.txt` in that temp tree (`Vout=5.0000000000` → `Vout=9.9999999999`).
3. Run `golden_runner` with `SYNTH_FIXTURE_ROOT` pointing at the temp tree.
4. Observe:

```text
[GOLDEN] DIFF g02_divider_5v
golden_runner failures=1
```

5. Re-run against the real repo fixture root:

```text
GOLDEN_OK g01_resistor_stamp
GOLDEN_OK g02_divider_5v
GOLDEN_OK g03_three_resistor
GOLDEN_OK g04_dfm_suite
GOLDEN_OK g05_compile_divider_sch
golden_runner failures=0
```

The harness fails closed on content mismatch. After push, confirm the same red/green behavior via GitHub Actions on `ubuntu-latest` and `windows-latest`, plus the ASan job.

## Closed

Gate 1 implementation is complete in-tree. Sign the checklist date once CI on `main` is green.
