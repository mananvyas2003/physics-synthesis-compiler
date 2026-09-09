#include "db.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct DB {
  sqlite3 *handle;
};

/* SQL statements — given to you, these are facts about your schema,
   not something you need to derive. Your job is the control flow
   around them. */
static const char *SQL_CREATE_TABLE = "CREATE TABLE IF NOT EXISTS Parts ("
                                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                      "mpn TEXT UNIQUE NOT NULL, "
                                      "type INTEGER NOT NULL, "
                                      "value REAL NOT NULL, "
                                      "package TEXT NOT NULL, "
                                      "v_rating REAL, "
                                      "i_rating REAL, "
                                      "esr_ohms REAL, "
                                      "power_rating_w REAL, "
                                      "tolerance_class INTEGER);";

static const char *SQL_CREATE_INDEX =
    "CREATE INDEX IF NOT EXISTS idx_parts_type_value ON Parts(type, value);";

static const char *SQL_CREATE_FAB_TABLE =
    "CREATE TABLE IF NOT EXISTS FabRules ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "fab_name TEXT UNIQUE NOT NULL, "
    "layer_count INTEGER NOT NULL, "
    "min_trace_width_mm REAL NOT NULL, "
    "min_clearance_mm REAL NOT NULL, "
    "min_drill_mm REAL NOT NULL, "
    "min_annular_ring_mm REAL NOT NULL);";

static const char *SQL_PRAGMA_WAL = "PRAGMA journal_mode = WAL;";
static const char *SQL_PRAGMA_SYNC = "PRAGMA synchronous = NORMAL;";

static const char *SQL_ENABLE_FOREIGN_KEYS = "PRAGMA foreign_keys = ON;";

static const char *SQL_CREATE_TOPOLOGIES =
    "CREATE TABLE IF NOT EXISTS Topologies ("
    "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    name        TEXT UNIQUE NOT NULL,"
    "    description TEXT,"
    "    category    TEXT"
    ");";

static const char *SQL_CREATE_TOPOLOGY_COMPONENTS =
    "CREATE TABLE IF NOT EXISTS TopologyComponents ("
    "    id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    topology_id  INTEGER NOT NULL,"
    "    role_name    TEXT NOT NULL,"
    "    part_type    INTEGER NOT NULL,"
    "    quantity     INTEGER NOT NULL DEFAULT 1,"
    "    FOREIGN KEY (topology_id) REFERENCES Topologies(id) ON DELETE CASCADE"
    ");";

static const char *SQL_CREATE_TOPOLOGY_NODES =
    "CREATE TABLE IF NOT EXISTS TopologyNodes ("
    "    id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    topology_id INTEGER NOT NULL,"
    "    node_name   TEXT NOT NULL,"
    "    FOREIGN KEY (topology_id) REFERENCES Topologies(id) ON DELETE CASCADE"
    ");";

static const char *SQL_CREATE_TOPOLOGY_CONNECTIONS =
    "CREATE TABLE IF NOT EXISTS TopologyConnections ("
    "    id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    component_id INTEGER NOT NULL,"
    "    pin_name     TEXT NOT NULL,"
    "    node_id      INTEGER NOT NULL,"
    "    FOREIGN KEY (component_id) REFERENCES TopologyComponents(id) ON "
    "DELETE CASCADE,"
    "    FOREIGN KEY (node_id) REFERENCES TopologyNodes(id) ON DELETE CASCADE"
    ");";

double Tolerance_ToPercentage(ToleranceClass tolerance) {
  switch (tolerance) {
  case TOLERANCE_E192:
    return 0.5;
  case TOLERANCE_E96:
    return 1.0;
  case TOLERANCE_E48:
    return 2.0;
  case TOLERANCE_E24:
    return 5.0;
  case TOLERANCE_E12:
    return 10.0;

  default:
    return 5.0;
  }
}

DBResult DB_BeginTransaction(DB *db) {
  if (!db || !db->handle) {
    fprintf(stderr, "[DB_BeginTransaction ERROR]: Invalid NULL argument.\n");
    return DB_ERROR;
  }
  char *err_msg = NULL;
  int rc = sqlite3_exec(db->handle, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[DB_BeginTransaction ERROR]: %s\n",
            err_msg ? err_msg : "Unknown");
    if (err_msg)
      sqlite3_free(err_msg);
    return DB_ERROR;
  }
  return DB_OK; /* Ensure this matches your DBResult enum (e.g. DB_OK) */
}

