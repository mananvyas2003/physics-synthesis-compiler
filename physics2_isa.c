#include "physics2_isa.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================
 * Parameter pool
 * ========================================================= */

void physics_param_pool_init(PhysicsParamPool *pool) {
  if (!pool)
    return;

  pool->values = NULL;
  pool->count = 0;
  pool->capacity = 0;
}

void physics_param_pool_free(PhysicsParamPool *pool) {
  if (!pool)
    return;

  free(pool->values);

  pool->values = NULL;
  pool->count = 0;
  pool->capacity = 0;
}

PhysicsParamId physics_param_add(PhysicsParamPool *pool, PhysicsValue value) {
  if (!pool)
    return PHYSICS_PARAM_NONE;

  if (pool->count == pool->capacity) {
    size_t new_capacity = pool->capacity ? pool->capacity * 2 : 16;

    if (new_capacity > SIZE_MAX / sizeof(*pool->values))
      return PHYSICS_PARAM_NONE;

    PhysicsValue *values =
        realloc(pool->values, new_capacity * sizeof(*pool->values));

    if (!values)
      return PHYSICS_PARAM_NONE;

    pool->values = values;
    pool->capacity = new_capacity;
  }

  pool->values[pool->count] = value;
  pool->count++;

  return (PhysicsParamId)pool->count;
}

const PhysicsValue *physics_param_get(const PhysicsParamPool *pool,
                                      PhysicsParamId id) {
  if (!pool || id == PHYSICS_PARAM_NONE)
    return NULL;

  if (id > pool->count)
    return NULL;

  return &pool->values[id - 1];
}

/* =========================================================
 * State pool
 * ========================================================= */

void physics_state_pool_init(PhysicsStatePool *pool) {
  if (!pool)
    return;

  pool->values = NULL;
  pool->count = 0;
  pool->capacity = 0;
}

void physics_state_pool_free(PhysicsStatePool *pool) {
  if (!pool)
    return;

  free(pool->values);

  pool->values = NULL;
  pool->count = 0;
  pool->capacity = 0;
}

PhysicsStateId physics_state_add(PhysicsStatePool *pool, size_t value_count) {
  if (!pool || value_count == 0)
    return PHYSICS_STATE_NONE;

  if (value_count > SIZE_MAX - pool->count)
    return PHYSICS_STATE_NONE;

  size_t offset = pool->count;
  size_t required = offset + value_count;

  if (required > pool->capacity) {
    size_t new_capacity = pool->capacity ? pool->capacity * 2 : 16;

    while (new_capacity < required) {
      if (new_capacity > SIZE_MAX / 2)
        return PHYSICS_STATE_NONE;

      new_capacity *= 2;
    }

    if (new_capacity > SIZE_MAX / sizeof(*pool->values))
      return PHYSICS_STATE_NONE;

    double *values =
        realloc(pool->values, new_capacity * sizeof(*pool->values));

    if (!values)
      return PHYSICS_STATE_NONE;

    pool->values = values;
    pool->capacity = new_capacity;
  }

  memset(pool->values + offset, 0, value_count * sizeof(*pool->values));

  pool->count = required;

  /*
   * State IDs are offsets encoded as +1 so zero remains NONE.
   */
  return (PhysicsStateId)(offset + 1);
}

double *physics_state_get(PhysicsStatePool *pool, PhysicsStateId id) {
  if (!pool || id == PHYSICS_STATE_NONE)
    return NULL;

  size_t offset = (size_t)id - 1;

  if (offset >= pool->count)
    return NULL;

  return &pool->values[offset];
}

/* =========================================================
 * Opcode helpers
 * ========================================================= */

const char *physics_opcode_name(PhysicsOpcode opcode) {
  switch (opcode) {
  case PHYS_OP_NONE:
    return "NONE";

  case PHYS_OP_RESISTOR:
    return "RESISTOR";

  case PHYS_OP_CAPACITOR:
    return "CAPACITOR";

  case PHYS_OP_INDUCTOR:
    return "INDUCTOR";

  case PHYS_OP_VSOURCE:
    return "VSOURCE";

  case PHYS_OP_ISOURCE:
    return "ISOURCE";

  case PHYS_OP_VCVS:
    return "VCVS";

  case PHYS_OP_VCCS:
    return "VCCS";

  case PHYS_OP_CCVS:
    return "CCVS";

  case PHYS_OP_CCCS:
    return "CCCS";

  case PHYS_OP_DIODE:
    return "DIODE";

  case PHYS_OP_TRANSISTOR:
    return "TRANSISTOR";

  case PHYS_OP_LOGIC_GATE:
    return "LOGIC_GATE";

  case PHYS_OP_SWITCH:
    return "SWITCH";

  case PHYS_OP_OPAMP:
    return "OPAMP";

  case PHYS_OP_LDO:
    return "LDO";

  case PHYS_OP_BATTERY:
    return "BATTERY";

  default:
    return "UNKNOWN";
  }
}

bool physics_opcode_valid(PhysicsOpcode opcode) {
  return opcode > PHYS_OP_NONE && opcode < PHYS_OP_COUNT;
}
