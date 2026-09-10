#include "physics2_interpreter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
 * Primitive Registry Implementation
 * ========================================================= */

void physics2_registry_init(PhysicsPrimitiveRegistry *registry) {
  if (!registry)
    return;

  registry->items = NULL;
  registry->count = 0;
  registry->capacity = 0;
}

void physics2_registry_free(PhysicsPrimitiveRegistry *registry) {
  if (!registry)
    return;

  free(registry->items);

  registry->items = NULL;
  registry->count = 0;
  registry->capacity = 0;
}

bool physics2_registry_add(PhysicsPrimitiveRegistry *registry,
                           PhysicsPrimitive *primitive, PrimitiveId *out_id) {
  if (!registry || !primitive || !out_id)
    return false;

  if (registry->count == registry->capacity) {
    size_t new_capacity = registry->capacity ? registry->capacity * 2 : 16;

    if (new_capacity > SIZE_MAX / sizeof(*registry->items))
      return false;

    PhysicsPrimitive **items =
        realloc(registry->items, new_capacity * sizeof(*registry->items));

    if (!items)
      return false;

    registry->items = items;
    registry->capacity = new_capacity;
  }

  registry->items[registry->count] = primitive;
  registry->count++;

  *out_id = (PrimitiveId)registry->count;

  return true;
}

PhysicsPrimitive *
physics2_registry_get(const PhysicsPrimitiveRegistry *registry,
                      PrimitiveId id) {
  if (!registry || id == PHYSICS_PRIMITIVE_NONE)
    return NULL;

  if ((size_t)id > registry->count)
    return NULL;

  return registry->items[id - 1];
}

/* =========================================================
 * Accumulator
 * ========================================================= */

struct PhysicsAccumulator {
  size_t size;
  double *matrix;
  double *rhs;
};

static size_t accumulator_index(const PhysicsAccumulator *accumulator,
                                size_t row, size_t column) {
  return row * accumulator->size + column;
}

PhysicsAccumulator *physics2_accumulator_create(size_t size) {
  PhysicsAccumulator *accumulator;

  if (size == 0)
    return NULL;

  accumulator = calloc(1, sizeof(*accumulator));
  if (!accumulator)
    return NULL;

  if (size > SIZE_MAX / size)
    goto fail;

  accumulator->matrix = calloc(size * size, sizeof(*accumulator->matrix));
  accumulator->rhs = calloc(size, sizeof(*accumulator->rhs));

  if (!accumulator->matrix || !accumulator->rhs)
    goto fail;

  accumulator->size = size;

  return accumulator;

fail:
  free(accumulator->matrix);
  free(accumulator->rhs);
  free(accumulator);

  return NULL;
}

void physics2_accumulator_free(PhysicsAccumulator *accumulator) {
  if (!accumulator)
    return;

  free(accumulator->matrix);
  free(accumulator->rhs);
  free(accumulator);
}

void physics2_accumulator_clear(PhysicsAccumulator *accumulator) {
  if (!accumulator)
    return;

  memset(accumulator->matrix, 0,
         accumulator->size * accumulator->size * sizeof(*accumulator->matrix));
  memset(accumulator->rhs, 0, accumulator->size * sizeof(*accumulator->rhs));
}

size_t physics2_accumulator_size(const PhysicsAccumulator *accumulator) {
  if (!accumulator)
    return 0;

  return accumulator->size;
}

bool physics2_accumulator_add(PhysicsAccumulator *accumulator, size_t row,
                              size_t column, double value) {
  if (!accumulator)
    return false;

  if (row >= accumulator->size || column >= accumulator->size)
    return false;

  accumulator->matrix[accumulator_index(accumulator, row, column)] += value;

  return true;
}

bool physics2_accumulator_add_rhs(PhysicsAccumulator *accumulator, size_t row,
                                  double value) {
  if (!accumulator || row >= accumulator->size)
    return false;

  accumulator->rhs[row] += value;

  return true;
}

double physics2_accumulator_get(const PhysicsAccumulator *accumulator,
                                size_t row, size_t column) {
  if (!accumulator)
    return 0.0;

  if (row >= accumulator->size || column >= accumulator->size)
    return 0.0;

  return accumulator->matrix[accumulator_index(accumulator, row, column)];
}

double physics2_accumulator_get_rhs(const PhysicsAccumulator *accumulator,
                                    size_t row) {
  if (!accumulator || row >= accumulator->size)
    return 0.0;

  return accumulator->rhs[row];
}

void physics2_accumulator_print(const PhysicsAccumulator *accumulator,
                                FILE *stream) {
  size_t row;
  size_t column;

  if (!accumulator || !stream)
    return;

  fprintf(stream, "Matrix:\n");

  for (row = 0; row < accumulator->size; row++) {
    for (column = 0; column < accumulator->size; column++) {
      fprintf(stream, "%12.6g ",
              physics2_accumulator_get(accumulator, row, column));
    }

    fprintf(stream, " | %12.6g\n",
            physics2_accumulator_get_rhs(accumulator, row));
  }
}

/* =========================================================
 * Linear solver
 * ========================================================= */