DBResult DB_CommitTransaction(DB *db) {
  if (!db || !db->handle) {
    fprintf(stderr, "[DB_CommitTransaction ERROR]: Invalid NULL argument.\n");
    return DB_ERROR;
  }
  char *err_msg = NULL;
  int rc = sqlite3_exec(db->handle, "COMMIT;", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[DB_CommitTransaction ERROR]: %s\n",
            err_msg ? err_msg : "Unknown");
    if (err_msg)
      sqlite3_free(err_msg);
    return DB_ERROR;
  }
  return DB_OK;
}

DBResult DB_RollbackTransaction(DB *db) {
  if (!db || !db->handle) {
    return DB_ERROR;
  }
  char *err_msg = NULL;
  sqlite3_exec(db->handle, "ROLLBACK;", NULL, NULL, &err_msg);
  if (err_msg)
    sqlite3_free(err_msg);
  return DB_OK;
}

DB *DB_open(const char *filename) {
  DB *db = (DB *)malloc(sizeof(DB));
  if (!db) {
    fprintf(stderr,
            "[DB_open ERROR]: Failed to allocate memory for DB handle.\n");
    return NULL;
  }
  db->handle = NULL;

  int rc = sqlite3_open(filename, &db->handle);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[DB_open ERROR]: sqlite3_open failed: %s\n",
            db->handle ? sqlite3_errmsg(db->handle) : "Unknown Error");
    if (db->handle)
      sqlite3_close(db->handle);
    free(db);
    return NULL;
  }

  char *err_msg = NULL;

  /* --- PRAGMAS --- */
  sqlite3_exec(db->handle, SQL_PRAGMA_WAL, NULL, NULL, NULL);
  sqlite3_exec(db->handle, SQL_PRAGMA_SYNC, NULL, NULL, NULL);

  /* --- FATAL SCHEMA SETUP --- */
  if (sqlite3_exec(db->handle, SQL_ENABLE_FOREIGN_KEYS, NULL, NULL, &err_msg) !=
      SQLITE_OK)
    goto fatal_cleanup;
  if (sqlite3_exec(db->handle, SQL_CREATE_TABLE, NULL, NULL, &err_msg) !=
      SQLITE_OK)
    goto fatal_cleanup;
  if (sqlite3_exec(db->handle, SQL_CREATE_FAB_TABLE, NULL, NULL, &err_msg) !=
      SQLITE_OK)
    goto fatal_cleanup;
  if (sqlite3_exec(db->handle, SQL_CREATE_TOPOLOGIES, NULL, NULL, &err_msg) !=
      SQLITE_OK)
    goto fatal_cleanup;
  if (sqlite3_exec(db->handle, SQL_CREATE_TOPOLOGY_COMPONENTS, NULL, NULL,
                   &err_msg) != SQLITE_OK)
    goto fatal_cleanup;
  if (sqlite3_exec(db->handle, SQL_CREATE_TOPOLOGY_NODES, NULL, NULL,
                   &err_msg) != SQLITE_OK)
    goto fatal_cleanup;
  if (sqlite3_exec(db->handle, SQL_CREATE_TOPOLOGY_CONNECTIONS, NULL, NULL,
                   &err_msg) != SQLITE_OK)
    goto fatal_cleanup;

  /* --- NON-DESTRUCTIVE MIGRATION (For pre-existing databases) --- */
  const char *migrations[] = {
      "ALTER TABLE Parts ADD COLUMN v_rating REAL;",
      "ALTER TABLE Parts ADD COLUMN i_rating REAL;",
      "ALTER TABLE Parts ADD COLUMN esr_ohms REAL;",
      "ALTER TABLE Parts ADD COLUMN power_rating_w REAL;",
      "ALTER TABLE Parts ADD COLUMN tolerance_class INTEGER;"};
  for (int i = 0; i < 5; i++) {
    sqlite3_exec(db->handle, migrations[i], NULL, NULL, NULL);
  }

  /* --- NON-FATAL INDEX CREATION --- */
  sqlite3_exec(db->handle, SQL_CREATE_INDEX, NULL, NULL, NULL);

  return db;

