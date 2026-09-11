#ifndef SEED_TOPOLOGY_H
#define SEED_TOPOLOGY_H

#include "db.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load topology + demo parts from a JSON seed file into an open DB.
 * Seed data lives under fixtures/seed/; never hardcoded machine paths.
 */
int seed_load_topology_json(DB *db, const char *json_path);

#ifdef __cplusplus
}
#endif

#endif
