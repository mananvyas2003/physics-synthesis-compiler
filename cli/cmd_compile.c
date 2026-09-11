#include "cli.h"

#include "compiler.h"
#include "db.h"
#include "seed_topology.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmd_compile_divider(const char *db_path, const char *output,
                               const char *seed_json) {
  DB *db;
  CompiledSchematic schematic;
  DBResult result;

  db = DB_open(db_path);
  if (!db) {
    fprintf(stderr, "[COMPILER] Failed to open %s\n", db_path);
    return 1;
  }

  if (seed_load_topology_json(db, seed_json) != 0) {
    fprintf(stderr, "[COMPILER] Failed to seed topology from %s\n", seed_json);
    DB_close(db);
    return 1;
  }

  result = compiler_compile_resistor_divider(db, "resistor_divider", &schematic);
  if (result != DB_OK) {
    fprintf(stderr, "[COMPILER] compile failed (result=%d)\n", result);
    DB_close(db);
    return 1;
  }

  if (!compiler_write_kicad_sch(output, &schematic)) {
    fprintf(stderr, "[COMPILER] Failed to write %s\n", output);
    compiler_free_schematic(&schematic);
    DB_close(db);
    return 1;
  }

  printf("[COMPILER] Wrote %s\n", output);
  compiler_free_schematic(&schematic);
  DB_close(db);
  return 0;
}

int cmd_compile(int argc, char **argv) {
  const char *db_path;
  const char *output;
  char *seed_path;
  int rc;

  if (argc < 3) {
    fprintf(stderr, "Usage: synth compile divider [db] [output.kicad_sch]\n");
    return 1;
  }

  if (strcmp(argv[2], "divider") != 0) {
    fprintf(stderr, "[COMPILER] Unknown target: %s\n", argv[2]);
    return 1;
  }

  db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;
  output = argc >= 5 ? argv[4] : "generated_resistor_divider.kicad_sch";

  seed_path = cli_join_path(cli_fixture_root(), "fixtures/seed/resistor_divider.json");
  if (!seed_path)
    return 1;

  rc = cmd_compile_divider(db_path, output, seed_path);
  free(seed_path);
  return rc;
}