bool physics2_accumulator_solve(const PhysicsAccumulator *accumulator,
                                NodeId reference_node, double *solution) {
  size_t n;
  size_t i;
  size_t j;
  size_t k;
  double *a;
  double *b;

  if (!accumulator || !solution)
    return false;

  n = accumulator->size;

  if (n == 0)
    return false;

  if (reference_node >= n)
    return false;

  a = malloc(n * n * sizeof(*a));
  b = malloc(n * sizeof(*b));

  if (!a || !b) {
    free(a);
    free(b);
    return false;
  }

  memcpy(a, accumulator->matrix, n * n * sizeof(*a));
  memcpy(b, accumulator->rhs, n * sizeof(*b));

  for (j = 0; j < n; j++)
    a[reference_node * n + j] = 0.0;

  for (i = 0; i < n; i++)
    a[i * n + reference_node] = 0.0;

  a[reference_node * n + reference_node] = 1.0;
  b[reference_node] = 0.0;

  for (k = 0; k < n; k++) {
    size_t pivot = k;
    double max_value = fabs(a[k * n + k]);

    for (i = k + 1; i < n; i++) {
      double value = fabs(a[i * n + k]);

      if (value > max_value) {
        max_value = value;
        pivot = i;
      }
    }

    if (max_value < 1e-14) {
      free(a);
      free(b);
      return false;
    }

    if (pivot != k) {
      for (j = 0; j < n; j++) {
        double tmp = a[k * n + j];
        a[k * n + j] = a[pivot * n + j];
        a[pivot * n + j] = tmp;
      }

      {
        double tmp = b[k];
        b[k] = b[pivot];
        b[pivot] = tmp;
      }
    }

    for (i = k + 1; i < n; i++) {
      double factor = a[i * n + k] / a[k * n + k];

      if (factor == 0.0)
        continue;

      a[i * n + k] = 0.0;

      for (j = k + 1; j < n; j++) {
        a[i * n + j] -= factor * a[k * n + j];
      }

      b[i] -= factor * b[k];
    }
  }

  for (i = n; i-- > 0;) {
    double sum = b[i];

    for (j = i + 1; j < n; j++) {
      sum -= a[i * n + j] * solution[j];
    }

    if (fabs(a[i * n + i]) < 1e-14) {
      free(a);
      free(b);
      return false;
    }

    solution[i] = sum / a[i * n + i];
  }

  free(a);
  free(b);

  return true;
}

/* =========================================================
 * Resistor
 * ========================================================= */

static bool resistor_stamp(const PhysicsPrimitive *primitive,
                           PhysicsExecutionContext *context,
                           const NodeId *terminals, uint8_t terminal_count,
                           BranchId branch) {
  double resistance;
  double conductance;

  (void)branch;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 2)
    return false;

  resistance = primitive->payload.two_terminal.value.nominal;

  if (!isfinite(resistance) || resistance <= 0.0)
    return false;

  conductance = 1.0 / resistance;

  if (!physics2_accumulator_add(context->accumulator, terminals[0],
                                terminals[0], conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[0],
                                terminals[1], -conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[1],
                                terminals[0], -conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[1],
                                terminals[1], conductance))
    return false;

  return true;
}

static uint8_t resistor_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t resistor_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void resistor_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: RESISTOR %.12g ohm +/- %.6g%%\n",
          primitive->name, primitive->payload.two_terminal.value.nominal,
          primitive->payload.two_terminal.value.tolerance_pct);
}

static const PhysicsPrimitiveOps resistor_ops = {
    resistor_stamp, resistor_terminal_count, resistor_extra_unknowns,
    resistor_print};

/* =========================================================
 * Capacitor
 * ========================================================= */

static bool capacitor_stamp(const PhysicsPrimitive *primitive,
                            PhysicsExecutionContext *context,
                            const NodeId *terminals, uint8_t terminal_count,
                            BranchId branch) {
  double capacitance;
  double conductance;
  double previous_voltage;
  size_t state_index;

  (void)branch;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 2)
    return false;

  if (context->timestep <= 0.0)
    return false;

  capacitance = primitive->payload.two_terminal.value.nominal;

  if (!isfinite(capacitance) || capacitance <= 0.0)
    return false;

  if (context->program == NULL)
    return false;

  state_index = SIZE_MAX;

  for (size_t i = 0; i < context->program->instruction_count; i++) {
    PhysicsPrimitive *inst_prim =
        physics2_registry_get(&context->program->primitives,
                              context->program->instructions[i].primitive_id);

    if (inst_prim == primitive) {
      state_index = i;
      break;
    }
  }

  if (state_index == SIZE_MAX || state_index >= context->state_count)
    return false;

  previous_voltage = context->states[state_index].capacitor_previous_voltage;

  conductance = capacitance / context->timestep;

  if (!physics2_accumulator_add(context->accumulator, terminals[0],
                                terminals[0], conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[0],
                                terminals[1], -conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[1],
                                terminals[0], -conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[1],
                                terminals[1], conductance))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[0],
                                    conductance * previous_voltage))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[1],
                                    -conductance * previous_voltage))
    return false;

  return true;
}

static uint8_t capacitor_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t capacitor_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void capacitor_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: CAPACITOR %.12g F +/- %.6g%%\n",
          primitive->name, primitive->payload.two_terminal.value.nominal,
          primitive->payload.two_terminal.value.tolerance_pct);
}

static const PhysicsPrimitiveOps capacitor_ops = {
    capacitor_stamp, capacitor_terminal_count, capacitor_extra_unknowns,
    capacitor_print};

/* =========================================================
 * Voltage source
 * ========================================================= */

static bool vsource_stamp(const PhysicsPrimitive *primitive,
                          PhysicsExecutionContext *context,
                          const NodeId *terminals, uint8_t terminal_count,
                          BranchId branch) {
  size_t p;
  size_t n;
  size_t b;
  double voltage;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 2)
    return false;

  if (branch == PHYSICS2_BRANCH_NONE)
    return false;

  p = terminals[0];
  n = terminals[1];
  b = branch;

  if (b >= physics2_accumulator_size(context->accumulator))
    return false;

  voltage = primitive->payload.two_terminal.value.nominal;

  if (!isfinite(voltage))
    return false;

  if (!physics2_accumulator_add(context->accumulator, p, b, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, p, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, n, b, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, n, -1.0))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, b, voltage))
    return false;

  return true;
}