fatal_cleanup:
  fprintf(stderr, "[DB_open FATAL]: %s\n",
          err_msg ? err_msg : "Unknown setup error");
  if (err_msg)
    sqlite3_free(err_msg);
  if (db->handle)
    sqlite3_close(db->handle);
  free(db);
  return NULL;
}

DBResult DB_InsertPart(DB *db, const char *mpn, PartTypes type, double value,
                       const char *package, int64_t *out_id) {
  /* Guard against NULL database handles or required text pointers upfront
   * (checking db->handle safely after verifying db != NULL). */
  if (!db || !db->handle || !mpn || !package) {
    fprintf(stderr, "[DB_InsertPart ERROR]: Invalid NULL argument provided.\n");
    return DB_ERROR;
  }

  const char *sql =
      "INSERT INTO Parts (mpn, type, value, package) VALUES (?, ?, ?, ?);";
  sqlite3_stmt *stmt = NULL;

  /* Prepare SQL statement into bytecode */
  int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[DB_InsertPart ERROR]: Prepare failed: %s\n",
            sqlite3_errmsg(db->handle));
    return DB_ERROR;
  }

  DBResult result = DB_ERROR;

  /* Bind parameters (1-indexed matching SQLite columns:
   * 1: mpn (TEXT), 2: type (INTEGER), 3: value (REAL), 4: package (TEXT)) */
  if (sqlite3_bind_text(stmt, 1, mpn, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_int(stmt, 2, (int)type) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 3, value) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 4, package, -1, SQLITE_TRANSIENT) != SQLITE_OK) {

    fprintf(stderr, "[DB_InsertPart ERROR]: Parameter binding failed: %s\n",
            sqlite3_errmsg(db->handle));
    goto cleanup;
  }

  /* Execute the statement step */
  rc = sqlite3_step(stmt);

  /* Flattened, sibling outcome decision tree */
  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id) {
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
    }
  } else if (rc == SQLITE_CONSTRAINT) {
    fprintf(
        stderr,
        "[DB_InsertPart WARNING]: Constraint violation (Duplicate MPN): %s\n",
        sqlite3_errmsg(db->handle));
    result = DB_DUPLICATE;
  } else {
    fprintf(stderr, "[DB_InsertPart ERROR]: Step execution failed (%d): %s\n",
            rc, sqlite3_errmsg(db->handle));
    result = DB_ERROR;
  }

cleanup:
  /* Guaranteed single point of statement finalization and resource cleanup */
  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_FindClosestPart(DB *db, PartTypes type, double targetValue,
                            const char *package, ToleranceClass tolerance,
                            DBPart *out_part) {
  if (!db || !db->handle || !package || !out_part)
    return DB_ERROR;

  double tolPercent = Tolerance_ToPercentage(tolerance);
  double window = targetValue * (tolPercent / 100.0);

  const char *sql =
      "SELECT mpn, type, value, package, v_rating, i_rating, esr_ohms, "
      "power_rating_w, tolerance_class "
      "FROM Parts WHERE type = ? AND package = ? "
      "AND value BETWEEN ? AND ? ORDER BY ABS(value - ?) LIMIT 1;";

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    return DB_ERROR;

  DBResult status = DB_ERROR;

  if (sqlite3_bind_int(stmt, 1, (int)type) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, package, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 3, targetValue - window) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 4, targetValue + window) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 5, targetValue) != SQLITE_OK) {
    goto cleanup;
  }

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *mpn_str = (const char *)sqlite3_column_text(stmt, 0);
    strncpy(out_part->mpn, mpn_str ? mpn_str : "", sizeof(out_part->mpn) - 1);
    out_part->mpn[sizeof(out_part->mpn) - 1] = '\0';

    out_part->type = (PartTypes)sqlite3_column_int(stmt, 1);
    out_part->value = sqlite3_column_double(stmt, 2);

    const char *pkg_str = (const char *)sqlite3_column_text(stmt, 3);
    strncpy(out_part->package, pkg_str ? pkg_str : "",
            sizeof(out_part->package) - 1);
    out_part->package[sizeof(out_part->package) - 1] = '\0';

    out_part->v_rating = sqlite3_column_type(stmt, 4) == SQLITE_NULL
                             ? 0.0
                             : sqlite3_column_double(stmt, 4);
    out_part->i_rating = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                             ? 0.0
                             : sqlite3_column_double(stmt, 5);
    out_part->esr_ohms = sqlite3_column_type(stmt, 6) == SQLITE_NULL
                             ? 0.0
                             : sqlite3_column_double(stmt, 6);
    out_part->power_rating_w = sqlite3_column_type(stmt, 7) == SQLITE_NULL
                                   ? 0.0
                                   : sqlite3_column_double(stmt, 7);
    out_part->tolerance_class =
        (ToleranceClass)(sqlite3_column_type(stmt, 8) == SQLITE_NULL
                             ? 0
                             : sqlite3_column_int(stmt, 8));

    status = DB_OK;
  } else {
    status = DB_NOT_FOUND;
  }

