#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *cli_fixture_root(void) {
  const char *root = getenv("SYNTH_FIXTURE_ROOT");

  if (root && root[0] != '\0')
    return root;

  return ".";
}

char *cli_join_path(const char *a, const char *b) {
  size_t len_a;
  size_t len_b;
  char *out;
  int need_slash;

  if (!a || !b)
    return NULL;

  len_a = strlen(a);
  len_b = strlen(b);
  need_slash = (len_a > 0 && a[len_a - 1] != '/' && a[len_a - 1] != '\\');

  out = malloc(len_a + len_b + (need_slash ? 2u : 1u));
  if (!out)
    return NULL;

  memcpy(out, a, len_a);
  if (need_slash) {
    out[len_a] = '/';
    memcpy(out + len_a + 1, b, len_b + 1);
  } else {
    memcpy(out + len_a, b, len_b + 1);
  }

  return out;
}

void print_usage(void) {
  printf("\n"
         "====================================================\n"
         " Physics2 + DFM + DB + Compiler CLI\n"
         "====================================================\n"
         "\n"
         "Database:\n"
         "  synth db reset [db]\n"
         "  synth db import <csv> [db]\n"
         "  synth db inspect [db]\n"
         "  synth db candidates [db]\n"
         "  synth db topology [db]\n"
         "  synth db seed <seed.json> [db]\n"
         "\n"
         "DFM:\n"
         "  synth dfm test\n"
         "\n"
         "Compiler:\n"
         "  synth compile divider [db] [output.kicad_sch]\n"
         "\n"
         "Generate:\n"
         "  synth generate <design.json> -o out/\n"
         "  synth generate --spec <spec.json> -o out/\n"
         "  synth generate --compose-gate4 -o out/\n"
         "\n"
         "Physics2:\n"
         "  synth physics2\n"
         "    (thin wrapper — Gate 1 regressions live in `ctest` / golden_runner)\n"
         "\n"
         "CSV path is always a CLI argument. No absolute machine paths.\n"
         "\n");
}