static uint8_t vsource_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t vsource_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 1;
}

static void vsource_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: VSOURCE %.12g V +/- %.6g%%\n", primitive->name,
          primitive->payload.two_terminal.value.nominal,
          primitive->payload.two_terminal.value.tolerance_pct);
}

static const PhysicsPrimitiveOps vsource_ops = {
    vsource_stamp, vsource_terminal_count, vsource_extra_unknowns,
    vsource_print};

/* =========================================================
 * Inductor (Backward Euler companion, matches capacitor )
 *
 * i = i_prev + (dt/L) * (vp - vn)
 * G = dt/L
 * ========================================================= */

static bool inductor_find_state_index(const PhysicsPrimitive *primitive,
                                      PhysicsExecutionContext *context,
                                      size_t *out_index) {
  size_t i;

  if (!primitive || !context || !context->program || !out_index)
    return false;

  for (i = 0; i < context->program->instruction_count; i++) {
    PhysicsPrimitive *inst_prim =
        physics2_registry_get(&context->program->primitives,
                              context->program->instructions[i].primitive_id);

    if (inst_prim == primitive) {
      *out_index = i;
      return true;
    }
  }

  return false;
}

static bool inductor_stamp(const PhysicsPrimitive *primitive,
                           PhysicsExecutionContext *context,
                           const NodeId *terminals, uint8_t terminal_count,
                           BranchId branch) {
  double inductance;
  double conductance;
  double previous_current;
  size_t state_index;

  (void)branch;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 2)
    return false;

  if (context->timestep <= 0.0)
    return false;

  inductance = primitive->payload.two_terminal.value.nominal;

  if (!isfinite(inductance) || inductance <= 0.0)
    return false;

  if (!inductor_find_state_index(primitive, context, &state_index))
    return false;

  if (state_index >= context->state_count)
    return false;

  previous_current = context->states[state_index].inductor_previous_current;

  conductance = context->timestep / inductance;

  if (!physics2_accumulator_add(context->accumulator, terminals[0],
                                terminals[0], conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[0],
                                terminals[1], -conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[1],
                                terminals[0], -conductance))
    return false;

  if (!physics2_accumulator_add(context->accumulator, terminals[1],
                                terminals[1], conductance))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[0],
                                    -previous_current))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[1],
                                    previous_current))
    return false;

  return true;
}

static uint8_t inductor_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t inductor_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void inductor_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: INDUCTOR %.12g H +/- %.6g%%\n",
          primitive->name, primitive->payload.two_terminal.value.nominal,
          primitive->payload.two_terminal.value.tolerance_pct);
}

static const PhysicsPrimitiveOps inductor_ops = {
    inductor_stamp, inductor_terminal_count, inductor_extra_unknowns,
    inductor_print};

/* =========================================================
 * Independent current source
 *
 * Current I flows from terminals[0] to terminals[1].
 * ========================================================= */

static bool isource_stamp(const PhysicsPrimitive *primitive,
                          PhysicsExecutionContext *context,
                          const NodeId *terminals, uint8_t terminal_count,
                          BranchId branch) {
  double current;

  (void)branch;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 2)
    return false;

  if (terminals[0] == PHYSICS2_NODE_NONE || terminals[1] == PHYSICS2_NODE_NONE)
    return false;

  current = primitive->payload.two_terminal.value.nominal;

  if (!isfinite(current))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[0],
                                    -current))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[1],
                                    current))
    return false;

  return true;
}

static uint8_t isource_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t isource_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void isource_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: ISOURCE %.12g A +/- %.6g%%\n", primitive->name,
          primitive->payload.two_terminal.value.nominal,
          primitive->payload.two_terminal.value.tolerance_pct);
}

static const PhysicsPrimitiveOps isource_ops = {
    isource_stamp, isource_terminal_count, isource_extra_unknowns,
    isource_print};

/* =========================================================
 * Controlled sources
 *
 * terminals[0], terminals[1] = output (+, -)
 * terminals[2], terminals[3] = control (+, -)
 *
 * Branch unknowns (when required) are packed contiguously:
 *   instruction.branch + 0, instruction.branch + 1, ...
 * Sign convention matches VSOURCE: branch current flows
 * from the first terminal of that port to the second.
 * ========================================================= */

static bool controlled_nodes_valid(const NodeId *terminals,
                                   PhysicsExecutionContext *context) {
  size_t size;
  uint8_t i;

  if (!terminals || !context)
    return false;

  size = physics2_accumulator_size(context->accumulator);

  for (i = 0; i < 4; i++) {
    if (terminals[i] == PHYSICS2_NODE_NONE || terminals[i] >= size)
      return false;
  }

  return true;
}

static bool vcvs_stamp(const PhysicsPrimitive *primitive,
                       PhysicsExecutionContext *context, const NodeId *terminals,
                       uint8_t terminal_count, BranchId branch) {
  NodeId p;
  NodeId n;
  NodeId cp;
  NodeId cn;
  size_t b;
  double mu;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 4)
    return false;

  if (branch == PHYSICS2_BRANCH_NONE)
    return false;

  if (!controlled_nodes_valid(terminals, context))
    return false;

  p = terminals[0];
  n = terminals[1];
  cp = terminals[2];
  cn = terminals[3];
  b = branch;

  if (b >= physics2_accumulator_size(context->accumulator))
    return false;

  mu = primitive->payload.controlled_source.gain.nominal;

  if (!isfinite(mu))
    return false;

  if (!physics2_accumulator_add(context->accumulator, p, b, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, n, b, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, p, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, n, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, cp, -mu))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, cn, mu))
    return false;

  return true;
}

static uint8_t vcvs_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 4;
}

static uint8_t vcvs_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 1;
}