cleanup:
  sqlite3_finalize(stmt);
  return status;
}

int DB_FindCandidates(DB *db, PartTypes type, double targetValue,
                      const char *package, ToleranceClass tolerance,
                      double min_v_rating, double min_i_rating,
                      double min_power_rating, DBPart *out_parts,
                      int max_results) {
  if (!db || !db->handle || !package || !out_parts || max_results <= 0)
    return 0;

  double tp = Tolerance_ToPercentage(tolerance);
  double window = targetValue * (tp / 100.0);
  double lowerBound = targetValue - window, upperBound = targetValue + window;

  const char *sql =
      "SELECT mpn, type, value, package, v_rating, i_rating, esr_ohms, "
      "power_rating_w, tolerance_class "
      "FROM Parts "
      "WHERE type = ?1 AND package = ?2 AND value BETWEEN ?3 AND ?4 "
      "AND (?5 <= 0.0 OR (v_rating IS NOT NULL AND v_rating >= ?5)) "
      "AND (?6 <= 0.0 OR (i_rating IS NOT NULL AND i_rating >= ?6)) "
      "AND (?7 <= 0.0 OR (power_rating_w IS NOT NULL AND power_rating_w >= "
      "?7)) "
      "ORDER BY ABS(value - ?8) LIMIT ?9;";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "[DB_FindCandidates ERROR]: Prepare failed: %s\n",
            sqlite3_errmsg(db->handle));
    return 0;
  }

  if (sqlite3_bind_int(stmt, 1, (int)type) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, package, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 3, lowerBound) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 4, upperBound) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 5, min_v_rating) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 6, min_i_rating) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 7, min_power_rating) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 8, targetValue) != SQLITE_OK ||
      sqlite3_bind_int(stmt, 9, max_results) != SQLITE_OK) {
    fprintf(stderr, "[DB_FindCandidates ERROR]: Bind failed: %s\n",
            sqlite3_errmsg(db->handle));
    sqlite3_finalize(stmt);
    return 0;
  }

  int count = 0;
  while (count < max_results && sqlite3_step(stmt) == SQLITE_ROW) {
    const char *mpn_str = (const char *)sqlite3_column_text(stmt, 0);
    strncpy(out_parts[count].mpn, mpn_str ? mpn_str : "",
            sizeof(out_parts[count].mpn) - 1);
    out_parts[count].mpn[sizeof(out_parts[count].mpn) - 1] = '\0';

    out_parts[count].type = (PartTypes)sqlite3_column_int(stmt, 1);
    out_parts[count].value = sqlite3_column_double(stmt, 2);

    const char *pkg_str = (const char *)sqlite3_column_text(stmt, 3);
    strncpy(out_parts[count].package, pkg_str ? pkg_str : "",
            sizeof(out_parts[count].package) - 1);
    out_parts[count].package[sizeof(out_parts[count].package) - 1] = '\0';

    out_parts[count].v_rating = sqlite3_column_type(stmt, 4) == SQLITE_NULL
                                    ? 0.0
                                    : sqlite3_column_double(stmt, 4);
    out_parts[count].i_rating = sqlite3_column_type(stmt, 5) == SQLITE_NULL
                                    ? 0.0
                                    : sqlite3_column_double(stmt, 5);
    out_parts[count].esr_ohms = sqlite3_column_type(stmt, 6) == SQLITE_NULL
                                    ? 0.0
                                    : sqlite3_column_double(stmt, 6);
    out_parts[count].power_rating_w =
        sqlite3_column_type(stmt, 7) == SQLITE_NULL
            ? 0.0
            : sqlite3_column_double(stmt, 7);
    out_parts[count].tolerance_class =
        (ToleranceClass)(sqlite3_column_type(stmt, 8) == SQLITE_NULL
                             ? 0
                             : sqlite3_column_int(stmt, 8));
    count++;
  }

  sqlite3_finalize(stmt);
  return count;
}

