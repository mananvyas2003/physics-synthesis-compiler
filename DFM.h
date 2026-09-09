#ifndef DFM_H
#define DFM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "db.h"
#include "physics2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * IDs
 * ============================================================ */

typedef uint32_t DFMPortId;
typedef uint32_t DFMBlockId;
typedef uint32_t DFMInstanceId;

/* ============================================================
 * Port system
 * ============================================================ */

typedef enum {
  DFM_PORT_NONE = 0,

  DFM_PORT_POWER_IN,
  DFM_PORT_POWER_OUT,

  DFM_PORT_SIGNAL_IN,
  DFM_PORT_SIGNAL_OUT,

  DFM_PORT_GND,

  DFM_PORT_I2C,
  DFM_PORT_UART,
  DFM_PORT_CAN
} DFMPortKind;

typedef struct {
  double min;
  double max;
} DFMRange;

typedef struct {
  DFMRange voltage;
  DFMRange current;
  double impedance_ohms;
} DFMElectricalLimits;

typedef struct {
  DFMPortId id;
  DFMPortKind kind;

  char name[64];

  DFMElectricalLimits electrical;
} DFMPort;

/* ============================================================
 * Block
 * ============================================================ */

typedef struct {
  DFMBlockId id;

  char name[64];

  DFMPort *ports;
  size_t port_count;
  size_t port_capacity;

  /* Internal physical implementation */
  /* Added when we define expansion */
} DFMBlock;

/* ============================================================
 * Block instances
 * ============================================================ */

typedef struct {
  DFMInstanceId id;
  DFMBlockId block_id;

  char name[64];
} DFMBlockInstance;

/* ============================================================
 * Composition
 * ============================================================ */

typedef struct {
  DFMInstanceId instance_a;
  DFMPortId port_a;

  DFMInstanceId instance_b;
  DFMPortId port_b;
} DFMConnection;

typedef struct {
  DFMBlockInstance *instances;
  size_t instance_count;
  size_t instance_capacity;

  DFMConnection *connections;
  size_t connection_count;
  size_t connection_capacity;
} DFMComposition;

/* ============================================================
 * Block pool
 * ============================================================ */

typedef struct {
  DFMBlock *blocks;
  size_t count;
  size_t capacity;
} DFMBlockPool;

/* ============================================================
 * Construction
 * ============================================================ */

void DFM_Init(DFMBlockPool *pool);

void DFM_Free(DFMBlockPool *pool);

DFMBlockId DFM_AddBlock(DFMBlockPool *pool, const char *name);

DFMPortId DFM_AddPort(DFMBlockPool *pool, DFMBlockId block_id, const char *name,
                      DFMPortKind kind, DFMElectricalLimits limits);

const DFMBlock *DFM_GetBlock(const DFMBlockPool *pool, DFMBlockId id);

/* ============================================================
 * Composition
 * ============================================================ */

void DFM_CompositionInit(DFMComposition *composition);

void DFM_CompositionFree(DFMComposition *composition);

DFMInstanceId DFM_AddInstance(DFMComposition *composition, DFMBlockId block_id,
                              const char *name);

bool DFM_Connect(DFMComposition *composition, DFMInstanceId instance_a,
                 DFMPortId port_a, DFMInstanceId instance_b, DFMPortId port_b);

/* ============================================================
 * Compatibility
 * ============================================================ */

bool DFM_PortsCompatible(const DFMPort *source, const DFMPort *sink);

bool DFM_ValidateComposition(const DFMBlockPool *pool,
                             const DFMComposition *composition);

#ifdef __cplusplus
}
#endif

#endif
