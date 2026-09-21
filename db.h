#pragma once

/* Standard C header for exact-width integer types (int64_t, uint32_t, etc.) */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DB DB;

typedef enum { DB_OK = 0, DB_ERROR, DB_NOT_FOUND, DB_DUPLICATE } DBResult;

typedef enum {
  PART_RESISTOR = 0,
  PART_CAPACITOR,
  PART_INDUCTOR,
  PART_DIODE,
  PART_TRANSISTOR,
  PART_IC,
  PART_CONNECTOR,
  PART_OTHER
} PartTypes;

typedef enum {
  TOLERANCE_E192 = 0,
  TOLERANCE_E96,
  TOLERANCE_E48,
  TOLERANCE_E24,
  TOLERANCE_E12

} ToleranceClass;

typedef struct {
  char mpn[64];
  PartTypes type;
  double value;
  char package[32];
  double v_rating;
  double i_rating;
  double esr_ohms;
  double power_rating_w;
  ToleranceClass tolerance_class;

} DBPart;

typedef struct {

  char fab_name[64];
  int layer_count;
  double min_trace_width_mm;
  double min_clearance_mm;
  double min_drill_mm;
  double min_annular_ring_mm;
} FabRule;

DB *DB_open(const char *filename);
void DB_close(DB *db);

/* Fixed signature: 6 parameters matching db.c */
DBResult DB_InsertPart(DB *db, const char *mpn, PartTypes type, double value,
                       const char *package, int64_t *out_id);

double Tolerance_ToPercentage(ToleranceClass tolerance);

/* Ensure the 5th parameter is ToleranceClass, matching the implementation */
DBResult DB_FindClosestPart(DB *db, PartTypes type, double targetValue,
                            const char *package, ToleranceClass tolerance,
                            DBPart *result);

DBResult DB_InsertFabRule(DB *db, const char *fab_name, int layer_count,
                          double min_trace, double min_clearance,
                          double min_drill, double min_annular,
                          int64_t *out_id);
DBResult DB_GetFabRule(DB *db, const char *fab_name, FabRule *out_rule);

DBResult DB_BeginTransaction(DB *db);
DBResult DB_CommitTransaction(DB *db);
DBResult DB_RollbackTransaction(DB *db);

int DB_FindCandidates(DB *db, PartTypes type, double targetValue,
                      const char *package, ToleranceClass tolerance,
                      double min_v_rating, double min_i_rating,
                      double min_power_rating, DBPart *out_parts,
                      int max_results);

/* Insert a fully populated DBPart struct (handles the new parametric ratings)
 */
DBResult DB_InsertPartFull(DB *db, const DBPart *part, int64_t *out_id);

DBResult DB_InsertTopology(DB *db, const char *name, const char *description,
                           const char *category, int64_t *out_id);
DBResult DB_AddTopologyComponent(DB *db, int64_t topology_id,
                                 const char *role_name, PartTypes part_type,
                                 int quantity, int64_t *out_id);
DBResult DB_AddTopologyNode(DB *db, int64_t topology_id, const char *node_name,
                            int64_t *out_id);
DBResult DB_AddTopologyConnection(DB *db, int64_t component_id,
                                  const char *pin_name, int64_t node_id,
                                  int64_t *out_id);

typedef struct {
  int64_t id;
  char role_name[64];
  PartTypes part_type;
  int quantity;
} TopologyComponentRow;

typedef struct {
  int64_t id;
  char node_name[64];
} TopologyNodeRow;

typedef struct {
  char pin_name[32];
  char component_role[64];
  char node_name[64];
} TopologyConnectionRow;

DBResult DB_GetTopology(DB *db, const char *name,
                        TopologyComponentRow *out_components,
                        int max_components, int *out_component_count,
                        TopologyNodeRow *out_nodes, int max_nodes,
                        int *out_node_count,
                        TopologyConnectionRow *out_connections,
                        int max_connections, int *out_connection_count);

void DB_PrintAllParts(DB *db);

/* Copy all Parts rows from src_path into dest (UNIQUE mpn: skip duplicates). */
int DB_MergePartsFrom(DB *dest, const char *src_path);

/* Number of rows in Parts, or -1 on error. */
int DB_CountParts(DB *db);

#ifdef __cplusplus
}
#endif
