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
  PHYS_PRIM_LOGIC_GATE,

  /* Phase 12 behavioral */
  PHYS_PRIM_SWITCH,
  PHYS_PRIM_OPAMP,
  PHYS_PRIM_LDO,
  PHYS_PRIM_BATTERY
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

/*
 * Diode model — Shockley constitutive law.
 * Stamp provides local linearization (Gd, Ieq); global Newton lives in
 * physics2_context_step when any PHYS_PRIM_DIODE is present.
 */
typedef struct {
  PhysicsValue isat;
  double n;
  double vt;
} PhysicsDiode;

typedef struct {
  uint8_t input_count;
  uint8_t output_count;

  uint8_t *truth_table;
  size_t truth_table_size;
} PhysicsLogicGate;

typedef enum {
  PHYS_XSTR_NONE = 0,
  PHYS_XSTR_BJT,
  PHYS_XSTR_NMOS,
  PHYS_XSTR_PMOS
} PhysicsTransistorSubtype;

/*
 * Transistor payload.
 * BJT: model = ebers_moll (DC). Terminals [0]=C, [1]=B, [2]=E.
 * MOS: Shichman-Hodges Level-1 (DC). Terminals [0]=D, [1]=G, [2]=S.
 */
typedef struct {
  PhysicsTransistorSubtype subtype;
  PhysicsValue scale; /* BJT: Is area; MOS: W/L multiplier on K */
  uint8_t terminal_count;
  /* BJT (ebers_moll) */
  double isat;    /* Is (A); IES=Is/αF, ICS=Is/αR */
  double alpha_f; /* 0 < αF < 1 */
  double alpha_r; /* 0 < αR < 1 */
  double vt;      /* thermal voltage */
  /* MOS (level1 / Shichman-Hodges) */
  double vth;    /* threshold (V); NMOS >0, PMOS stored positive |Vth| */
  double k;      /* K = μ Cox (A/V^2); effective K *= scale.nominal */
  double lambda; /* channel-length modulation (1/V); sat only */
} PhysicsTransistor;

/* Switch: R = Ron if on else Roff. Terminals [0],[1]. No hidden state. */
typedef struct {
  double ron;
  double roff;
  int on; /* nonzero = ON */
} PhysicsSwitch;

/*
 * Behavioral op-amp: V(out+)-V(out-) = A * (V(in+)-V(in-)).
 * Terminals [0]=out+, [1]=out-, [2]=in+, [3]=in- (same as VCVS).
 * Finite A; no rails in v1 (ponytail: add rail clamp when saturation tests need it).
 */
typedef struct {
  double gain;
} PhysicsOpAmp;

/*
 * behavioral_lumped_ldo (not silicon):
 *   Vset = min(Vtarget, Vin_gnd - Vdropout); Vout via Rout Thevenin to GND.
 * Terminals [0]=VIN, [1]=VOUT, [2]=GND. No input current. ilimit<=0 disables.
 */
typedef struct {
  double vtarget;
  double vdropout;
  double rout;
  double ilimit;
} PhysicsLdo;

/* Battery Thevenin: Voc − I*Rint. Terminals [0]=+, [1]=−. */
typedef struct {
  double voc;
  double rint;
} PhysicsBattery;

typedef union {
  PhysicsTwoTerminal two_terminal;
  PhysicsControlledSource controlled_source;
  PhysicsDiode diode;
  PhysicsLogicGate logic_gate;
  PhysicsTransistor transistor;
  PhysicsSwitch sw;
  PhysicsOpAmp opamp;
  PhysicsLdo ldo;
  PhysicsBattery battery;
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

bool physics2_primitive_init_inductor(PhysicsPrimitive *primitive,
                                      const char *name, double inductance_h,
                                      double tolerance_pct);

bool physics2_primitive_init_isource(PhysicsPrimitive *primitive,
                                     const char *name, double current_a,
                                     double tolerance_pct);

bool physics2_primitive_init_vcvs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct);

bool physics2_primitive_init_vccs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct);

bool physics2_primitive_init_ccvs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct);

bool physics2_primitive_init_cccs(PhysicsPrimitive *primitive, const char *name,
                                  double gain, double tolerance_pct);

bool physics2_primitive_init_diode(PhysicsPrimitive *primitive, const char *name,
                                   double isat_a, double n, double vt_v,
                                   double tolerance_pct);

/*
 * Structural logic-gate data only.
 * Caller owns truth_table memory. No digital event/timing ABI.
 */
bool physics2_primitive_init_logic_gate(PhysicsPrimitive *primitive,
                                        const char *name, uint8_t input_count,
                                        uint8_t output_count,
                                        uint8_t *truth_table,
                                        size_t truth_table_size);

/*
 * NPN BJT, model = ebers_moll.
 * Terminals: [0]=collector, [1]=base, [2]=emitter.
 */