DBResult DB_InsertFabRule(DB *db, const char *fab_name, int layer_count,
                          double min_trace, double min_clearance,
                          double min_drill, double min_annular,
                          int64_t *out_id) {
  if (!db || !db->handle || !fab_name) {
    fprintf(stderr, "[DB_InsertFabRule ERROR]: Invalid NULL argument.\n");
    return DB_ERROR;
  }

  const char *sql =
      "INSERT INTO FabRules (fab_name, layer_count, min_trace_width_mm, "
      "min_clearance_mm, min_drill_mm, min_annular_ring_mm) "
      "VALUES (?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt = NULL;

  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
    fprintf(stderr, "[DB_InsertFabRule ERROR]: Prepare failed: %s\n",
            sqlite3_errmsg(db->handle));
    return DB_ERROR;
  }

  DBResult result = DB_ERROR;

  /* Bind parameters. Note the SQLITE_TRANSIENT flag on the string! */
  if (sqlite3_bind_text(stmt, 1, fab_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_int(stmt, 2, layer_count) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 3, min_trace) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 4, min_clearance) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 5, min_drill) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 6, min_annular) != SQLITE_OK) {

    fprintf(stderr, "[DB_InsertFabRule ERROR]: Bind failed: %s\n",
            sqlite3_errmsg(db->handle));
    goto cleanup;
  }

  int rc = sqlite3_step(stmt);

  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id) {
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
    }
  } else if (rc == SQLITE_CONSTRAINT) {
    /* Drill down into the specific constraint failure */
    int ext_rc = sqlite3_extended_errcode(db->handle);

    if (ext_rc == SQLITE_CONSTRAINT_UNIQUE) {
      fprintf(stderr, "[DB_InsertFabRule WARNING]: Duplicate fab_name: %s\n",
              sqlite3_errmsg(db->handle));
      result = DB_DUPLICATE;
    } else {
      fprintf(stderr,
              "[DB_InsertFabRule ERROR]: Constraint violation (Extended Code "
              "%d): %s\n",
              ext_rc, sqlite3_errmsg(db->handle));
      result = DB_ERROR;
    }
  } else {
    fprintf(stderr, "[DB_InsertFabRule ERROR]: Execution failed (%d): %s\n", rc,
            sqlite3_errmsg(db->handle));
  }

cleanup:
  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_GetFabRule(DB *db, const char *fab_name, FabRule *out_rule) {
  if (!db || !db->handle || !fab_name || !out_rule) {
    fprintf(stderr, "[DB_GetFabRule ERROR]: Invalid NULL argument.\n");
    return DB_ERROR;
  }

  const char *sql = "SELECT layer_count, min_trace_width_mm, min_clearance_mm, "
                    "min_drill_mm, min_annular_ring_mm FROM FabRules "
                    "WHERE fab_name = ? LIMIT 1;";
  sqlite3_stmt *stmt = NULL;

  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return DB_ERROR;
  }

  DBResult status = DB_ERROR;

  if (sqlite3_bind_text(stmt, 1, fab_name, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
    goto cleanup;
  }

  /* Execute the statement step */
  int rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW) {
    /* 1. Safely copy the query param into our output struct */
    strncpy(out_rule->fab_name, fab_name, sizeof(out_rule->fab_name) - 1);
    out_rule->fab_name[sizeof(out_rule->fab_name) - 1] = '\0';

    /* 2. Extract numeric columns (Indices 0 to 4 based on SELECT statement) */
    out_rule->layer_count = sqlite3_column_int(stmt, 0);
    out_rule->min_trace_width_mm = sqlite3_column_double(stmt, 1);
    out_rule->min_clearance_mm = sqlite3_column_double(stmt, 2);
    out_rule->min_drill_mm = sqlite3_column_double(stmt, 3);
    out_rule->min_annular_ring_mm = sqlite3_column_double(stmt, 4);

    status = DB_OK;
  } else if (rc == SQLITE_DONE) {
    status = DB_NOT_FOUND;
  } else {
    fprintf(stderr, "[DB_GetFabRule ERROR]: Step execution failed (%d): %s\n",
            rc, sqlite3_errmsg(db->handle));
    status = DB_ERROR;
  }

