#include "dfm_compose.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Internal helpers
 * ============================================================ */

static char *dfm_copy_string(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0)
    return NULL;

  if (!src)
    src = "";

  strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';

  return dst;
}

static bool grow_array(void **ptr, size_t *capacity, size_t element_size,
                       size_t required) {
  if (!ptr || !capacity || element_size == 0)
    return false;

  if (required <= *capacity)
    return true;

  size_t new_capacity = (*capacity == 0) ? 4 : *capacity;

  while (new_capacity < required) {
    if (new_capacity > SIZE_MAX / 2)
      return false;

    new_capacity *= 2;
  }

  if (new_capacity > SIZE_MAX / element_size)
    return false;

  void *new_ptr = realloc(*ptr, new_capacity * element_size);

  if (!new_ptr)
    return false;

  *ptr = new_ptr;
  *capacity = new_capacity;

  return true;
}

static DFMBlock *get_block_mutable(DFMBlockPool *pool, DFMBlockId id) {
  if (!pool || id == 0)
    return NULL;

  size_t index = (size_t)(id - 1);

  if (index >= pool->count)
    return NULL;

  return &pool->blocks[index];
}

static const DFMBlock *get_block_const(const DFMBlockPool *pool,
                                       DFMBlockId id) {
  if (!pool || id == 0)
    return NULL;

  size_t index = (size_t)(id - 1);

  if (index >= pool->count)
    return NULL;

  return &pool->blocks[index];
}

static const DFMPort *get_port_const(const DFMBlock *block, DFMPortId id) {
  if (!block || id == 0)
    return NULL;

  size_t index = (size_t)(id - 1);

  if (index >= block->port_count)
    return NULL;

  return &block->ports[index];
}

static const DFMBlockInstance *
get_instance_const(const DFMComposition *composition, DFMInstanceId id) {
  if (!composition || id == 0)
    return NULL;

  size_t index = (size_t)(id - 1);

  if (index >= composition->instance_count)
    return NULL;

  return &composition->instances[index];
}

static bool port_is_source(DFMPortKind kind) {
  switch (kind) {
  case DFM_PORT_POWER_OUT:
  case DFM_PORT_SIGNAL_OUT:
    return true;

  default:
    return false;
  }
}

static bool port_is_sink(DFMPortKind kind) {
  switch (kind) {
  case DFM_PORT_POWER_IN:
  case DFM_PORT_SIGNAL_IN:
    return true;

  default:
    return false;
  }
}

/* ============================================================
 * Block pool
 * ============================================================ */

void DFM_Init(DFMBlockPool *pool) {
  if (!pool)
    return;

  memset(pool, 0, sizeof(*pool));
}

void DFM_Free(DFMBlockPool *pool) {
  if (!pool)
    return;

  for (size_t i = 0; i < pool->count; ++i) {
    free(pool->blocks[i].ports);
    pool->blocks[i].ports = NULL;
    pool->blocks[i].port_count = 0;
    pool->blocks[i].port_capacity = 0;
  }

  free(pool->blocks);

  memset(pool, 0, sizeof(*pool));
}

DFMBlockId DFM_AddBlock(DFMBlockPool *pool, const char *name) {
  if (!pool)
    return 0;

  size_t required = pool->count + 1;

  if (!grow_array((void **)&pool->blocks, &pool->capacity, sizeof(DFMBlock),
                  required))
    return 0;

  DFMBlock *block = &pool->blocks[pool->count];

  memset(block, 0, sizeof(*block));

  block->id = (DFMBlockId)required;

  dfm_copy_string(block->name, sizeof(block->name), name);

  pool->count++;

  return block->id;
}

DFMPortId DFM_AddPort(DFMBlockPool *pool, DFMBlockId block_id, const char *name,
                      DFMPortKind kind, DFMElectricalLimits limits) {
  DFMBlock *block = get_block_mutable(pool, block_id);

  if (!block)
    return 0;

  size_t required = block->port_count + 1;

  if (!grow_array((void **)&block->ports, &block->port_capacity,
                  sizeof(DFMPort), required))
    return 0;

  DFMPort *port = &block->ports[block->port_count];

  memset(port, 0, sizeof(*port));

  /*
   * Port IDs are local to their block.
   * The pair (block instance, port id) uniquely
   * identifies a port during composition.
   */
  port->id = (DFMPortId)required;
  port->kind = kind;
  port->electrical = limits;

  dfm_copy_string(port->name, sizeof(port->name), name);

  block->port_count++;

  return port->id;
}

const DFMBlock *DFM_GetBlock(const DFMBlockPool *pool, DFMBlockId id) {
  return get_block_const(pool, id);
}

/* ============================================================
 * Composition
 * ============================================================ */

void DFM_CompositionInit(DFMComposition *composition) {
  if (!composition)
    return;

  memset(composition, 0, sizeof(*composition));
}

void DFM_CompositionFree(DFMComposition *composition) {
  if (!composition)
    return;

  free(composition->instances);
  free(composition->connections);

  memset(composition, 0, sizeof(*composition));
}

DFMInstanceId DFM_AddInstance(DFMComposition *composition, DFMBlockId block_id,
                              const char *name) {
  if (!composition || block_id == 0)
    return 0;

  size_t required = composition->instance_count + 1;

  if (!grow_array((void **)&composition->instances,
                  &composition->instance_capacity, sizeof(DFMBlockInstance),
                  required))
    return 0;

  DFMBlockInstance *instance =
      &composition->instances[composition->instance_count];

  memset(instance, 0, sizeof(*instance));

  instance->id = (DFMInstanceId)required;
  instance->block_id = block_id;

  dfm_copy_string(instance->name, sizeof(instance->name), name);

  composition->instance_count++;

  return instance->id;
}

