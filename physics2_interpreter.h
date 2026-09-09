#ifndef PHYSICS2_INTERPRETER_H
#define PHYSICS2_INTERPRETER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "physics2_isa.h"

#define PHYSICS2_NODE_NONE PHYSICS_NODE_NONE
#define PHYSICS2_BRANCH_NONE PHYSICS_BRANCH_NONE

typedef struct PhysicsAccumulator PhysicsAccumulator;
typedef struct PhysicsPrimitive PhysicsPrimitive;
typedef struct PhysicsExecutionContext PhysicsExecutionContext;

/* =========================================================
 * Primitive kinds
 * ========================================================= */

typedef enum {
  PHYS_PRIM_NONE = 0,

  PHYS_PRIM_RESISTOR,
  PHYS_PRIM_CAPACITOR,
  PHYS_PRIM_INDUCTOR,
  PHYS_PRIM_VSOURCE,
  PHYS_PRIM_ISOURCE,

  PHYS_PRIM_VCVS,
  PHYS_PRIM_VCCS,
  PHYS_PRIM_CCVS,
  PHYS_PRIM_CCCS,

  PHYS_PRIM_DIODE,
  PHYS_PRIM_TRANSISTOR,
  PHYS_PRIM_LOGIC_GATE
} PhysicsPrimitiveKind;

/* =========================================================
 * Primitive payloads
 * ========================================================= */

typedef struct {
  PhysicsValue value;
} PhysicsTwoTerminal;

typedef struct {
  PhysicsValue gain;
} PhysicsControlledSource;

typedef struct {
  uint8_t input_count;
  uint8_t output_count;

  uint8_t *truth_table;
  size_t truth_table_size;
} PhysicsLogicGate;

typedef union {
  PhysicsTwoTerminal two_terminal;
  PhysicsControlledSource controlled_source;
  PhysicsLogicGate logic_gate;
} PhysicsPrimitivePayload;

/* =========================================================
 * Primitive operations
 * ========================================================= */

typedef struct {
  bool (*stamp)(const PhysicsPrimitive *primitive,
                PhysicsExecutionContext *context, const NodeId *terminals,
                uint8_t terminal_count, BranchId branch);

  uint8_t (*terminal_count)(const PhysicsPrimitive *primitive);

  uint8_t (*extra_unknowns)(const PhysicsPrimitive *primitive);

  void (*print)(const PhysicsPrimitive *primitive, FILE *stream);
} PhysicsPrimitiveOps;

/* =========================================================
 * Primitive
 * ========================================================= */

struct PhysicsPrimitive {
  const PhysicsPrimitiveOps *ops;
  PhysicsPrimitiveKind kind;

  char name[64];

  PhysicsPrimitivePayload payload;
};

/* =========================================================
 * Primitive Registry
 * ========================================================= */

typedef struct {
  PhysicsPrimitive **items;
  size_t count;
  size_t capacity;
} PhysicsPrimitiveRegistry;

void physics2_registry_init(PhysicsPrimitiveRegistry *registry);
void physics2_registry_free(PhysicsPrimitiveRegistry *registry);

bool physics2_registry_add(PhysicsPrimitiveRegistry *registry,
                           PhysicsPrimitive *primitive, PrimitiveId *out_id);

PhysicsPrimitive *
physics2_registry_get(const PhysicsPrimitiveRegistry *registry, PrimitiveId id);

/* =========================================================
 * Primitive constructors
 * ========================================================= */

bool physics2_primitive_init_resistor(PhysicsPrimitive *primitive,
                                      const char *name, double resistance_ohms,
                                      double tolerance_pct);

bool physics2_primitive_init_capacitor(PhysicsPrimitive *primitive,
                                       const char *name, double capacitance_f,
                                       double tolerance_pct);

bool physics2_primitive_init_vsource(PhysicsPrimitive *primitive,
                                     const char *name, double voltage_v,
                                     double tolerance_pct);

/* =========================================================
 * Accumulator
 * ========================================================= */

PhysicsAccumulator *physics2_accumulator_create(size_t size);

void physics2_accumulator_free(PhysicsAccumulator *accumulator);

void physics2_accumulator_clear(PhysicsAccumulator *accumulator);

size_t physics2_accumulator_size(const PhysicsAccumulator *accumulator);

bool physics2_accumulator_add(PhysicsAccumulator *accumulator, size_t row,
                              size_t column, double value);

bool physics2_accumulator_add_rhs(PhysicsAccumulator *accumulator, size_t row,
                                  double value);

double physics2_accumulator_get(const PhysicsAccumulator *accumulator,
                                size_t row, size_t column);

double physics2_accumulator_get_rhs(const PhysicsAccumulator *accumulator,
                                    size_t row);

void physics2_accumulator_print(const PhysicsAccumulator *accumulator,
                                FILE *stream);

/* =========================================================
 * Linear solve
 * ========================================================= */

bool physics2_accumulator_solve(const PhysicsAccumulator *accumulator,
                                NodeId reference_node, double *solution);

/* =========================================================
 * Program
 * ========================================================= */

typedef struct {
  PhysicsInstruction *instructions;
  size_t instruction_count;
  size_t instruction_capacity;

  PhysicsParamPool parameters;
  PhysicsStatePool states;

  PhysicsPrimitiveRegistry primitives;

  NodeId next_node;
  BranchId branch_count;
} PhysicsProgram;

void physics2_program_init(PhysicsProgram *program);

void physics2_program_free(PhysicsProgram *program);

NodeId physics2_program_new_node(PhysicsProgram *program);

PrimitiveId physics2_program_add_primitive(PhysicsProgram *program,
                                           PhysicsPrimitive *primitive,
                                           const NodeId *terminals,
                                           uint8_t terminal_count);

/* =========================================================
 * Dynamic state
 * ========================================================= */

typedef struct {
  double capacitor_previous_voltage;
} PhysicsPrimitiveState;

/* =========================================================
 * Execution context
 * ========================================================= */

struct PhysicsExecutionContext {
  PhysicsProgram *program;
  PhysicsAccumulator *accumulator;

  double time;
  double timestep;

  PhysicsPrimitiveState *states;
  size_t state_count;

  double *solution;
  size_t solution_size;
};

bool physics2_context_init(PhysicsExecutionContext *context,
                           PhysicsProgram *program,
                           PhysicsAccumulator *accumulator, double timestep);

void physics2_context_free(PhysicsExecutionContext *context);

void physics2_context_reset(PhysicsExecutionContext *context);

bool physics2_context_step(PhysicsExecutionContext *context,
                           NodeId reference_node);

/* =========================================================
 * Interpreter
 * ========================================================= */

typedef struct {
  PhysicsProgram *program;
  PhysicsExecutionContext *context;
} PhysicsInterpreter;

bool physics2_interpreter_init(PhysicsInterpreter *interpreter,
                               PhysicsProgram *program,
                               PhysicsExecutionContext *context);

bool physics2_interpreter_execute(PhysicsInterpreter *interpreter);

#endif
