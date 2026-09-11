#include "cli.h"

#include <stdio.h>

/*
 * Gate 1: Physics2 numerical regressions moved to golden_runner / ctest.
 * Keep a thin CLI entry so existing docs still resolve.
 */
int cmd_physics2(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("physics2 regressions are owned by golden_runner / ctest\n");
  printf("run: cmake --build build && ctest --test-dir build --output-on-failure\n");
  return 0;
}