static void vcvs_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: VCVS gain %.12g +/- %.6g%%\n", primitive->name,
          primitive->payload.controlled_source.gain.nominal,
          primitive->payload.controlled_source.gain.tolerance_pct);
}

static const PhysicsPrimitiveOps vcvs_ops = {
    vcvs_stamp, vcvs_terminal_count, vcvs_extra_unknowns, vcvs_print};

static bool vccs_stamp(const PhysicsPrimitive *primitive,
                       PhysicsExecutionContext *context, const NodeId *terminals,
                       uint8_t terminal_count, BranchId branch) {
  NodeId p;
  NodeId n;
  NodeId cp;
  NodeId cn;
  double gm;

  (void)branch;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 4)
    return false;

  if (!controlled_nodes_valid(terminals, context))
    return false;

  p = terminals[0];
  n = terminals[1];
  cp = terminals[2];
  cn = terminals[3];

  gm = primitive->payload.controlled_source.gain.nominal;

  if (!isfinite(gm))
    return false;

  if (!physics2_accumulator_add(context->accumulator, p, cp, gm))
    return false;

  if (!physics2_accumulator_add(context->accumulator, p, cn, -gm))
    return false;

  if (!physics2_accumulator_add(context->accumulator, n, cp, -gm))
    return false;

  if (!physics2_accumulator_add(context->accumulator, n, cn, gm))
    return false;

  return true;
}

static uint8_t vccs_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 4;
}

static uint8_t vccs_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void vccs_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: VCCS gm %.12g S +/- %.6g%%\n", primitive->name,
          primitive->payload.controlled_source.gain.nominal,
          primitive->payload.controlled_source.gain.tolerance_pct);
}

static const PhysicsPrimitiveOps vccs_ops = {
    vccs_stamp, vccs_terminal_count, vccs_extra_unknowns, vccs_print};

static bool ccvs_stamp(const PhysicsPrimitive *primitive,
                       PhysicsExecutionContext *context, const NodeId *terminals,
                       uint8_t terminal_count, BranchId branch) {
  NodeId p;
  NodeId n;
  NodeId cp;
  NodeId cn;
  size_t bs;
  size_t bo;
  double rm;
  size_t size;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 4)
    return false;

  if (branch == PHYSICS2_BRANCH_NONE)
    return false;

  if (!controlled_nodes_valid(terminals, context))
    return false;

  p = terminals[0];
  n = terminals[1];
  cp = terminals[2];
  cn = terminals[3];
  bs = branch;
  bo = (size_t)branch + 1u;
  size = physics2_accumulator_size(context->accumulator);

  if (bs >= size || bo >= size)
    return false;

  rm = primitive->payload.controlled_source.gain.nominal;

  if (!isfinite(rm))
    return false;

  /* Sense short between control terminals; current bs flows cp -> cn. */
  if (!physics2_accumulator_add(context->accumulator, cp, bs, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, cn, bs, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, bs, cp, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, bs, cn, -1.0))
    return false;

  /* Output voltage: Vp - Vn - rm * i_sense = 0. */
  if (!physics2_accumulator_add(context->accumulator, p, bo, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, n, bo, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, bo, p, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, bo, n, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, bo, bs, -rm))
    return false;

  return true;
}

static uint8_t ccvs_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 4;
}

static uint8_t ccvs_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static void ccvs_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: CCVS rm %.12g ohm +/- %.6g%%\n",
          primitive->name, primitive->payload.controlled_source.gain.nominal,
          primitive->payload.controlled_source.gain.tolerance_pct);
}

static const PhysicsPrimitiveOps ccvs_ops = {
    ccvs_stamp, ccvs_terminal_count, ccvs_extra_unknowns, ccvs_print};

static bool cccs_stamp(const PhysicsPrimitive *primitive,
                       PhysicsExecutionContext *context, const NodeId *terminals,
                       uint8_t terminal_count, BranchId branch) {
  NodeId p;
  NodeId n;
  NodeId cp;
  NodeId cn;
  size_t b;
  double beta;

  if (!primitive || !context || !terminals)
    return false;

  if (terminal_count != 4)
    return false;

  if (branch == PHYSICS2_BRANCH_NONE)
    return false;

  if (!controlled_nodes_valid(terminals, context))
    return false;

  p = terminals[0];
  n = terminals[1];
  cp = terminals[2];
  cn = terminals[3];
  b = branch;

  if (b >= physics2_accumulator_size(context->accumulator))
    return false;

  beta = primitive->payload.controlled_source.gain.nominal;

  if (!isfinite(beta))
    return false;

  /* Sense short between control terminals. */
  if (!physics2_accumulator_add(context->accumulator, cp, b, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, cn, b, -1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, cp, 1.0))
    return false;

  if (!physics2_accumulator_add(context->accumulator, b, cn, -1.0))
    return false;

  /* Output current beta * i_sense from p to n. */
  if (!physics2_accumulator_add(context->accumulator, p, b, beta))
    return false;

  if (!physics2_accumulator_add(context->accumulator, n, b, -beta))
    return false;

  return true;
}

static uint8_t cccs_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 4;
}

static uint8_t cccs_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 1;
}

static void cccs_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream, "Primitive %s: CCCS beta %.12g +/- %.6g%%\n", primitive->name,
          primitive->payload.controlled_source.gain.nominal,
          primitive->payload.controlled_source.gain.tolerance_pct);
}

static const PhysicsPrimitiveOps cccs_ops = {
    cccs_stamp, cccs_terminal_count, cccs_extra_unknowns, cccs_print};

/* =========================================================
 * Diode (data model only — nonlinear stamp unsupported)
 * ========================================================= */

