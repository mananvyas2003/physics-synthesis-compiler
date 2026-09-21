# Build Audit — Phase 0

**Date:** 2026-09-21  
**Platform:** Windows 10 (win32 10.0.19043), x86_64  
**Scope:** Compile and test only. No source changes to silence warnings.

---

## 1. Compiler discovery

| Tool | Status | Version / path |
|------|--------|----------------|
| `gcc` | **FOUND** | 12.1.0 (MSYS2 MinGW64) `D:\msys64\mingw64\bin\gcc.exe` |
| `g++` | **FOUND** | 12.1.0 same toolchain |
| `clang` / `clang++` | NOT FOUND | — |
| `cl` (MSVC) | NOT FOUND | — |
| `cmake` | NOT FOUND | not on PATH; not under common install dirs |
| `make` | NOT FOUND | — |
| `ninja` | NOT FOUND | — |

**Chosen toolchain:** MinGW GCC 12.1.0 (preferred for strict C11).  
**Build generator:** manual `gcc -c` / `gcc -o` (CMake unavailable). Source lists mirrored from root [`CMakeLists.txt`](../../CMakeLists.txt).  
**Linker:** `ld` via `x86_64-w64-mingw32` collect2; linked with `-lpthread` (and `-lm` for golden/vendor tests).

---

## 2. C standard and flags

| Setting | Value |
|---------|-------|
| Language | `-std=c11` |
| Extensions | off (matches CMake `CMAKE_C_EXTENSIONS OFF`) |
| Debug | `-O0 -g` |
| Warnings (project sources) | `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith` |
| SQLite amalgamation exception | `-Wall -Wno-unused-parameter -Wno-sign-compare` only (pedantic conversion flags would flood third-party code) |
| Output directory | `audit_build/` |

---

## 3. Build command (reproducible)

```text
# From repo root, with MinGW on PATH:
gcc -std=c11 -O0 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
  -I. -Icli -Iseed -Iemit -Ispec -Icompose -Ibind -Iverify \
  -Ithird_party/sqlite3 -Ithird_party/cJSON -Ivendor_next/src \
  -c <each .c> -o audit_build/<name>.o

gcc -o audit_build/synth.exe audit_build/main.o <all synth+vendor+sqlite+cjson .o> -lpthread
gcc -o audit_build/golden_runner.exe ... -lpthread -lm
# vendor_*_test.exe link electronics_core objects only + -lm
```

Artifacts: `audit_build/synth.exe`, `audit_build/golden_runner.exe`, eight vendor test executables.

---

## 4. Warnings (project code — not silenced)

### Counts (strict flags)

| Unit | Approximate warning lines |
|------|---------------------------|
| `synth_core` sources | **30** |
| `electronics_core` (vendor_next) | **6** |
| cJSON (third_party, strict flags) | **2** |
| SQLite | not counted under strict flags (exception) |

### Dominant categories

| Flag | Approx count | Notes |
|------|--------------|-------|
| `-Wfloat-conversion` | ~27 synth + several mna | MinGW `isfinite(double)` often prototypes as float overload; false-ish noise on doubles |
| `-Wunused-function` | 2 | `find_connection_any` (compiler.c), `matrix_node_entry` (mna.c) |
| `-Wmissing-prototypes` | 1 | `ImportJlcPartsRow` in jlcparts_import.c |
| `-Wconversion` | 1+ | size_t→int in jlcparts_import |

Full logs: `audit_build/synth_compile.log`, `audit_build/vendor_compile.log`, `audit_build/cjson_compile.log`.

**Linker errors:** none for the release/debug audit build.

---

## 5. Sanitizers

| Check | Result |
|-------|--------|
| `-fsanitize=address,undefined` probe link | **FAILED** |
| Reason | `ld: cannot find -lasan` / `-lubsan` |
| Status | **NOT AVAILABLE** on this MinGW install |

No sanitizer test run. CMake option `SYNTH_ENABLE_SANITIZERS` remains documented for GCC/Clang environments that ship the libs.

---

## 6. Test execution

Environment: `SYNTH_FIXTURE_ROOT=d:\physics-synthesis-compiler`  
Working directory: repo root.

### Vendor unit tests (`electronics_core`)

| Test | Exit | Result |
|------|------|--------|
| `vec_test` | 0 | PASSED |
| `intern_test` | 0 | PASSED |
| `design_test` | 0 | PASSED |
| `component_model_test` | 0 | PASSED |
| `model_test` | 0 | PASSED |
| `dfm_test` | 0 | PASSED |
| `provider_test` | 0 | PASSED |
| `mna_test` | 0 | PASSED (matrix, divider, nonlinear diode, multi-diode) |

### Golden runner

| Case | Result | Mathematical claim (audit note) |
|------|--------|----------------------------------|
| g01_resistor_stamp | GOLDEN_OK | Matrix G stamp entries |
| g02_divider_5v | GOLDEN_OK | Analytic Vout=5 for 10 V equal R |
| g03_three_resistor | GOLDEN_OK | Analytic divider with parallel |
| g04_dfm_suite | GOLDEN_OK | Block-compose port DFM |
| g05–g08 | GOLDEN_OK | Emit sch/net/bom/snapshot |
| g09_spec_corpus | GOLDEN_OK | SpecV1 offline corpus |
| g10–g11 | GOLDEN_OK | Compose / bind rationale |
| g12_verify_report | GOLDEN_OK | Verify JSON shape + Physics2 divider |
| g13–g16 | GOLDEN_OK | Schematic corpus / prompt / ERC structural |
| g17_led_series | GOLDEN_OK | Generate+verify LED path (analytical) |
| g18_rc_low_pass | GOLDEN_OK | Compile/verify pass — **not** τ/fc assertion |
| g19_invalid_role | GOLDEN_OK | Rejection of bad role |
| g20_rl_low_pass | GOLDEN_OK | Compile/verify pass — **not** τ assertion |
| g21_ldo_3v3 | GOLDEN_OK | Structural IC path |

**Summary:** `golden_runner failures=0` (exit 0). All 21 golden cases + 8 vendor tests **PASS**.

**Re-verification:** same machine, later same calendar day — all 8 vendor tests PASS; golden g01–g21 `failures=0` again.

---

## 7. Generated / external dependencies

| Dependency | Role | Built how |
|------------|------|-----------|
| `third_party/sqlite3` | Parts + topology DB | Compiled into synth |
| `third_party/cJSON` | JSON IR | Compiled into synth |
| `vendor_next/` | Design IR, DFM, MNA, providers | `electronics_core` objects |
| curl (runtime) | Gemini HTTP only if API key set | External binary, not linked |
| KiCad | Optional ERC script | Not required for ctest/golden |

**Generated during audit:** `audit_build/*` (local; should remain untracked).  
**Previously committed generated risk:** root `synth.exe`, various `out_*` trees (see CLEANUP_REPORT).

---

## 8. Exceptions / notes

1. CMake recommended by README but unavailable — audit used equivalent manual build.
2. `-Wfloat-conversion` on `isfinite` is a MinGW/header quirk; not treated as a project defect without further proof.
3. Existing root `synth.exe` was **not** used for tests; `audit_build/synth.exe` and `audit_build/golden_runner.exe` were.