cleanup:
  sqlite3_finalize(stmt);
  return status;
}

void DB_close(DB *db) {
  if (!db)
    return;

  if (db->handle) {
    /* Force WAL pages to flush completely into the main database file */
    char *err_msg = NULL;
    int rc = sqlite3_exec(db->handle, "PRAGMA wal_checkpoint(TRUNCATE);", NULL,
                          NULL, &err_msg);
    if (rc != SQLITE_OK) {
      fprintf(stderr, "[DB_close WARNING]: WAL checkpoint failed: %s\n",
              err_msg ? err_msg : "Unknown Error");
      if (err_msg)
        sqlite3_free(err_msg);
    }

    /* Finalize any dangling unfinalized statements to prevent SQLITE_BUSY */
    sqlite3_stmt *stmt = NULL;
    while ((stmt = sqlite3_next_stmt(db->handle, NULL)) != NULL) {
      sqlite3_finalize(stmt);
    }

    /* Close connection */
    rc = sqlite3_close(db->handle);
    if (rc != SQLITE_OK) {
      fprintf(stderr, "[DB_close ERROR]: sqlite3_close failed (%d): %s\n", rc,
              sqlite3_errmsg(db->handle));
      return; /* Avoid freeing db struct if handle closure failed */
    }
  }

  free(db);
}

/* Updated implementation in db.c */

DBResult DB_InsertPartFull(DB *db, const DBPart *part, int64_t *out_id) {
  if (!db || !db->handle || !part)
    return DB_ERROR;

  const char *sql = "INSERT INTO Parts (mpn, type, value, package, v_rating, "
                    "i_rating, esr_ohms, power_rating_w, tolerance_class) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
    return DB_ERROR;
  }

  /* Bind base fields with error handling */
  if (sqlite3_bind_text(stmt, 1, part->mpn, -1, SQLITE_TRANSIENT) !=
          SQLITE_OK ||
      sqlite3_bind_int(stmt, 2, (int)part->type) != SQLITE_OK ||
      sqlite3_bind_double(stmt, 3, part->value) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 4, part->package, -1, SQLITE_TRANSIENT) !=
          SQLITE_OK) {
    sqlite3_finalize(stmt);
    return DB_ERROR;
  }

  /* Bind parametric fields with strict NULL handling */
  if (part->v_rating > 0.0)
    sqlite3_bind_double(stmt, 5, part->v_rating);
  else
    sqlite3_bind_null(stmt, 5);

  if (part->i_rating > 0.0)
    sqlite3_bind_double(stmt, 6, part->i_rating);
  else
    sqlite3_bind_null(stmt, 6);

  if (part->esr_ohms > 0.0)
    sqlite3_bind_double(stmt, 7, part->esr_ohms);
  else
    sqlite3_bind_null(stmt, 7);

  if (part->power_rating_w > 0.0)
    sqlite3_bind_double(stmt, 8, part->power_rating_w);
  else
    sqlite3_bind_null(stmt, 8);

  sqlite3_bind_int(stmt, 9, (int)part->tolerance_class);

  DBResult result = DB_ERROR;
  int rc = sqlite3_step(stmt);

  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id)
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
  } else if (rc == SQLITE_CONSTRAINT) {
    result = DB_DUPLICATE;
  }

  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_InsertTopology(DB *db, const char *name, const char *description,
                           const char *category, int64_t *out_id) {
  if (!db || !db->handle || !name)
    return DB_ERROR;
  const char *sql =
      "INSERT INTO Topologies (name, description, category) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    return DB_ERROR;
  DBResult result = DB_ERROR;
  if (sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, description ? description : "", -1,
                        SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 3, category ? category : "", -1,
                        SQLITE_TRANSIENT) != SQLITE_OK) {
    goto cleanup;
  }
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id)
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
  } else if (rc == SQLITE_CONSTRAINT) {
    result = DB_DUPLICATE;
  }
