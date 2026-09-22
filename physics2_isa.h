#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* =========================================================
 * Runtime IDs
 * ========================================================= */

typedef uint32_t NodeId;
typedef uint32_t PrimitiveId;
typedef uint32_t BranchId;
typedef uint32_t PhysicsParamId;
typedef uint32_t PhysicsStateId;

#define PHYSICS_NODE_NONE ((NodeId)0xFFFFFFFFu)
#define PHYSICS_PRIMITIVE_NONE ((PrimitiveId)0u)
#define PHYSICS_BRANCH_NONE ((BranchId)0xFFFFFFFFu)
#define PHYSICS_PARAM_NONE ((PhysicsParamId)0u)
#define PHYSICS_STATE_NONE ((PhysicsStateId)0u)

/* =========================================================
 * Physical value
 * ========================================================= */

typedef struct {
  double nominal;
  double tolerance_pct;
} PhysicsValue;

/* =========================================================
 * Physics opcodes
 * ========================================================= */

typedef enum {
  PHYS_OP_NONE = 0,

  PHYS_OP_RESISTOR,
  PHYS_OP_CAPACITOR,
  PHYS_OP_INDUCTOR,

  PHYS_OP_VSOURCE,
  PHYS_OP_ISOURCE,

  PHYS_OP_VCVS,
  PHYS_OP_VCCS,
  PHYS_OP_CCVS,
  PHYS_OP_CCCS,

  PHYS_OP_DIODE,
  PHYS_OP_TRANSISTOR,
  PHYS_OP_LOGIC_GATE,

  PHYS_OP_SWITCH,
  PHYS_OP_OPAMP,
  PHYS_OP_LDO,
  PHYS_OP_BATTERY,

  PHYS_OP_COUNT
} PhysicsOpcode;

/* =========================================================
 * ISA instruction
 * ========================================================= */

#define PHYSICS2_MAX_TERMINALS 8

typedef struct {
  PhysicsOpcode opcode;

  PrimitiveId primitive_id;

  uint8_t terminal_count;
  NodeId terminals[PHYSICS2_MAX_TERMINALS];

  BranchId branch;

  PhysicsParamId parameter_id;
  PhysicsStateId state_id;

  uint32_t flags;
} PhysicsInstruction;

/* =========================================================
 * Parameter pool
 * ========================================================= */

typedef struct {
  PhysicsValue *values;
  size_t count;
  size_t capacity;
} PhysicsParamPool;

/* =========================================================
 * State pool
 * ========================================================= */

typedef struct {
  double *values;
  size_t count;
  size_t capacity;
} PhysicsStatePool;

/* =========================================================
 * Parameter pool API
 * ========================================================= */

void physics_param_pool_init(PhysicsParamPool *pool);
void physics_param_pool_free(PhysicsParamPool *pool);

PhysicsParamId physics_param_add(PhysicsParamPool *pool, PhysicsValue value);

const PhysicsValue *physics_param_get(const PhysicsParamPool *pool,
                                      PhysicsParamId id);

/* =========================================================
 * State pool API
 * ========================================================= */

void physics_state_pool_init(PhysicsStatePool *pool);
void physics_state_pool_free(PhysicsStatePool *pool);

PhysicsStateId physics_state_add(PhysicsStatePool *pool, size_t value_count);

double *physics_state_get(PhysicsStatePool *pool, PhysicsStateId id);

/* =========================================================
 * ISA helpers
 * ========================================================= */

const char *physics_opcode_name(PhysicsOpcode opcode);

bool physics_opcode_valid(PhysicsOpcode opcode);
