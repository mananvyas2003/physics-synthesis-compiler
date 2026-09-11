#ifndef COMPOSE_H
#define COMPOSE_H

#include "DFM.h"

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

#ifdef __cplusplus
}
#endif

#endif