bool physics2_primitive_init_bjt(PhysicsPrimitive *primitive, const char *name,
                                 double isat_a, double alpha_f, double alpha_r,
                                 double vt_v, double tolerance_pct);

/*
 * NMOS/PMOS Level-1. Terminals: [0]=drain, [1]=gate, [2]=source.
 * k_a_per_v2 is K=μCox; scale multiplies K. lambda is channel modulation.
 */
bool physics2_primitive_init_nmos(PhysicsPrimitive *primitive, const char *name,
                                  double vth_v, double k_a_per_v2,
                                  double lambda_per_v, double scale,
                                  double tolerance_pct);

bool physics2_primitive_init_pmos(PhysicsPrimitive *primitive, const char *name,
                                  double vth_v, double k_a_per_v2,
                                  double lambda_per_v, double scale,
                                  double tolerance_pct);

bool physics2_primitive_init_switch(PhysicsPrimitive *primitive, const char *name,
                                    double ron_ohm, double roff_ohm, int on);

bool physics2_primitive_init_opamp(PhysicsPrimitive *primitive, const char *name,
                                   double gain);

bool physics2_primitive_init_ldo(PhysicsPrimitive *primitive, const char *name,
                                 double vtarget, double vdropout, double rout,
                                 double ilimit);

bool physics2_primitive_init_battery(PhysicsPrimitive *primitive,
                                     const char *name, double voc,
                                     double rint_ohm);

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
 * AC (complex MNA) — opaque paired re/im storage
 * ========================================================= */

typedef struct PhysicsAcSystem PhysicsAcSystem;

PhysicsAcSystem *physics2_ac_create(size_t size);
void physics2_ac_free(PhysicsAcSystem *sys);
void physics2_ac_clear(PhysicsAcSystem *sys);
size_t physics2_ac_size(const PhysicsAcSystem *sys);

/* Stamp A[row,col] += re + j*im ; RHS[row] += re + j*im */
bool physics2_ac_add(PhysicsAcSystem *sys, size_t row, size_t column, double re,
                     double im);
bool physics2_ac_add_rhs(PhysicsAcSystem *sys, size_t row, double re,
                         double im);

bool physics2_ac_solve(const PhysicsAcSystem *sys, NodeId reference_node,
                       double *x_re, double *x_im);

void physics2_ac_mag_phase(double re, double im, double *mag_out,
                           double *phase_deg_out);

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
  double inductor_previous_current;
} PhysicsPrimitiveState;

typedef enum {
  PHYSICS2_NEWTON_OK = 0,
  PHYSICS2_NEWTON_LINEAR,    /* direct solve, no nonlinear devices */
  PHYSICS2_NEWTON_DIVERGED,
  PHYSICS2_NEWTON_SINGULAR,
  PHYSICS2_NEWTON_NONFINITE
} Physics2NewtonStatus;

typedef struct {
  Physics2NewtonStatus status;
  size_t iterations;
  double residual_norm; /* ∞-norm of last Newton update (proxy for ||F||) */
  double update_norm;   /* same as residual_norm at accept; last raw step */
  double damping;       /* last accepted α in (0,1] */
  char failure[80];
} Physics2NewtonReport;

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

  /* Global Newton (runtime-owned; devices only stamp companions). */
  Physics2NewtonReport newton;
  size_t newton_max_iter;
  double newton_abs_tol;
  double newton_rel_tol;
  double newton_max_dv;
  int quiet; /* suppress primitive print during Newton */
};

bool physics2_context_init(PhysicsExecutionContext *context,
                           PhysicsProgram *program,
                           PhysicsAccumulator *accumulator, double timestep);

void physics2_context_free(PhysicsExecutionContext *context);

void physics2_context_reset(PhysicsExecutionContext *context);

/* Optional Newton knobs (0 / non-finite → keep defaults). */
void physics2_context_set_newton_limits(PhysicsExecutionContext *context,
                                        size_t max_iter, double abs_tol,
                                        double rel_tol, double max_dv);

bool physics2_context_step(PhysicsExecutionContext *context,
                           NodeId reference_node);

/*
 * Run n_steps BE timesteps (requires context->timestep > 0).
 * Stops on first failed step; prior commits kept (transactional per step).
 */
bool physics2_context_run_steps(PhysicsExecutionContext *context,
                                NodeId reference_node, size_t n_steps);

/*
 * Linear AC at ω (rad/s). Stamps R/C/L/V/I (+ controlled, switch, battery).
 * Writes phasors into x_re/x_im (length == context->solution_size).
 * Cap: Y=jωC; Ind: Vp-Vn = jωL I (branch); Vsrc phasor = DC nominal ∠0.
 */
bool physics2_context_step_ac(const PhysicsExecutionContext *context,
                              NodeId reference_node, double omega_rad,
                              double *x_re, double *x_im);

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
