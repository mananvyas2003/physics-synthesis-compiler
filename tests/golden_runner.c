#include "golden_cases.h"

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const char *expected_relpath;
  int (*run)(FILE *out, const char *fixture_root);
} GoldenCase;

static int run_g01(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g01_resistor_stamp(out);
}

static int run_g02(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g02_divider_5v(out);
}

static int run_g03(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g03_three_resistor(out);
}

static int run_g04(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g04_dfm_suite(out);
}

static int run_g05(FILE *out, const char *fixture_root) {
  return golden_g05_compile_divider_sch(out, fixture_root);
}

static int run_g06(FILE *out, const char *fixture_root) {
  return golden_g06_generate_net(out, fixture_root);
}

static int run_g07(FILE *out, const char *fixture_root) {
  return golden_g07_generate_bom(out, fixture_root);
}

static int run_g08(FILE *out, const char *fixture_root) {
  return golden_g08_snapshot(out, fixture_root);
}

static int run_g09(FILE *out, const char *fixture_root) {
  return golden_g09_spec_corpus(out, fixture_root);
}

static int run_g10(FILE *out, const char *fixture_root) {
  return golden_g10_compose_gate4(out, fixture_root);
}

static int run_g11(FILE *out, const char *fixture_root) {
  return golden_g11_bind_rationale(out, fixture_root);
}

static int run_g12(FILE *out, const char *fixture_root) {
  return golden_g12_verify_report(out, fixture_root);
}

static char *read_file(const char *path) {
  FILE *fp;
  long size;
  char *buf;

  fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }

  size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  buf = malloc((size_t)size + 1u);
  if (!buf) {
    fclose(fp);
    return NULL;
  }

  if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
    free(buf);
    fclose(fp);
    return NULL;
  }

  buf[size] = '\0';
  fclose(fp);
  return buf;
}

static void normalize_newlines(char *text) {
  char *src;
  char *dst;

  if (!text)
    return;

  src = text;
  dst = text;

  while (*src) {
    if (*src == '\r') {
      src++;
      continue;
    }
    *dst++ = *src++;
  }

  *dst = '\0';
}

static int write_temp_and_compare(const GoldenCase *gc, const char *fixture_root,
                                  const char *expected_path) {
  char actual_path[256];
  FILE *fp;
  char *expected;
  char *actual;
  int rc;

  snprintf(actual_path, sizeof(actual_path), "golden_actual_%s.txt", gc->name);

  fp = fopen(actual_path, "wb");
  if (!fp) {
    fprintf(stderr, "[GOLDEN] cannot write %s\n", actual_path);
    return 1;
  }

  rc = gc->run(fp, fixture_root);
  fclose(fp);

  if (rc != 0) {
    fprintf(stderr, "[GOLDEN] %s case execution failed\n", gc->name);
    remove(actual_path);
    return 1;
  }

  expected = read_file(expected_path);
  actual = read_file(actual_path);

  if (!expected || !actual) {
    fprintf(stderr, "[GOLDEN] %s missing expected or actual file\n", gc->name);
    free(expected);
    free(actual);
    remove(actual_path);
    return 1;
  }

  normalize_newlines(expected);
  normalize_newlines(actual);

  if (strcmp(expected, actual) != 0) {
    fprintf(stderr, "[GOLDEN] DIFF %s\n--- expected ---\n%s\n--- actual ---\n%s\n",
            gc->name, expected, actual);
    free(expected);
    free(actual);
    remove(actual_path);
    return 1;
  }

  free(expected);
  free(actual);
  remove(actual_path);
  return 0;
}

int main(int argc, char **argv) {
  const char *fixture_root;
  const GoldenCase cases[] = {
      {"g01_resistor_stamp", "tests/golden/g01_resistor_stamp/expected.txt",
       run_g01},
      {"g02_divider_5v", "tests/golden/g02_divider_5v/expected.txt", run_g02},
      {"g03_three_resistor", "tests/golden/g03_three_resistor/expected.txt",
       run_g03},
      {"g04_dfm_suite", "tests/golden/g04_dfm_suite/expected.txt", run_g04},
      {"g05_compile_divider_sch",
       "tests/golden/g05_compile_divider_sch/expected.txt", run_g05},
      {"g06_generate_net", "tests/golden/g06_generate_net/expected.txt",
       run_g06},
      {"g07_generate_bom", "tests/golden/g07_generate_bom/expected.txt",
       run_g07},
      {"g08_snapshot", "tests/golden/g08_snapshot/expected.txt", run_g08},
      {"g09_spec_corpus", "tests/golden/g09_spec_corpus/expected.txt", run_g09},
      {"g10_compose_gate4", "tests/golden/g10_compose_gate4/expected.txt",
       run_g10},
      {"g11_bind_rationale", "tests/golden/g11_bind_rationale/expected.txt",
       run_g11},
      {"g12_verify_report", "tests/golden/g12_verify_report/expected.txt",
       run_g12},
  };
  size_t i;
  int failures = 0;

  (void)argc;
  (void)argv;

  fixture_root = cli_fixture_root();

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char *expected_path =
        cli_join_path(fixture_root, cases[i].expected_relpath);

    if (!expected_path) {
      failures++;
      continue;
    }

    if (write_temp_and_compare(&cases[i], fixture_root, expected_path) != 0)
      failures++;
    else
      fprintf(stdout, "GOLDEN_OK %s\n", cases[i].name);

    free(expected_path);
  }

  if (failures != 0) {
    fprintf(stderr, "golden_runner failures=%d\n", failures);
    return 1;
  }

  fprintf(stdout, "golden_runner failures=0\n");
  return 0;
}