cleanup:
  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_AddTopologyComponent(DB *db, int64_t topology_id,
                                 const char *role_name, PartTypes part_type,
                                 int quantity, int64_t *out_id) {
  if (!db || !db->handle || !role_name)
    return DB_ERROR;
  const char *sql = "INSERT INTO TopologyComponents (topology_id, role_name, "
                    "part_type, quantity) "
                    "VALUES (?, ?, ?, ?);";
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    return DB_ERROR;
  DBResult result = DB_ERROR;
  if (sqlite3_bind_int64(stmt, 1, topology_id) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, role_name, -1, SQLITE_TRANSIENT) !=
          SQLITE_OK ||
      sqlite3_bind_int(stmt, 3, (int)part_type) != SQLITE_OK ||
      sqlite3_bind_int(stmt, 4, quantity) != SQLITE_OK) {
    goto cleanup;
  }
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id)
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
  }
cleanup:
  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_AddTopologyNode(DB *db, int64_t topology_id, const char *node_name,
                            int64_t *out_id) {
  if (!db || !db->handle || !node_name)
    return DB_ERROR;
  const char *sql =
      "INSERT INTO TopologyNodes (topology_id, node_name) VALUES (?, ?);";
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    return DB_ERROR;
  DBResult result = DB_ERROR;
  if (sqlite3_bind_int64(stmt, 1, topology_id) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, node_name, -1, SQLITE_TRANSIENT) !=
          SQLITE_OK) {
    goto cleanup;
  }
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id)
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
  }
cleanup:
  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_AddTopologyConnection(DB *db, int64_t component_id,
                                  const char *pin_name, int64_t node_id,
                                  int64_t *out_id) {
  if (!db || !db->handle || !pin_name)
    return DB_ERROR;
  const char *sql = "INSERT INTO TopologyConnections (component_id, pin_name, "
                    "node_id) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    return DB_ERROR;
  DBResult result = DB_ERROR;
  if (sqlite3_bind_int64(stmt, 1, component_id) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, pin_name, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_int64(stmt, 3, node_id) != SQLITE_OK) {
    goto cleanup;
  }
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    result = DB_OK;
    if (out_id)
      *out_id = (int64_t)sqlite3_last_insert_rowid(db->handle);
  }
cleanup:
  sqlite3_finalize(stmt);
  return result;
}