static bool diode_stamp(const PhysicsPrimitive *primitive,
                        PhysicsExecutionContext *context, const NodeId *terminals,
                        uint8_t terminal_count, BranchId branch) {
  (void)primitive;
  (void)context;
  (void)terminals;
  (void)terminal_count;
  (void)branch;

  /*
   * Missing runtime capability:
   * no residual/Jacobian (or equivalent linearization) ABI,
   * and no Newton iteration ownership outside the primitive.
   */
  return false;
}

static uint8_t diode_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t diode_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void diode_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream,
          "Primitive %s: DIODE Is=%.12g A n=%.6g Vt=%.6g V +/- %.6g%% "
          "[nonlinear stamp unsupported]\n",
          primitive->name, primitive->payload.diode.isat.nominal,
          primitive->payload.diode.n, primitive->payload.diode.vt,
          primitive->payload.diode.isat.tolerance_pct);
}

static const PhysicsPrimitiveOps diode_ops = {
    diode_stamp, diode_terminal_count, diode_extra_unknowns, diode_print};

/* =========================================================
 * Primitive constructors
 * ========================================================= */

static bool init_common(PhysicsPrimitive *primitive, const char *name,
                        double value, double tolerance_pct,
                        const PhysicsPrimitiveOps *ops,
                        PhysicsPrimitiveKind kind) {
  if (!primitive || !name || !ops)
    return false;

  if (!isfinite(value))
    return false;

  if (!isfinite(tolerance_pct) || tolerance_pct < 0.0)
    return false;

  memset(primitive, 0, sizeof(*primitive));

  primitive->ops = ops;
  primitive->kind = kind;

  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';

  primitive->payload.two_terminal.value.nominal = value;
  primitive->payload.two_terminal.value.tolerance_pct = tolerance_pct;

  return true;
}

bool physics2_primitive_init_resistor(PhysicsPrimitive *primitive,
                                      const char *name, double resistance_ohms,
                                      double tolerance_pct) {
  if (resistance_ohms <= 0.0)
    return false;

  return init_common(primitive, name, resistance_ohms, tolerance_pct,
                     &resistor_ops, PHYS_PRIM_RESISTOR);
}

bool physics2_primitive_init_capacitor(PhysicsPrimitive *primitive,
                                       const char *name, double capacitance_f,
                                       double tolerance_pct) {
  if (capacitance_f <= 0.0)
    return false;

  return init_common(primitive, name, capacitance_f, tolerance_pct,
                     &capacitor_ops, PHYS_PRIM_CAPACITOR);
}

bool physics2_primitive_init_vsource(PhysicsPrimitive *primitive,
                                     const char *name, double voltage_v,
                                     double tolerance_pct) {
  return init_common(primitive, name, voltage_v, tolerance_pct, &vsource_ops,
                     PHYS_PRIM_VSOURCE);
}

bool physics2_primitive_init_inductor(PhysicsPrimitive *primitive,
                                      const char *name, double inductance_h,
                                      double tolerance_pct) {
  if (inductance_h <= 0.0)
    return false;

  return init_common(primitive, name, inductance_h, tolerance_pct,
                     &inductor_ops, PHYS_PRIM_INDUCTOR);
}

bool physics2_primitive_init_isource(PhysicsPrimitive *primitive,
                                     const char *name, double current_a,
                                     double tolerance_pct) {
  return init_common(primitive, name, current_a, tolerance_pct, &isource_ops,
                     PHYS_PRIM_ISOURCE);
}

static bool init_controlled(PhysicsPrimitive *primitive, const char *name,
                            double gain, double tolerance_pct,
                            const PhysicsPrimitiveOps *ops,
                            PhysicsPrimitiveKind kind) {
  if (!primitive || !name || !ops)
    return false;

  if (!isfinite(gain))
    return false;

  if (!isfinite(tolerance_pct) || tolerance_pct < 0.0)
    return false;

  memset(primitive, 0, sizeof(*primitive));

  primitive->ops = ops;
  primitive->kind = kind;

  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';

  primitive->payload.controlled_source.gain.nominal = gain;
  primitive->payload.controlled_source.gain.tolerance_pct = tolerance_pct;

  return true;
}

bool physics2_primitive_init_vcvs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct) {
  return init_controlled(primitive, name, gain, tolerance_pct, &vcvs_ops,
                         PHYS_PRIM_VCVS);
}

bool physics2_primitive_init_vccs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct) {
  return init_controlled(primitive, name, gain, tolerance_pct, &vccs_ops,
                         PHYS_PRIM_VCCS);
}

bool physics2_primitive_init_ccvs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct) {
  return init_controlled(primitive, name, gain, tolerance_pct, &ccvs_ops,
                         PHYS_PRIM_CCVS);
}

bool physics2_primitive_init_cccs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct) {
  return init_controlled(primitive, name, gain, tolerance_pct, &cccs_ops,
                         PHYS_PRIM_CCCS);
}

bool physics2_primitive_init_diode(PhysicsPrimitive *primitive, const char *name,
                                   double isat_a, double n, double vt_v,
                                   double tolerance_pct) {
  if (!primitive || !name)
    return false;

  if (!isfinite(isat_a) || isat_a <= 0.0)
    return false;

  if (!isfinite(n) || n <= 0.0)
    return false;

  if (!isfinite(vt_v) || vt_v <= 0.0)
    return false;

  if (!isfinite(tolerance_pct) || tolerance_pct < 0.0)
    return false;

  memset(primitive, 0, sizeof(*primitive));

  primitive->ops = &diode_ops;
  primitive->kind = PHYS_PRIM_DIODE;

  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';

  primitive->payload.diode.isat.nominal = isat_a;
  primitive->payload.diode.isat.tolerance_pct = tolerance_pct;
  primitive->payload.diode.n = n;
  primitive->payload.diode.vt = vt_v;

  return true;
}