bool DFM_Connect(DFMComposition *composition, DFMInstanceId instance_a,
                 DFMPortId port_a, DFMInstanceId instance_b, DFMPortId port_b) {
  if (!composition)
    return false;

  if (instance_a == 0 || instance_b == 0 || port_a == 0 || port_b == 0)
    return false;

  size_t required = composition->connection_count + 1;

  if (!grow_array((void **)&composition->connections,
                  &composition->connection_capacity, sizeof(DFMConnection),
                  required))
    return false;

  DFMConnection *connection =
      &composition->connections[composition->connection_count];

  connection->instance_a = instance_a;
  connection->port_a = port_a;
  connection->instance_b = instance_b;
  connection->port_b = port_b;

  composition->connection_count++;

  return true;
}

/* ============================================================
 * Port compatibility
 * ============================================================ */

bool DFM_PortsCompatible(const DFMPort *source, const DFMPort *sink) {
  if (!source || !sink)
    return false;

  /*
   * Ground is special.
   */
  if (source->kind == DFM_PORT_GND || sink->kind == DFM_PORT_GND) {
    return source->kind == DFM_PORT_GND && sink->kind == DFM_PORT_GND;
  }

  /*
   * I2C is inherently bidirectional.
   * For the first implementation, matching I2C
   * ports are considered compatible.
   */
  if (source->kind == DFM_PORT_I2C || sink->kind == DFM_PORT_I2C) {
    return source->kind == DFM_PORT_I2C && sink->kind == DFM_PORT_I2C;
  }

  /*
   * UART and CAN currently require matching
   * interface types.
   */
  if (source->kind == DFM_PORT_UART || sink->kind == DFM_PORT_UART) {
    return source->kind == DFM_PORT_UART && sink->kind == DFM_PORT_UART;
  }

  if (source->kind == DFM_PORT_CAN || sink->kind == DFM_PORT_CAN) {
    return source->kind == DFM_PORT_CAN && sink->kind == DFM_PORT_CAN;
  }

  /*
   * Normal directed connection.
   */
  if (!port_is_source(source->kind) || !port_is_sink(sink->kind))
    return false;

  /*
   * Voltage ranges must overlap.
   */
  if (source->electrical.voltage.max < sink->electrical.voltage.min)
    return false;

  if (sink->electrical.voltage.max < source->electrical.voltage.min)
    return false;

  /*
   * Source must be capable of supplying
   * the sink's minimum required current.
   */
  if (source->electrical.current.max < sink->electrical.current.min)
    return false;

  /*
   * If both impedances are specified,
   * a source should not be higher impedance
   * than the sink requirement.
   *
   * Zero means "unspecified".
   */
  if (source->electrical.impedance_ohms > 0.0 &&
      sink->electrical.impedance_ohms > 0.0) {
    if (source->electrical.impedance_ohms > sink->electrical.impedance_ohms)
      return false;
  }

  return true;
}

/* ============================================================
 * Composition validation
 * ============================================================ */

bool DFM_ValidateComposition(const DFMBlockPool *pool,
                             const DFMComposition *composition) {
  if (!pool || !composition)
    return false;

  /*
   * First validate every instance.
   */
  for (size_t i = 0; i < composition->instance_count; ++i) {

    const DFMBlockInstance *instance = &composition->instances[i];

    if (!get_block_const(pool, instance->block_id))
      return false;
  }

  /*
   * Validate every connection.
   */
  for (size_t i = 0; i < composition->connection_count; ++i) {

    const DFMConnection *connection = &composition->connections[i];

    const DFMBlockInstance *instance_a =
        get_instance_const(composition, connection->instance_a);

    const DFMBlockInstance *instance_b =
        get_instance_const(composition, connection->instance_b);

    if (!instance_a || !instance_b)
      return false;

    const DFMBlock *block_a = get_block_const(pool, instance_a->block_id);

    const DFMBlock *block_b = get_block_const(pool, instance_b->block_id);

    if (!block_a || !block_b)
      return false;

    const DFMPort *port_a = get_port_const(block_a, connection->port_a);

    const DFMPort *port_b = get_port_const(block_b, connection->port_b);

    if (!port_a || !port_b)
      return false;

    /*
     * Determine connection direction.
     */
    bool a_source = port_is_source(port_a->kind);
    bool b_source = port_is_source(port_b->kind);

    bool a_sink = port_is_sink(port_a->kind);
    bool b_sink = port_is_sink(port_b->kind);

    /*
     * Ground and bidirectional interfaces.
     */
    if (port_a->kind == DFM_PORT_GND || port_b->kind == DFM_PORT_GND) {

      if (port_a->kind != DFM_PORT_GND || port_b->kind != DFM_PORT_GND)
        return false;

      continue;
    }

    if (port_a->kind == DFM_PORT_I2C || port_b->kind == DFM_PORT_I2C) {

      if (!DFM_PortsCompatible(port_a, port_b))
        return false;

      continue;
    }

    if (port_a->kind == DFM_PORT_UART || port_b->kind == DFM_PORT_UART) {

      if (!DFM_PortsCompatible(port_a, port_b))
        return false;

      continue;
    }

    if (port_a->kind == DFM_PORT_CAN || port_b->kind == DFM_PORT_CAN) {

      if (!DFM_PortsCompatible(port_a, port_b))
        return false;

      continue;
    }

    /*
     * Exactly one side must be a source
     * and exactly one side must be a sink.
     */
    if (a_source && b_sink) {
      if (!DFM_PortsCompatible(port_a, port_b))
        return false;
    } else if (b_source && a_sink) {
      if (!DFM_PortsCompatible(port_b, port_a))
        return false;
    } else {
      return false;
    }
  }

  return true;
}
