#ifndef COMPOSE_H
#define COMPOSE_H

#include "dfm_compose.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char id[64];
  char path[256];
} ComposeBlockRef;

typedef struct {
  ComposeBlockRef blocks[16];
  int block_count;
  int composition_ok;
} ComposeResult;

/* Load block JSON descriptors and validate a composition via DFM. */
int compose_from_block_ids(const char *const *block_ids, int count,
                           ComposeResult *out);

/* Gate 4 scenario: USB-C + LDO + MCU + I2C sensor + LED (5 blocks). */
int compose_gate4_scenario(ComposeResult *out);

/*
 * Merge block expand recipes into one schematic-ir JSON file.
 * Requires composition_ok. Returns 0 on success.
 */
int compose_expand_to_schematic(const ComposeResult *composition,
                                const char *out_path);

#ifdef __cplusplus
}
#endif

#endif
