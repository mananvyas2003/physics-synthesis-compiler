#ifndef SEED_TOPOLOGY_H
#define SEED_TOPOLOGY_H

#include "db.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load topology (+ optional IR parts[]) from JSON into an open DB.
 * insert_ir_parts: 1 = seed Parts from IR (fixtures/offline);
 *                  0 = topology only — catalogue/DB is manufacturer truth.
 */
int seed_load_topology_json_ex(DB *db, const char *json_path,
                               int insert_ir_parts);
int seed_load_topology_json(DB *db, const char *json_path);

#ifdef __cplusplus
}
#endif

#endif