/* =========================================================
 * Logic gate (structural / data model only)
 * ========================================================= */

static bool logic_stamp(const PhysicsPrimitive *primitive,
                        PhysicsExecutionContext *context, const NodeId *terminals,
                        uint8_t terminal_count, BranchId branch) {
  (void)primitive;
  (void)context;
  (void)terminals;
  (void)terminal_count;
  (void)branch;

  /* Missing runtime capability: no digital event/timing execution contract. */
  return false;
}

static uint8_t logic_terminal_count(const PhysicsPrimitive *primitive) {
  if (!primitive)
    return 0;

  return (uint8_t)(primitive->payload.logic_gate.input_count +
                   primitive->payload.logic_gate.output_count);
}

static uint8_t logic_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void logic_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;

  fprintf(stream,
          "Primitive %s: LOGIC_GATE in=%u out=%u truth_bytes=%zu "
          "[digital execution unsupported]\n",
          primitive->name,
          (unsigned)primitive->payload.logic_gate.input_count,
          (unsigned)primitive->payload.logic_gate.output_count,
          primitive->payload.logic_gate.truth_table_size);
}

static const PhysicsPrimitiveOps logic_ops = {
    logic_stamp, logic_terminal_count, logic_extra_unknowns, logic_print};

bool physics2_primitive_init_logic_gate(PhysicsPrimitive *primitive,
                                        const char *name, uint8_t input_count,
                                        uint8_t output_count,
                                        uint8_t *truth_table,
                                        size_t truth_table_size) {
  size_t total_terminals;

  if (!primitive || !name || !truth_table)
    return false;

  if (input_count == 0 || output_count == 0)
    return false;

  total_terminals = (size_t)input_count + (size_t)output_count;

  if (total_terminals > PHYSICS2_MAX_TERMINALS)
    return false;

  if (truth_table_size == 0)
    return false;

  memset(primitive, 0, sizeof(*primitive));

  primitive->ops = &logic_ops;
  primitive->kind = PHYS_PRIM_LOGIC_GATE;

  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';

  primitive->payload.logic_gate.input_count = input_count;
  primitive->payload.logic_gate.output_count = output_count;
  primitive->payload.logic_gate.truth_table = truth_table;
  primitive->payload.logic_gate.truth_table_size = truth_table_size;

  return true;
}

/* =========================================================
 * Transistor family (structural / unsupported nonlinear)
 * ========================================================= */

static bool transistor_stamp(const PhysicsPrimitive *primitive,
                             PhysicsExecutionContext *context,
                             const NodeId *terminals, uint8_t terminal_count,
                             BranchId branch) {
  (void)primitive;
  (void)context;
  (void)terminals;
  (void)terminal_count;
  (void)branch;

  /*
   * Missing runtime capability: residual/Jacobian (or equivalent)
   * and Newton ownership outside the primitive.
   */
  return false;
}

static uint8_t transistor_terminal_count(const PhysicsPrimitive *primitive) {
  if (!primitive)
    return 0;

  return primitive->payload.transistor.terminal_count;
}

static uint8_t transistor_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void transistor_print(const PhysicsPrimitive *primitive, FILE *stream) {
  const char *subtype;

  if (!primitive || !stream)
    return;

  switch (primitive->payload.transistor.subtype) {
  case PHYS_XSTR_BJT:
    subtype = "BJT";
    break;
  case PHYS_XSTR_NMOS:
    subtype = "NMOS";
    break;
  case PHYS_XSTR_PMOS:
    subtype = "PMOS";
    break;
  default:
    subtype = "UNKNOWN";
    break;
  }

  fprintf(stream,
          "Primitive %s: TRANSISTOR %s scale=%.12g +/- %.6g%% "
          "[nonlinear stamp unsupported]\n",
          primitive->name, subtype,
          primitive->payload.transistor.scale.nominal,
          primitive->payload.transistor.scale.tolerance_pct);
}

static const PhysicsPrimitiveOps transistor_ops = {
    transistor_stamp, transistor_terminal_count, transistor_extra_unknowns,
    transistor_print};

static bool init_transistor(PhysicsPrimitive *primitive, const char *name,
                            double scale, double tolerance_pct,
                            PhysicsTransistorSubtype subtype) {
  if (!primitive || !name)
    return false;

  if (!isfinite(scale) || scale <= 0.0)
    return false;

  if (!isfinite(tolerance_pct) || tolerance_pct < 0.0)
    return false;

  memset(primitive, 0, sizeof(*primitive));

  primitive->ops = &transistor_ops;
  primitive->kind = PHYS_PRIM_TRANSISTOR;

  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';

  primitive->payload.transistor.subtype = subtype;
  primitive->payload.transistor.scale.nominal = scale;
  primitive->payload.transistor.scale.tolerance_pct = tolerance_pct;
  primitive->payload.transistor.terminal_count = 3;

  return true;
}

bool physics2_primitive_init_bjt(PhysicsPrimitive *primitive, const char *name,
                                 double scale, double tolerance_pct) {
  return init_transistor(primitive, name, scale, tolerance_pct, PHYS_XSTR_BJT);
}

bool physics2_primitive_init_nmos(PhysicsPrimitive *primitive, const char *name,
                                  double scale, double tolerance_pct) {
  return init_transistor(primitive, name, scale, tolerance_pct, PHYS_XSTR_NMOS);
}

bool physics2_primitive_init_pmos(PhysicsPrimitive *primitive, const char *name,
                                  double scale, double tolerance_pct) {
  return init_transistor(primitive, name, scale, tolerance_pct, PHYS_XSTR_PMOS);
}

/* =========================================================
 * Opcode mapping helper
 * ========================================================= */

