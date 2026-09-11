#include "cli.h"

#include "db.h"
#include "jlcparts_import.h"
#include "seed_topology.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DB *OpenDB(const char *path) {
  DB *db = DB_open(path);

  if (!db)
    fprintf(stderr, "[DB] Failed to open %s\n", path);
  else
    printf("[DB] Opened %s\n", path);

  return db;
}

static void PrintCandidates(const char *label, const DBPart *parts, int count) {
  int i;

  printf("\n%s -> %d candidate(s)\n", label, count);

  for (i = 0; i < count; ++i) {
    printf("  %d. %-28s value=%-12g package=%-10s "
           "V=%-8.3g I=%-8.3g P=%-8.3g\n",
           i + 1, parts[i].mpn, parts[i].value, parts[i].package,
           parts[i].v_rating, parts[i].i_rating, parts[i].power_rating_w);
  }

  if (count == 0)
    printf("  (none)\n");
}

static int cmd_db_reset(const char *db_path) {
  if (remove(db_path) == 0)
    printf("[DB] Removed %s\n", db_path);
  else
    printf("[DB] %s did not exist\n", db_path);

  return 0;
}

static int cmd_db_import(const char *csv_path, const char *db_path) {
  DB *db;
  int rows;

  printf("\n[DB] CSV    : %s\n", csv_path);
  printf("[DB] Output : %s\n\n", db_path);

  remove(db_path);

  db = OpenDB(db_path);
  if (!db)
    return 1;

  rows = ImportJLCPCBCsv(db, csv_path);
  printf("[IMPORT] Rows inserted: %d\n", rows);
  DB_close(db);

  if (rows <= 0) {
    fprintf(stderr, "[DB] CSV import produced no rows.\n");
    return 1;
  }

  return 0;
}

static int cmd_db_inspect(const char *db_path) {
  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  DB_PrintAllParts(db);
  DB_close(db);
  return 0;
}

static int cmd_db_candidates(const char *db_path) {
  DB *db = OpenDB(db_path);
  DBPart candidates[16];
  int count;

  if (!db)
    return 1;

  count = DB_FindCandidates(db, PART_RESISTOR, 10000.0, "0603", TOLERANCE_E24,
                            0.0, 0.0, 0.0, candidates, 16);
  PrintCandidates("10k / 0603 resistor search", candidates, count);
  DB_close(db);

  return count > 0 ? 0 : 1;
}

static int cmd_db_topology(const char *db_path) {
  DB *db = OpenDB(db_path);
  TopologyComponentRow components[32];
  TopologyNodeRow nodes[32];
  TopologyConnectionRow connections[64];
  int component_count = 0;
  int node_count = 0;
  int connection_count = 0;
  DBResult r;
  int i;

  if (!db)
    return 1;

  r = DB_GetTopology(db, "resistor_divider", components, 32, &component_count,
                     nodes, 32, &node_count, connections, 64, &connection_count);

  printf("[DB] result=%d components=%d nodes=%d connections=%d\n", r,
         component_count, node_count, connection_count);

  for (i = 0; i < component_count; ++i) {
    printf("  COMPONENT %-10s type=%d quantity=%d\n", components[i].role_name,
           components[i].part_type, components[i].quantity);
  }

  for (i = 0; i < node_count; ++i)
    printf("  NODE      %s\n", nodes[i].node_name);

  for (i = 0; i < connection_count; ++i) {
    printf("  CONNECT   %-10s pin=%-4s node=%s\n",
           connections[i].component_role, connections[i].pin_name,
           connections[i].node_name);
  }

  DB_close(db);
  return r == DB_OK ? 0 : 1;
}

static int cmd_db_seed(const char *json_path, const char *db_path) {
  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  if (seed_load_topology_json(db, json_path) != 0) {
    DB_close(db);
    return 1;
  }

  printf("[DB] Seeded from %s into %s\n", json_path, db_path);
  DB_close(db);
  return 0;
}

int cmd_db(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage:\n"
            "  synth db reset [db]\n"
            "  synth db import <csv> [db]\n"
            "  synth db inspect [db]\n"
            "  synth db candidates [db]\n"
            "  synth db topology [db]\n"
            "  synth db seed <seed.json> [db]\n");
    return 1;
  }

  if (strcmp(argv[2], "reset") == 0) {
    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;
    return cmd_db_reset(db_path);
  }

  if (strcmp(argv[2], "import") == 0) {
    const char *csv_path;
    const char *db_path;

    if (argc < 4) {
      fprintf(stderr, "Usage: synth db import <csv> [db]\n");
      return 1;
    }

    csv_path = argv[3];
    db_path = argc >= 5 ? argv[4] : DEFAULT_DB_PATH;
    return cmd_db_import(csv_path, db_path);
  }

  if (strcmp(argv[2], "inspect") == 0) {
    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;
    return cmd_db_inspect(db_path);
  }

  if (strcmp(argv[2], "candidates") == 0) {
    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;
    return cmd_db_candidates(db_path);
  }

  if (strcmp(argv[2], "topology") == 0) {
    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;
    return cmd_db_topology(db_path);
  }

  if (strcmp(argv[2], "seed") == 0) {
    const char *json_path;
    const char *db_path;

    if (argc < 4) {
      fprintf(stderr, "Usage: synth db seed <seed.json> [db]\n");
      return 1;
    }

    json_path = argv[3];
    db_path = argc >= 5 ? argv[4] : DEFAULT_DB_PATH;
    return cmd_db_seed(json_path, db_path);
  }

  fprintf(stderr, "[CLI] Unknown db subcommand: %s\n", argv[2]);
  return 1;
}