DBResult DB_GetTopology(DB *db, const char *name,
                        TopologyComponentRow *out_components,
                        int max_components, int *out_component_count,
                        TopologyNodeRow *out_nodes, int max_nodes,
                        int *out_node_count,
                        TopologyConnectionRow *out_connections,
                        int max_connections, int *out_connection_count) {
  if (!db || !db->handle || !name || !out_components || !out_component_count ||
      !out_nodes || !out_node_count || !out_connections ||
      !out_connection_count) {
    return DB_ERROR;
  }

  int64_t topology_id = 0;
  {
    const char *sql = "SELECT id FROM Topologies WHERE name = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
      return DB_ERROR;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
      sqlite3_finalize(stmt);
      return DB_NOT_FOUND;
    }
    topology_id = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
  }

  *out_component_count = 0;
  {
    const char *sql =
        "SELECT id, role_name, part_type, quantity FROM TopologyComponents "
        "WHERE topology_id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
      return DB_ERROR;
    sqlite3_bind_int64(stmt, 1, topology_id);
    while (*out_component_count < max_components &&
           sqlite3_step(stmt) == SQLITE_ROW) {
      TopologyComponentRow *c = &out_components[*out_component_count];
      c->id = sqlite3_column_int64(stmt, 0);

      const char *role = (const char *)sqlite3_column_text(stmt, 1);
      strncpy(c->role_name, role ? role : "", sizeof(c->role_name) - 1);
      c->role_name[sizeof(c->role_name) - 1] = '\0';

      c->part_type = (PartTypes)sqlite3_column_int(stmt, 2);
      c->quantity = sqlite3_column_int(stmt, 3);
      (*out_component_count)++;
    }
    sqlite3_finalize(stmt);
  }

  *out_node_count = 0;
  {
    const char *sql =
        "SELECT id, node_name FROM TopologyNodes WHERE topology_id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
      return DB_ERROR;
    sqlite3_bind_int64(stmt, 1, topology_id);
    while (*out_node_count < max_nodes && sqlite3_step(stmt) == SQLITE_ROW) {
      TopologyNodeRow *n = &out_nodes[*out_node_count];
      n->id = sqlite3_column_int64(stmt, 0);

      const char *node = (const char *)sqlite3_column_text(stmt, 1);
      strncpy(n->node_name, node ? node : "", sizeof(n->node_name) - 1);
      n->node_name[sizeof(n->node_name) - 1] = '\0';

      (*out_node_count)++;
    }
    sqlite3_finalize(stmt);
  }

  *out_connection_count = 0;
  {
    const char *sql =
        "SELECT tc.pin_name, comp.role_name, node.node_name "
        "FROM TopologyConnections tc "
        "JOIN TopologyComponents comp ON tc.component_id = comp.id "
        "JOIN TopologyNodes node ON tc.node_id = node.id "
        "WHERE comp.topology_id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
      return DB_ERROR;
    sqlite3_bind_int64(stmt, 1, topology_id);
    while (*out_connection_count < max_connections &&
           sqlite3_step(stmt) == SQLITE_ROW) {
      TopologyConnectionRow *conn = &out_connections[*out_connection_count];

      const char *pin = (const char *)sqlite3_column_text(stmt, 0);
      strncpy(conn->pin_name, pin ? pin : "", sizeof(conn->pin_name) - 1);
      conn->pin_name[sizeof(conn->pin_name) - 1] = '\0';

      const char *role = (const char *)sqlite3_column_text(stmt, 1);
      strncpy(conn->component_role, role ? role : "",
              sizeof(conn->component_role) - 1);
      conn->component_role[sizeof(conn->component_role) - 1] = '\0';

      const char *node = (const char *)sqlite3_column_text(stmt, 2);
      strncpy(conn->node_name, node ? node : "", sizeof(conn->node_name) - 1);
      conn->node_name[sizeof(conn->node_name) - 1] = '\0';

      (*out_connection_count)++;
    }
    sqlite3_finalize(stmt);
  }

  return DB_OK;
}

void DB_PrintAllParts(DB *db) {
  if (!db || !db->handle)
    return;

  const char *sql = "SELECT id, mpn, type, value, package, v_rating, i_rating, "
                    "power_rating_w "
                    "FROM Parts ORDER BY type, value;";
  sqlite3_stmt *stmt = NULL;

  if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
    fprintf(stderr, "[DB ERROR]: Failed to prepare dump query: %s\n",
            sqlite3_errmsg(db->handle));
    return;
  }

  printf("\n==================================================================="
         "=============\n");
  printf("                              DATABASE PARTS DUMP\n");
  printf("====================================================================="
         "===========\n");
  printf("%-5s | %-20s | %-4s | %-10s | %-8s | %-5s | %-5s | %-5s\n", "ID",
         "MPN", "TYPE", "VALUE", "PACKAGE", "V_RAT", "I_RAT", "P_RAT");
  printf("---------------------------------------------------------------------"
         "-----------\n");

  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    printf(
        "%-5lld | %-20s | %-4d | %-10.4g | %-8s | %-5.1f | %-5.2f | %-5.2f\n",
        sqlite3_column_int64(stmt, 0),
        sqlite3_column_text(stmt, 1)
            ? (const char *)sqlite3_column_text(stmt, 1)
            : "N/A",
        sqlite3_column_int(stmt, 2), sqlite3_column_double(stmt, 3),
        sqlite3_column_text(stmt, 4)
            ? (const char *)sqlite3_column_text(stmt, 4)
            : "N/A",
        sqlite3_column_type(stmt, 5) == SQLITE_NULL
            ? 0.0
            : sqlite3_column_double(stmt, 5),
        sqlite3_column_type(stmt, 6) == SQLITE_NULL
            ? 0.0
            : sqlite3_column_double(stmt, 6),
        sqlite3_column_type(stmt, 7) == SQLITE_NULL
            ? 0.0
            : sqlite3_column_double(stmt, 7));
    count++;
  }
  sqlite3_finalize(stmt);

  printf("---------------------------------------------------------------------"
         "-----------\n");
  printf("Total Parts in DB: %d\n", count);
  printf("====================================================================="
         "===========\n\n");
}