static PhysicsOpcode physics_opcode_from_kind(PhysicsPrimitiveKind kind) {
  switch (kind) {
  case PHYS_PRIM_RESISTOR:
    return PHYS_OP_RESISTOR;

  case PHYS_PRIM_CAPACITOR:
    return PHYS_OP_CAPACITOR;

  case PHYS_PRIM_VSOURCE:
    return PHYS_OP_VSOURCE;

  case PHYS_PRIM_INDUCTOR:
    return PHYS_OP_INDUCTOR;

  case PHYS_PRIM_ISOURCE:
    return PHYS_OP_ISOURCE;

  case PHYS_PRIM_VCVS:
    return PHYS_OP_VCVS;

  case PHYS_PRIM_VCCS:
    return PHYS_OP_VCCS;

  case PHYS_PRIM_CCVS:
    return PHYS_OP_CCVS;

  case PHYS_PRIM_CCCS:
    return PHYS_OP_CCCS;

  case PHYS_PRIM_DIODE:
    return PHYS_OP_DIODE;

  case PHYS_PRIM_TRANSISTOR:
    return PHYS_OP_TRANSISTOR;

  case PHYS_PRIM_LOGIC_GATE:
    return PHYS_OP_LOGIC_GATE;

  case PHYS_PRIM_NONE:
  default:
    return PHYS_OP_NONE;
  }
}

/* =========================================================
 * Program
 * ========================================================= */

void physics2_program_init(PhysicsProgram *program) {
  if (!program)
    return;

  program->instructions = NULL;
  program->instruction_count = 0;
  program->instruction_capacity = 0;

  physics_param_pool_init(&program->parameters);
  physics_state_pool_init(&program->states);
  physics2_registry_init(&program->primitives);

  program->next_node = 0;
  program->branch_count = 0;
}

void physics2_program_free(PhysicsProgram *program) {
  if (!program)
    return;

  free(program->instructions);

  program->instructions = NULL;
  program->instruction_count = 0;
  program->instruction_capacity = 0;

  physics_param_pool_free(&program->parameters);
  physics_state_pool_free(&program->states);
  physics2_registry_free(&program->primitives);

  program->next_node = 0;
  program->branch_count = 0;
}

NodeId physics2_program_new_node(PhysicsProgram *program) {
  NodeId node;

  if (!program)
    return PHYSICS2_NODE_NONE;

  node = program->next_node;

  if (node == PHYSICS2_NODE_NONE)
    return PHYSICS2_NODE_NONE;

  program->next_node++;

  return node;
}

PrimitiveId physics2_program_add_primitive(PhysicsProgram *program,
                                           PhysicsPrimitive *primitive,
                                           const NodeId *terminals,
                                           uint8_t terminal_count) {
  PhysicsInstruction *instructions;
  size_t capacity;
  uint8_t expected;
  uint8_t unknowns;
  uint8_t i;
  PrimitiveId registered_id = PHYSICS_PRIMITIVE_NONE;

  if (!program || !primitive || !primitive->ops || !terminals)
    return PHYSICS_PRIMITIVE_NONE;

  if (!primitive->ops->terminal_count)
    return PHYSICS_PRIMITIVE_NONE;

  expected = primitive->ops->terminal_count(primitive);

  if (expected != terminal_count || terminal_count == 0 ||
      terminal_count > PHYSICS2_MAX_TERMINALS)
    return PHYSICS_PRIMITIVE_NONE;

  unknowns = 0;

  if (primitive->ops->extra_unknowns)
    unknowns = primitive->ops->extra_unknowns(primitive);

  if (unknowns > 0) {
    switch (primitive->kind) {
    case PHYS_PRIM_VSOURCE:
    case PHYS_PRIM_VCVS:
    case PHYS_PRIM_CCVS:
    case PHYS_PRIM_CCCS:
      break;
    default:
      return PHYSICS_PRIMITIVE_NONE;
    }
  }

  if (!physics2_registry_add(&program->primitives, primitive, &registered_id)) {
    return PHYSICS_PRIMITIVE_NONE;
  }

  if (program->instruction_count == program->instruction_capacity) {
    capacity =
        program->instruction_capacity ? program->instruction_capacity * 2 : 8;

    if (capacity < program->instruction_capacity)
      return PHYSICS_PRIMITIVE_NONE;

    instructions =
        realloc(program->instructions, capacity * sizeof(*instructions));

    if (!instructions)
      return PHYSICS_PRIMITIVE_NONE;

    program->instructions = instructions;
    program->instruction_capacity = capacity;
  }

  PhysicsInstruction instruction = {0};

  instruction.opcode = physics_opcode_from_kind(primitive->kind);

  if (!physics_opcode_valid(instruction.opcode))
    return PHYSICS_PRIMITIVE_NONE;

  instruction.primitive_id = registered_id;
  instruction.terminal_count = terminal_count;

  for (i = 0; i < terminal_count; i++) {
    if (terminals[i] == PHYSICS2_NODE_NONE)
      return PHYSICS_PRIMITIVE_NONE;

    instruction.terminals[i] = terminals[i];
  }

  instruction.branch = PHYSICS2_BRANCH_NONE;
  instruction.parameter_id = PHYSICS_PARAM_NONE;
  instruction.state_id = PHYSICS_STATE_NONE;
  instruction.flags = 0;

  if (primitive->kind == PHYS_PRIM_RESISTOR) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.two_terminal.value);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_CAPACITOR) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.two_terminal.value);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;

    instruction.state_id = physics_state_add(&program->states, 1);

    if (instruction.state_id == PHYSICS_STATE_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_VSOURCE) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.two_terminal.value);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_INDUCTOR) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.two_terminal.value);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;

    instruction.state_id = physics_state_add(&program->states, 1);

    if (instruction.state_id == PHYSICS_STATE_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_ISOURCE) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.two_terminal.value);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_VCVS || primitive->kind == PHYS_PRIM_VCCS ||
      primitive->kind == PHYS_PRIM_CCVS || primitive->kind == PHYS_PRIM_CCCS) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.controlled_source.gain);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_DIODE) {
    instruction.parameter_id =
        physics_param_add(&program->parameters, primitive->payload.diode.isat);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (primitive->kind == PHYS_PRIM_TRANSISTOR) {
    instruction.parameter_id = physics_param_add(
        &program->parameters, primitive->payload.transistor.scale);

    if (instruction.parameter_id == PHYSICS_PARAM_NONE)
      return PHYSICS_PRIMITIVE_NONE;
  }

  if (unknowns > 0) {
    instruction.branch = program->next_node + program->branch_count;
    program->branch_count += unknowns;
  }

  program->instructions[program->instruction_count] = instruction;
  program->instruction_count++;

  return registered_id;
}

/* =========================================================
 * Execution context
 * ========================================================= */

bool physics2_context_init(PhysicsExecutionContext *context,
                           PhysicsProgram *program,
                           PhysicsAccumulator *accumulator, double timestep) {
  size_t total_unknowns;

  if (!context || !program || !accumulator)
    return false;

  if (timestep <= 0.0 || !isfinite(timestep))
    return false;

  total_unknowns = program->next_node + program->branch_count;

  if (total_unknowns != physics2_accumulator_size(accumulator))
    return false;

  memset(context, 0, sizeof(*context));

  context->states =
      calloc(program->instruction_count, sizeof(*context->states));

  context->solution = calloc(total_unknowns, sizeof(*context->solution));

  if (program->instruction_count > 0 && !context->states)
    goto fail;

  if (total_unknowns > 0 && !context->solution)
    goto fail;

  context->program = program;
  context->accumulator = accumulator;
  context->timestep = timestep;
  context->time = 0.0;

  context->state_count = program->instruction_count;
  context->solution_size = total_unknowns;

  return true;

fail:
  free(context->states);
  free(context->solution);
  memset(context, 0, sizeof(*context));

  return false;
}

void physics2_context_free(PhysicsExecutionContext *context) {
  if (!context)
    return;

  free(context->states);
  free(context->solution);

  memset(context, 0, sizeof(*context));
}

void physics2_context_reset(PhysicsExecutionContext *context) {
  if (!context)
    return;

  context->time = 0.0;

  if (context->states) {
    memset(context->states, 0, context->state_count * sizeof(*context->states));
  }

  if (context->solution) {
    memset(context->solution, 0,
           context->solution_size * sizeof(*context->solution));
  }

  physics2_accumulator_clear(context->accumulator);
}

bool physics2_interpreter_execute(PhysicsInterpreter *interpreter) {
  size_t i;

  if (!interpreter || !interpreter->program || !interpreter->context)
    return false;

  physics2_accumulator_clear(interpreter->context->accumulator);

  for (i = 0; i < interpreter->program->instruction_count; i++) {
    PhysicsInstruction *instruction = &interpreter->program->instructions[i];

    PhysicsPrimitive *primitive = physics2_registry_get(
        &interpreter->program->primitives, instruction->primitive_id);

    if (!primitive || !primitive->ops)
      return false;

    if (primitive->ops->print)
      primitive->ops->print(primitive, stdout);

    if (primitive->ops->stamp) {
      if (!primitive->ops->stamp(
              primitive, interpreter->context, instruction->terminals,
              instruction->terminal_count, instruction->branch)) {
        return false;
      }
    }
  }

  return true;
}

bool physics2_context_step(PhysicsExecutionContext *context,
                           NodeId reference_node) {
  PhysicsInterpreter interpreter;
  size_t i;

  if (!context || !context->program || !context->accumulator ||
      !context->solution)
    return false;

  interpreter.program = context->program;
  interpreter.context = context;

  if (!physics2_interpreter_execute(&interpreter))
    return false;

  if (!physics2_accumulator_solve(context->accumulator, reference_node,
                                  context->solution))
    return false;

  for (i = 0; i < context->program->instruction_count; i++) {
    PhysicsInstruction *instruction = &context->program->instructions[i];

    PhysicsPrimitive *primitive = physics2_registry_get(
        &context->program->primitives, instruction->primitive_id);

    if (!primitive)
      return false;

    if (primitive->kind == PHYS_PRIM_CAPACITOR) {
      NodeId p = instruction->terminals[0];
      NodeId n = instruction->terminals[1];

      if (p >= context->solution_size || n >= context->solution_size)
        return false;

      context->states[i].capacitor_previous_voltage =
          context->solution[p] - context->solution[n];
    }

    if (primitive->kind == PHYS_PRIM_INDUCTOR) {
      NodeId p = instruction->terminals[0];
      NodeId n = instruction->terminals[1];
      double inductance;
      double conductance;
      double voltage;

      if (p >= context->solution_size || n >= context->solution_size)
        return false;

      inductance = primitive->payload.two_terminal.value.nominal;

      if (!isfinite(inductance) || inductance <= 0.0 ||
          context->timestep <= 0.0)
        return false;

      conductance = context->timestep / inductance;
      voltage = context->solution[p] - context->solution[n];

      context->states[i].inductor_previous_current += conductance * voltage;
    }
  }

  context->time += context->timestep;

  return true;
}

bool physics2_interpreter_init(PhysicsInterpreter *interpreter,
                               PhysicsProgram *program,
                               PhysicsExecutionContext *context) {
  if (!interpreter || !program || !context)
    return false;

  interpreter->program = program;
  interpreter->context = context;

  return true;
}
