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
 * AC complex MNA (paired re/im — opaque to stamps)
 * ========================================================= */

struct PhysicsAcSystem {
  size_t size;
  double *re; /* n*n */
  double *im;
  double *rhs_re;
  double *rhs_im;
};

void physics2_ac_free(PhysicsAcSystem *sys);

static size_t ac_index(const PhysicsAcSystem *sys, size_t row, size_t col) {
  return row * sys->size + col;
}

PhysicsAcSystem *physics2_ac_create(size_t size) {
  PhysicsAcSystem *sys;
  if (size == 0)
    return NULL;
  sys = calloc(1, sizeof(*sys));
  if (!sys)
    return NULL;
  if (size > SIZE_MAX / size)
    goto fail;
  sys->re = calloc(size * size, sizeof(double));
  sys->im = calloc(size * size, sizeof(double));
  sys->rhs_re = calloc(size, sizeof(double));
  sys->rhs_im = calloc(size, sizeof(double));
  if (!sys->re || !sys->im || !sys->rhs_re || !sys->rhs_im)
    goto fail;
  sys->size = size;
  return sys;
fail:
  physics2_ac_free(sys);
  return NULL;
}

void physics2_ac_free(PhysicsAcSystem *sys) {
  if (!sys)
    return;
  free(sys->re);
  free(sys->im);
  free(sys->rhs_re);
  free(sys->rhs_im);
  free(sys);
}

void physics2_ac_clear(PhysicsAcSystem *sys) {
  if (!sys)
    return;
  memset(sys->re, 0, sys->size * sys->size * sizeof(double));
  memset(sys->im, 0, sys->size * sys->size * sizeof(double));
  memset(sys->rhs_re, 0, sys->size * sizeof(double));
  memset(sys->rhs_im, 0, sys->size * sizeof(double));
}

size_t physics2_ac_size(const PhysicsAcSystem *sys) {
  return sys ? sys->size : 0;
}

bool physics2_ac_add(PhysicsAcSystem *sys, size_t row, size_t column, double re,
                     double im) {
  size_t idx;
  if (!sys || row >= sys->size || column >= sys->size)
    return false;
  if (!isfinite(re) || !isfinite(im))
    return false;
  idx = ac_index(sys, row, column);
  sys->re[idx] += re;
  sys->im[idx] += im;
  return true;
}

bool physics2_ac_add_rhs(PhysicsAcSystem *sys, size_t row, double re,
                         double im) {
  if (!sys || row >= sys->size)
    return false;
  if (!isfinite(re) || !isfinite(im))
    return false;
  sys->rhs_re[row] += re;
  sys->rhs_im[row] += im;
  return true;
}

void physics2_ac_mag_phase(double re, double im, double *mag_out,
                           double *phase_deg_out) {
  double mag = sqrt(re * re + im * im);
  if (mag_out)
    *mag_out = mag;
  if (phase_deg_out)
    *phase_deg_out = (mag < 1e-30) ? 0.0 : atan2(im, re) * (180.0 / 3.14159265358979323846);
}

static void cplx_mul(double ar, double ai, double br, double bi, double *cr,
                     double *ci) {
  *cr = ar * br - ai * bi;
  *ci = ar * bi + ai * br;
}

static int cplx_div(double ar, double ai, double br, double bi, double *cr,
                    double *ci) {
  double d = br * br + bi * bi;
  if (d < 1e-30)
    return 0;
  *cr = (ar * br + ai * bi) / d;
  *ci = (ai * br - ar * bi) / d;
  return 1;
}

bool physics2_ac_solve(const PhysicsAcSystem *sys, NodeId reference_node,
                       double *x_re, double *x_im) {
  size_t n, i, j, k;
  double *ar, *ai, *br, *bi;

  if (!sys || !x_re || !x_im)
    return false;
  n = sys->size;
  if (n == 0 || reference_node >= n)
    return false;

  ar = malloc(n * n * sizeof(double));
  ai = malloc(n * n * sizeof(double));
  br = malloc(n * sizeof(double));
  bi = malloc(n * sizeof(double));
  if (!ar || !ai || !br || !bi) {
    free(ar);
    free(ai);
    free(br);
    free(bi);
    return false;
  }
  memcpy(ar, sys->re, n * n * sizeof(double));
  memcpy(ai, sys->im, n * n * sizeof(double));
  memcpy(br, sys->rhs_re, n * sizeof(double));
  memcpy(bi, sys->rhs_im, n * sizeof(double));

  /* Ground reference node */
  for (j = 0; j < n; j++) {
    ar[reference_node * n + j] = 0.0;
    ai[reference_node * n + j] = 0.0;
    ar[j * n + reference_node] = 0.0;
    ai[j * n + reference_node] = 0.0;
  }
  ar[reference_node * n + reference_node] = 1.0;
  ai[reference_node * n + reference_node] = 0.0;
  br[reference_node] = 0.0;
  bi[reference_node] = 0.0;

  for (k = 0; k < n; k++) {
    size_t pivot = k;
    double max_v = ar[k * n + k] * ar[k * n + k] + ai[k * n + k] * ai[k * n + k];
    for (i = k + 1; i < n; i++) {
      double v = ar[i * n + k] * ar[i * n + k] + ai[i * n + k] * ai[i * n + k];
      if (v > max_v) {
        max_v = v;
        pivot = i;
      }
    }
    if (max_v < 1e-28) {
      free(ar);
      free(ai);
      free(br);
      free(bi);
      return false;
    }
    if (pivot != k) {
      for (j = 0; j < n; j++) {
        double tr = ar[k * n + j], ti = ai[k * n + j];
        ar[k * n + j] = ar[pivot * n + j];
        ai[k * n + j] = ai[pivot * n + j];
        ar[pivot * n + j] = tr;
        ai[pivot * n + j] = ti;
      }
      {
        double tr = br[k], ti = bi[k];
        br[k] = br[pivot];
        bi[k] = bi[pivot];
        br[pivot] = tr;
        bi[pivot] = ti;
      }
    }
    for (i = k + 1; i < n; i++) {
      double fr, fi;
      if (!cplx_div(ar[i * n + k], ai[i * n + k], ar[k * n + k], ai[k * n + k],
                    &fr, &fi))
        continue;
      ar[i * n + k] = 0.0;
      ai[i * n + k] = 0.0;
      for (j = k + 1; j < n; j++) {
        double pr, pi;
        cplx_mul(fr, fi, ar[k * n + j], ai[k * n + j], &pr, &pi);
        ar[i * n + j] -= pr;
        ai[i * n + j] -= pi;
      }
      {
        double pr, pi;
        cplx_mul(fr, fi, br[k], bi[k], &pr, &pi);
        br[i] -= pr;
        bi[i] -= pi;
      }
    }
  }

  for (i = n; i-- > 0;) {
    double sr = br[i], si = bi[i];
    for (j = i + 1; j < n; j++) {
      double pr, pi;
      cplx_mul(ar[i * n + j], ai[i * n + j], x_re[j], x_im[j], &pr, &pi);
      sr -= pr;
      si -= pi;
    }
    if (!cplx_div(sr, si, ar[i * n + i], ai[i * n + i], &x_re[i], &x_im[i])) {
      free(ar);
      free(ai);
      free(br);
      free(bi);
      return false;
    }
  }

  free(ar);
  free(ai);
  free(br);
  free(bi);
  return true;
}

static bool ac_stamp_admittance(PhysicsAcSystem *sys, NodeId a, NodeId b,
                                double g_re, double g_im) {
  if (a == PHYSICS2_NODE_NONE || b == PHYSICS2_NODE_NONE)
    return false;
  return physics2_ac_add(sys, a, a, g_re, g_im) &&
         physics2_ac_add(sys, a, b, -g_re, -g_im) &&
         physics2_ac_add(sys, b, a, -g_re, -g_im) &&
         physics2_ac_add(sys, b, b, g_re, g_im);
}

/* Small-signal stamp: the device's DC stamp at the operating point is its
 * Jacobian J(x0); copy the matrix part (bias RHS dropped) into the AC system. */
static bool ac_stamp_jacobian(PhysicsAcSystem *sys,
                              const PhysicsExecutionContext *context,
                              PhysicsAccumulator *scratch,
                              const PhysicsPrimitive *p,
                              const PhysicsInstruction *ins) {
  PhysicsExecutionContext op;
  size_t r, c, n;
  if (!context || !scratch || !p->ops || !p->ops->stamp)
    return false;
  physics2_accumulator_clear(scratch);
  op = *context;
  op.accumulator = scratch;
  op.timestep = 0.0;
  if (!p->ops->stamp(p, &op, ins->terminals, ins->terminal_count, ins->branch))
    return false;
  n = physics2_accumulator_size(scratch);
  for (r = 0; r < n; r++)
    for (c = 0; c < n; c++) {
      double v = physics2_accumulator_get(scratch, r, c);
      if (v == 0.0 || !isfinite(v))
        continue;
      if (!physics2_ac_add(sys, r, c, v, 0.0))
        return false;
    }
  return true;
}

static bool ac_stamp_instruction(PhysicsAcSystem *sys,
                                 const PhysicsExecutionContext *context,
                                 PhysicsAccumulator *scratch,
                                 const PhysicsProgram *program,
                                 const PhysicsInstruction *ins, double omega) {
  PhysicsPrimitive *p;
  NodeId t0, t1, t2, t3;
  BranchId br;
  double val, wL;

  if (!sys || !program || !ins)
    return false;
  p = physics2_registry_get(&program->primitives, ins->primitive_id);
  if (!p)
    return false;
  t0 = ins->terminals[0];
  t1 = ins->terminals[1];
  t2 = ins->terminal_count > 2 ? ins->terminals[2] : PHYSICS2_NODE_NONE;
  t3 = ins->terminal_count > 3 ? ins->terminals[3] : PHYSICS2_NODE_NONE;
  br = ins->branch;

  switch (p->kind) {
  case PHYS_PRIM_RESISTOR:
  case PHYS_PRIM_SWITCH: {
    if (p->kind == PHYS_PRIM_SWITCH)
      val = p->payload.sw.on ? p->payload.sw.ron : p->payload.sw.roff;
    else
      val = p->payload.two_terminal.value.nominal;
    if (!(val > 0.0))
      return false;
    return ac_stamp_admittance(sys, t0, t1, 1.0 / val, 0.0);
  }
  case PHYS_PRIM_BATTERY: {
    /* Thevenin: G + Voc shorted for AC (DC bias only) → just Rint */
    val = p->payload.battery.rint;
    if (!(val > 0.0))
      return false;
    return ac_stamp_admittance(sys, t0, t1, 1.0 / val, 0.0);
  }
  case PHYS_PRIM_CAPACITOR:
    val = p->payload.two_terminal.value.nominal;
    if (!(val > 0.0) || !(omega > 0.0))
      return false;
    /* Y = jωC */
    return ac_stamp_admittance(sys, t0, t1, 0.0, omega * val);
  case PHYS_PRIM_INDUCTOR:
    if (br == PHYSICS2_BRANCH_NONE || !(omega > 0.0))
      return false;
    val = p->payload.two_terminal.value.nominal;
    if (!(val > 0.0))
      return false;
    wL = omega * val;
    /* Vp - Vn - jωL I = 0; I through branch */
    if (!physics2_ac_add(sys, t0, br, 1.0, 0.0) ||
        !physics2_ac_add(sys, t1, br, -1.0, 0.0) ||
        !physics2_ac_add(sys, br, t0, 1.0, 0.0) ||
        !physics2_ac_add(sys, br, t1, -1.0, 0.0) ||
        !physics2_ac_add(sys, br, br, 0.0, -wL))
      return false;
    return true;
  case PHYS_PRIM_VSOURCE:
    if (br == PHYSICS2_BRANCH_NONE)
      return false;
    val = p->payload.two_terminal.value.nominal; /* AC phasor ∠0 */
    if (!isfinite(val))
      return false;
    if (!physics2_ac_add(sys, t0, br, 1.0, 0.0) ||
        !physics2_ac_add(sys, t1, br, -1.0, 0.0) ||
        !physics2_ac_add(sys, br, t0, 1.0, 0.0) ||
        !physics2_ac_add(sys, br, t1, -1.0, 0.0) ||
        !physics2_ac_add_rhs(sys, br, val, 0.0))
      return false;
    return true;
  case PHYS_PRIM_ISOURCE:
    val = p->payload.two_terminal.value.nominal;
    if (!isfinite(val))
      return false;
    return physics2_ac_add_rhs(sys, t0, -val, 0.0) &&
           physics2_ac_add_rhs(sys, t1, val, 0.0);
  case PHYS_PRIM_VCVS:
  case PHYS_PRIM_OPAMP: {
    double mu;
    if (p->kind == PHYS_PRIM_OPAMP && p->payload.opamp.railed)
      return ac_stamp_jacobian(sys, context, scratch, p, ins);
    mu = (p->kind == PHYS_PRIM_OPAMP)
                    ? p->payload.opamp.gain
                    : p->payload.controlled_source.gain.nominal;
    if (br == PHYSICS2_BRANCH_NONE || !isfinite(mu))
      return false;
    /* out+:t0 out-:t1 ctrl+:t2 ctrl-:t3 */
    return physics2_ac_add(sys, t0, br, 1.0, 0.0) &&
           physics2_ac_add(sys, t1, br, -1.0, 0.0) &&
           physics2_ac_add(sys, br, t0, 1.0, 0.0) &&
           physics2_ac_add(sys, br, t1, -1.0, 0.0) &&
           physics2_ac_add(sys, br, t2, -mu, 0.0) &&
           physics2_ac_add(sys, br, t3, mu, 0.0);
  }
  case PHYS_PRIM_VCCS: {
    double gm = p->payload.controlled_source.gain.nominal;
    if (!isfinite(gm))
      return false;
    /* I from t0→t1 = gm*(V_t2 - V_t3) */
    return physics2_ac_add(sys, t0, t2, gm, 0.0) &&
           physics2_ac_add(sys, t0, t3, -gm, 0.0) &&
           physics2_ac_add(sys, t1, t2, -gm, 0.0) &&
           physics2_ac_add(sys, t1, t3, gm, 0.0);
  }
  default:
    /* Diode/BJT/MOS/LDO/CCxS: linearized at the DC operating point. */
    return ac_stamp_jacobian(sys, context, scratch, p, ins);
  }
}

bool physics2_context_step_ac(const PhysicsExecutionContext *context,
                              NodeId reference_node, double omega_rad,
                              double *x_re, double *x_im) {
  PhysicsAcSystem *sys = NULL;
  PhysicsAccumulator *scratch = NULL;
  size_t i;
  int ok = 0;

  if (!context || !context->program || !x_re || !x_im)
    return false;
  if (!(omega_rad > 0.0) || !isfinite(omega_rad))
    return false;
  if (context->solution_size == 0 || reference_node >= context->solution_size)
    return false;

  sys = physics2_ac_create(context->solution_size);
  scratch = physics2_accumulator_create(context->solution_size);
  if (!sys || !scratch)
    goto done;

  for (i = 0; i < context->program->instruction_count; i++) {
    if (!ac_stamp_instruction(sys, context, scratch, context->program,
                              &context->program->instructions[i], omega_rad))
      goto done;
  }

  if (!physics2_ac_solve(sys, reference_node, x_re, x_im))
    goto done;
  ok = 1;
done:
  physics2_accumulator_free(scratch);
  physics2_ac_free(sys);
  return ok != 0;
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

  /* DC operating point: capacitor is open circuit, but must remain in the
   * instruction stream (not omitted). timestep==0 means DC analysis. */
  if (context->timestep == 0.0)
    return true;

  if (context->timestep < 0.0)
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
  double l_over_dt;
  double previous_current;
  size_t state_index;
  size_t p;
  size_t n;
  size_t b;

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

  /* Always use branch current I_L.
   * DC: Vp - Vn = 0 (ideal short).
   * BE: Vp - Vn - (L/dt) I = -(L/dt) I_prev
   */
  if (!physics2_accumulator_add(context->accumulator, p, b, 1.0))
    return false;
  if (!physics2_accumulator_add(context->accumulator, n, b, -1.0))
    return false;
  if (!physics2_accumulator_add(context->accumulator, b, p, 1.0))
    return false;
  if (!physics2_accumulator_add(context->accumulator, b, n, -1.0))
    return false;

  if (context->timestep == 0.0)
    return true; /* V=0 short; RHS[b]=0 */

  if (context->timestep < 0.0)
    return false;

  inductance = primitive->payload.two_terminal.value.nominal;
  if (!isfinite(inductance) || inductance <= 0.0)
    return false;

  if (!inductor_find_state_index(primitive, context, &state_index))
    return false;
  if (state_index >= context->state_count)
    return false;

  previous_current = context->states[state_index].inductor_previous_current;
  l_over_dt = inductance / context->timestep;

  if (!physics2_accumulator_add(context->accumulator, b, b, -l_over_dt))
    return false;
  if (!physics2_accumulator_add_rhs(context->accumulator, b,
                                    -l_over_dt * previous_current))
    return false;

  return true;
}

static uint8_t inductor_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t inductor_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 1; /* branch current I_L (DC short + transient BE) */
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
 * Diode — Shockley companion linearization (Newton owned by runtime)
 *
 *   I = Is * (exp(Vd/(n*Vt)) - 1)
 *   Gd = dI/dVd
 *   Ieq = I - Gd * Vd_lim
 *   stamp Gd like R; RHS ± Ieq
 *
 * Domain: the law is exact for Vd/(n·Vt) <= JUNCTION_XMAX (80, i.e. Is·5e34
 * A: beyond any physical current) and continued linearly (C1) above it, so
 * exp never overflows. The model is never distorted inside the physical
 * range; convergence comes from the runtime's per-step voltage limit.
 * ========================================================= */

#define JUNCTION_XMAX 80.0

/* e^x (exact for x <= XMAX, C1 linear above) and its derivative. */
static void junction_exp(double x, double *e, double *de) {
  if (x > JUNCTION_XMAX) {
    double emax = exp(JUNCTION_XMAX);
    *e = emax * (1.0 + (x - JUNCTION_XMAX));
    *de = emax;
  } else {
    *e = exp(x);
    *de = *e;
  }
}

static void diode_shockley(double isat, double n, double vt, double vd,
                           double *current_a, double *conductance_s) {
  double e;
  double de;

  junction_exp(vd / (n * vt), &e, &de);
  *current_a = isat * (e - 1.0);
  *conductance_s = isat * de / (n * vt);
}

static bool diode_stamp(const PhysicsPrimitive *primitive,
                        PhysicsExecutionContext *context, const NodeId *terminals,
                        uint8_t terminal_count, BranchId branch) {
  double isat;
  double n;
  double vt;
  double va;
  double vc;
  double vd;
  double current;
  double gd;
  double ieq;

  (void)branch;

  if (!primitive || !context || !context->accumulator || !terminals)
    return false;

  if (terminal_count != 2)
    return false;

  isat = primitive->payload.diode.isat.nominal;
  n = primitive->payload.diode.n;
  vt = primitive->payload.diode.vt;

  if (!isfinite(isat) || isat <= 0.0 || !isfinite(n) || n <= 0.0 ||
      !isfinite(vt) || vt <= 0.0)
    return false;

  va = 0.0;
  vc = 0.0;
  if (context->solution && context->solution_size > 0) {
    if (terminals[0] >= context->solution_size ||
        terminals[1] >= context->solution_size)
      return false;
    va = context->solution[terminals[0]];
    vc = context->solution[terminals[1]];
  }

  vd = va - vc;
  diode_shockley(isat, n, vt, vd, &current, &gd);
  /* Companion about Vd: I ≈ Gd·V + (I(Vd) - Gd·Vd). */
  ieq = current - gd * vd;

  if (!physics2_accumulator_add(context->accumulator, terminals[0], terminals[0],
                                gd))
    return false;
  if (!physics2_accumulator_add(context->accumulator, terminals[0], terminals[1],
                                -gd))
    return false;
  if (!physics2_accumulator_add(context->accumulator, terminals[1], terminals[0],
                                -gd))
    return false;
  if (!physics2_accumulator_add(context->accumulator, terminals[1], terminals[1],
                                gd))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[0], -ieq))
    return false;
  if (!physics2_accumulator_add_rhs(context->accumulator, terminals[1], ieq))
    return false;

  return true;
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
          "Primitive %s: DIODE Is=%.12g A n=%.6g Vt=%.6g V +/- %.6g%%\n",
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
 * Transistor family
 * BJT: Ebers-Moll DC (model = ebers_moll). Terminals C,B,E.
 * MOS: stamp still unsupported (Phase 11).
 * ========================================================= */

static void bjt_diode_branch(double ies_or_ics, double vt, double v_junc,
                             double *i_branch, double *gd) {
  double e;
  double de;
  junction_exp(v_junc / vt, &e, &de);
  *i_branch = ies_or_ics * (e - 1.0);
  *gd = ies_or_ics * de / vt;
}

static bool bjt_ebers_moll_stamp(const PhysicsPrimitive *primitive,
                                 PhysicsExecutionContext *context,
                                 const NodeId *terminals) {
  NodeId nc, nb, ne;
  double vc, vb, ve;
  double vbe, vbc;
  double isat, af, ar, vt;
  double ies, ics;
  double i_f, i_r, gd_f, gd_r;
  double i_c, i_b, i_e;
  double dIc_dVc, dIc_dVb, dIc_dVe;
  double dIb_dVc, dIb_dVb, dIb_dVe;
  double dIe_dVc, dIe_dVb, dIe_dVe;
  double ieq_c, ieq_b, ieq_e;
  double scale;

  if (!primitive || !context || !context->accumulator || !terminals)
    return false;

  nc = terminals[0];
  nb = terminals[1];
  ne = terminals[2];
  if (nc == PHYSICS2_NODE_NONE || nb == PHYSICS2_NODE_NONE ||
      ne == PHYSICS2_NODE_NONE)
    return false;

  isat = primitive->payload.transistor.isat;
  af = primitive->payload.transistor.alpha_f;
  ar = primitive->payload.transistor.alpha_r;
  vt = primitive->payload.transistor.vt;
  scale = primitive->payload.transistor.scale.nominal;
  if (!(isat > 0.0) || !(af > 0.0 && af < 1.0) || !(ar > 0.0 && ar < 1.0) ||
      !(vt > 0.0) || !(scale > 0.0))
    return false;

  ies = (isat * scale) / af;
  ics = (isat * scale) / ar;

  vc = vb = ve = 0.0;
  if (context->solution && context->solution_size > 0) {
    if (nc >= context->solution_size || nb >= context->solution_size ||
        ne >= context->solution_size)
      return false;
    vc = context->solution[nc];
    vb = context->solution[nb];
    ve = context->solution[ne];
  }

  vbe = vb - ve;
  vbc = vb - vc;
  bjt_diode_branch(ies, vt, vbe, &i_f, &gd_f);
  bjt_diode_branch(ics, vt, vbc, &i_r, &gd_r);

  /* Terminal currents into the device (KCL: Ic+Ib+Ie=0). */
  i_c = af * i_f - i_r;
  i_e = -i_f + ar * i_r;
  i_b = (1.0 - af) * i_f + (1.0 - ar) * i_r;

  /* Jacobian ∂I/∂V with VBE=Vb-Ve, VBC=Vb-Vc. */
  dIc_dVb = af * gd_f - gd_r;
  dIc_dVe = -af * gd_f;
  dIc_dVc = gd_r;

  dIe_dVb = -gd_f + ar * gd_r;
  dIe_dVe = gd_f;
  dIe_dVc = -ar * gd_r;

  dIb_dVb = (1.0 - af) * gd_f + (1.0 - ar) * gd_r;
  dIb_dVe = -(1.0 - af) * gd_f;
  dIb_dVc = -(1.0 - ar) * gd_r;

  ieq_c = i_c - dIc_dVc * vc - dIc_dVb * vb - dIc_dVe * ve;
  ieq_b = i_b - dIb_dVc * vc - dIb_dVb * vb - dIb_dVe * ve;
  ieq_e = i_e - dIe_dVc * vc - dIe_dVb * vb - dIe_dVe * ve;

  if (!physics2_accumulator_add(context->accumulator, nc, nc, dIc_dVc) ||
      !physics2_accumulator_add(context->accumulator, nc, nb, dIc_dVb) ||
      !physics2_accumulator_add(context->accumulator, nc, ne, dIc_dVe) ||
      !physics2_accumulator_add(context->accumulator, nb, nc, dIb_dVc) ||
      !physics2_accumulator_add(context->accumulator, nb, nb, dIb_dVb) ||
      !physics2_accumulator_add(context->accumulator, nb, ne, dIb_dVe) ||
      !physics2_accumulator_add(context->accumulator, ne, nc, dIe_dVc) ||
      !physics2_accumulator_add(context->accumulator, ne, nb, dIe_dVb) ||
      !physics2_accumulator_add(context->accumulator, ne, ne, dIe_dVe))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, nc, -ieq_c) ||
      !physics2_accumulator_add_rhs(context->accumulator, nb, -ieq_b) ||
      !physics2_accumulator_add_rhs(context->accumulator, ne, -ieq_e))
    return false;

  return true;
}

/* Level-1 NMOS constitutive + Jacobian in (Vgs, Vds). Ig=0, Is=−Id. */
static void mos_nmos_id(double vgs, double vds, double vth, double k,
                        double lambda, double *id, double *dId_dVgs,
                        double *dId_dVds) {
  double vov = vgs - vth;
  *id = 0.0;
  *dId_dVgs = 0.0;
  *dId_dVds = 0.0;
  if (!(vov > 0.0))
    return;
  if (vds < 0.0) {
    /* Reverse: evaluate with swapped DS, negate Id. */
    double id_r, g_gs, g_ds;
    mos_nmos_id(vgs, -vds, vth, k, lambda, &id_r, &g_gs, &g_ds);
    *id = -id_r;
    *dId_dVgs = -g_gs;
    *dId_dVds = g_ds; /* ∂(−id_r)/∂vds = −(∂id_r/∂(−vds))*(−1) = ∂id_r/∂vds_r */
    return;
  }
  if (vds < vov) {
    *id = k * (vov * vds - 0.5 * vds * vds);
    *dId_dVgs = k * vds;
    *dId_dVds = k * (vov - vds);
  } else {
    *id = 0.5 * k * vov * vov * (1.0 + lambda * vds);
    *dId_dVgs = k * vov * (1.0 + lambda * vds);
    *dId_dVds = 0.5 * k * vov * vov * lambda;
  }
}

static bool mos_level1_stamp(const PhysicsPrimitive *primitive,
                             PhysicsExecutionContext *context,
                             const NodeId *terminals, int pmos) {
  NodeId nd, ng, ns;
  double vd, vg, vs;
  double vgs, vds, vth, k, lambda, id;
  double dId_dVgs, dId_dVds;
  double dId_dVd, dId_dVg, dId_dVs;
  double dIs_dVd, dIs_dVg, dIs_dVs;
  double ieq_d, ieq_s;
  double scale;

  if (!primitive || !context || !context->accumulator || !terminals)
    return false;

  nd = terminals[0];
  ng = terminals[1];
  ns = terminals[2];
  if (nd == PHYSICS2_NODE_NONE || ng == PHYSICS2_NODE_NONE ||
      ns == PHYSICS2_NODE_NONE)
    return false;

  vth = primitive->payload.transistor.vth;
  k = primitive->payload.transistor.k;
  lambda = primitive->payload.transistor.lambda;
  scale = primitive->payload.transistor.scale.nominal;
  if (!(vth > 0.0) || !(k > 0.0) || !(lambda >= 0.0) || !(scale > 0.0))
    return false;
  k *= scale;

  vd = vg = vs = 0.0;
  if (context->solution && context->solution_size > 0) {
    if (nd >= context->solution_size || ng >= context->solution_size ||
        ns >= context->solution_size)
      return false;
    vd = context->solution[nd];
    vg = context->solution[ng];
    vs = context->solution[ns];
  }

  if (pmos) {
    /* Map PMOS → NMOS-equivalent polarity: negate all node voltages. */
    vgs = (-vg) - (-vs);
    vds = (-vd) - (-vs);
  } else {
    vgs = vg - vs;
    vds = vd - vs;
  }

  mos_nmos_id(vgs, vds, vth, k, lambda, &id, &dId_dVgs, &dId_dVds);

  /* ∂/∂ physical voltages. For PMOS, v_n = −v_p ⇒ ∂f/∂v_p = ∂f/∂v_n * (−1),
   * and Id_p = −id_n ⇒ overall G matches ∂id_n/∂v_n; Id flips. */
  if (pmos)
    id = -id;

  dId_dVd = dId_dVds;
  dId_dVg = dId_dVgs;
  dId_dVs = -dId_dVgs - dId_dVds;
  dIs_dVd = -dId_dVd;
  dIs_dVg = -dId_dVg;
  dIs_dVs = -dId_dVs;

  ieq_d = id - dId_dVd * vd - dId_dVg * vg - dId_dVs * vs;
  ieq_s = (-id) - dIs_dVd * vd - dIs_dVg * vg - dIs_dVs * vs;

  if (!physics2_accumulator_add(context->accumulator, nd, nd, dId_dVd) ||
      !physics2_accumulator_add(context->accumulator, nd, ng, dId_dVg) ||
      !physics2_accumulator_add(context->accumulator, nd, ns, dId_dVs) ||
      !physics2_accumulator_add(context->accumulator, ns, nd, dIs_dVd) ||
      !physics2_accumulator_add(context->accumulator, ns, ng, dIs_dVg) ||
      !physics2_accumulator_add(context->accumulator, ns, ns, dIs_dVs))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, nd, -ieq_d) ||
      !physics2_accumulator_add_rhs(context->accumulator, ns, -ieq_s))
    return false;

  return true;
}

static bool transistor_stamp(const PhysicsPrimitive *primitive,
                             PhysicsExecutionContext *context,
                             const NodeId *terminals, uint8_t terminal_count,
                             BranchId branch) {
  (void)branch;

  if (!primitive || terminal_count != 3)
    return false;

  if (primitive->payload.transistor.subtype == PHYS_XSTR_BJT)
    return bjt_ebers_moll_stamp(primitive, context, terminals);

  if (primitive->payload.transistor.subtype == PHYS_XSTR_NMOS)
    return mos_level1_stamp(primitive, context, terminals, 0);

  if (primitive->payload.transistor.subtype == PHYS_XSTR_PMOS)
    return mos_level1_stamp(primitive, context, terminals, 1);

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
    subtype = "BJT(ebers_moll)";
    break;
  case PHYS_XSTR_NMOS:
    subtype = "NMOS(level1)";
    break;
  case PHYS_XSTR_PMOS:
    subtype = "PMOS(level1)";
    break;
  default:
    subtype = "UNKNOWN";
    break;
  }

  if (primitive->payload.transistor.subtype == PHYS_XSTR_BJT) {
    fprintf(stream,
            "Primitive %s: %s Is=%.4g αF=%.4g αR=%.4g Vt=%.4g scale=%.4g\n",
            primitive->name, subtype, primitive->payload.transistor.isat,
            primitive->payload.transistor.alpha_f,
            primitive->payload.transistor.alpha_r,
            primitive->payload.transistor.vt,
            primitive->payload.transistor.scale.nominal);
  } else if (primitive->payload.transistor.subtype == PHYS_XSTR_NMOS ||
             primitive->payload.transistor.subtype == PHYS_XSTR_PMOS) {
    fprintf(stream,
            "Primitive %s: %s Vth=%.4g K=%.4g λ=%.4g scale=%.4g\n",
            primitive->name, subtype, primitive->payload.transistor.vth,
            primitive->payload.transistor.k,
            primitive->payload.transistor.lambda,
            primitive->payload.transistor.scale.nominal);
  } else {
    fprintf(stream, "Primitive %s: TRANSISTOR %s [unsupported]\n",
            primitive->name, subtype);
  }
}

static const PhysicsPrimitiveOps transistor_ops = {
    transistor_stamp, transistor_terminal_count, transistor_extra_unknowns,
    transistor_print};

static bool init_transistor_mos(PhysicsPrimitive *primitive, const char *name,
                                double vth_v, double k_a_per_v2,
                                double lambda_per_v, double scale,
                                double tolerance_pct,
                                PhysicsTransistorSubtype subtype) {
  if (!primitive || !name)
    return false;
  if (!isfinite(vth_v) || vth_v <= 0.0)
    return false;
  if (!isfinite(k_a_per_v2) || k_a_per_v2 <= 0.0)
    return false;
  if (!isfinite(lambda_per_v) || lambda_per_v < 0.0)
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
  primitive->payload.transistor.vth = vth_v;
  primitive->payload.transistor.k = k_a_per_v2;
  primitive->payload.transistor.lambda = lambda_per_v;
  return true;
}

bool physics2_primitive_init_bjt(PhysicsPrimitive *primitive, const char *name,
                                 double isat_a, double alpha_f, double alpha_r,
                                 double vt_v, double tolerance_pct) {
  if (!primitive || !name)
    return false;
  if (!isfinite(isat_a) || isat_a <= 0.0)
    return false;
  if (!isfinite(alpha_f) || alpha_f <= 0.0 || alpha_f >= 1.0)
    return false;
  if (!isfinite(alpha_r) || alpha_r <= 0.0 || alpha_r >= 1.0)
    return false;
  if (!isfinite(vt_v) || vt_v <= 0.0)
    return false;
  if (!isfinite(tolerance_pct) || tolerance_pct < 0.0)
    return false;

  memset(primitive, 0, sizeof(*primitive));
  primitive->ops = &transistor_ops;
  primitive->kind = PHYS_PRIM_TRANSISTOR;
  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';
  primitive->payload.transistor.subtype = PHYS_XSTR_BJT;
  primitive->payload.transistor.scale.nominal = 1.0;
  primitive->payload.transistor.scale.tolerance_pct = tolerance_pct;
  primitive->payload.transistor.terminal_count = 3;
  primitive->payload.transistor.isat = isat_a;
  primitive->payload.transistor.alpha_f = alpha_f;
  primitive->payload.transistor.alpha_r = alpha_r;
  primitive->payload.transistor.vt = vt_v;
  return true;
}

bool physics2_primitive_init_nmos(PhysicsPrimitive *primitive, const char *name,
                                  double vth_v, double k_a_per_v2,
                                  double lambda_per_v, double scale,
                                  double tolerance_pct) {
  return init_transistor_mos(primitive, name, vth_v, k_a_per_v2, lambda_per_v,
                             scale, tolerance_pct, PHYS_XSTR_NMOS);
}

bool physics2_primitive_init_pmos(PhysicsPrimitive *primitive, const char *name,
                                  double vth_v, double k_a_per_v2,
                                  double lambda_per_v, double scale,
                                  double tolerance_pct) {
  return init_transistor_mos(primitive, name, vth_v, k_a_per_v2, lambda_per_v,
                             scale, tolerance_pct, PHYS_XSTR_PMOS);
}

/* =========================================================
 * Phase 12: SWITCH / OPAMP / LDO / BATTERY
 * ========================================================= */

static bool switch_stamp(const PhysicsPrimitive *primitive,
                         PhysicsExecutionContext *context,
                         const NodeId *terminals, uint8_t terminal_count,
                         BranchId branch) {
  NodeId a, b;
  double r, g;
  (void)branch;
  if (!primitive || !context || !terminals || terminal_count != 2)
    return false;
  a = terminals[0];
  b = terminals[1];
  if (a == PHYSICS2_NODE_NONE || b == PHYSICS2_NODE_NONE)
    return false;
  r = primitive->payload.sw.on ? primitive->payload.sw.ron
                               : primitive->payload.sw.roff;
  if (!(r > 0.0) || !isfinite(r))
    return false;
  g = 1.0 / r;
  return physics2_accumulator_add(context->accumulator, a, a, g) &&
         physics2_accumulator_add(context->accumulator, a, b, -g) &&
         physics2_accumulator_add(context->accumulator, b, a, -g) &&
         physics2_accumulator_add(context->accumulator, b, b, g);
}

static uint8_t switch_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t switch_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void switch_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;
  fprintf(stream, "Primitive %s: SWITCH %s Ron=%.4g Roff=%.4g\n",
          primitive->name, primitive->payload.sw.on ? "ON" : "OFF",
          primitive->payload.sw.ron, primitive->payload.sw.roff);
}

static const PhysicsPrimitiveOps switch_ops = {
    switch_stamp, switch_terminal_count, switch_extra_unknowns, switch_print};

static bool opamp_stamp(const PhysicsPrimitive *primitive,
                        PhysicsExecutionContext *context,
                        const NodeId *terminals, uint8_t terminal_count,
                        BranchId branch) {
  /* Same MNA as VCVS: out = A*(in+ - in-). */
  PhysicsPrimitive tmp;
  NodeId out, ref, vcc;
  double y, span, a;
  if (!primitive)
    return false;
  tmp = *primitive;
  tmp.payload.controlled_source.gain.nominal = primitive->payload.opamp.gain;
  tmp.payload.controlled_source.gain.tolerance_pct = 0.0;
  if (!primitive->payload.opamp.railed)
    return vcvs_stamp(&tmp, context, terminals, terminal_count, branch);

  /* Railed: linear inside [0, span]; else branch row pins out to a rail. */
  if (!context || !terminals || terminal_count != 5 ||
      branch == PHYSICS2_BRANCH_NONE ||
      terminals[4] >= context->solution_size ||
      !controlled_nodes_valid(terminals, context))
    return false;
  out = terminals[0];
  ref = terminals[1];
  vcc = terminals[4];
  a = primitive->payload.opamp.gain;
  y = a * (context->solution[terminals[2]] - context->solution[terminals[3]]);
  span = context->solution[vcc] - context->solution[ref];
  if (y >= 0.0 && y <= span)
    return vcvs_stamp(&tmp, context, terminals, 4, branch);
  return physics2_accumulator_add(context->accumulator, out, branch, 1.0) &&
         physics2_accumulator_add(context->accumulator, ref, branch, -1.0) &&
         physics2_accumulator_add(context->accumulator, branch, out, 1.0) &&
         physics2_accumulator_add(context->accumulator, branch,
                                  y > span ? vcc : ref, -1.0);
}

static uint8_t opamp_terminal_count(const PhysicsPrimitive *primitive) {
  return primitive && primitive->payload.opamp.railed ? 5 : 4;
}

static uint8_t opamp_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 1;
}

static void opamp_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;
  fprintf(stream, "Primitive %s: OPAMP(behavioral) A=%.6g\n", primitive->name,
          primitive->payload.opamp.gain);
}

static const PhysicsPrimitiveOps opamp_ops = {
    opamp_stamp, opamp_terminal_count, opamp_extra_unknowns, opamp_print};

static bool ldo_stamp(const PhysicsPrimitive *primitive,
                      PhysicsExecutionContext *context, const NodeId *terminals,
                      uint8_t terminal_count, BranchId branch) {
  NodeId n_in, n_out, n_gnd;
  double vin, vout, vgnd, vset, g, rout, vtarget, vdrop, ilimit;
  double dVset_dVin, i_out, ieq_out, ieq_gnd;
  double dI_dVin, dI_dVout, dI_dVgnd;
  (void)branch;

  if (!primitive || !context || !terminals || terminal_count != 3)
    return false;
  n_in = terminals[0];
  n_out = terminals[1];
  n_gnd = terminals[2];
  if (n_in == PHYSICS2_NODE_NONE || n_out == PHYSICS2_NODE_NONE ||
      n_gnd == PHYSICS2_NODE_NONE)
    return false;

  vtarget = primitive->payload.ldo.vtarget;
  vdrop = primitive->payload.ldo.vdropout;
  rout = primitive->payload.ldo.rout;
  ilimit = primitive->payload.ldo.ilimit;
  if (!(vtarget > 0.0) || !(vdrop >= 0.0) || !(rout > 0.0))
    return false;
  g = 1.0 / rout;

  vin = vout = vgnd = 0.0;
  if (context->solution && context->solution_size > 0) {
    if (n_in >= context->solution_size || n_out >= context->solution_size ||
        n_gnd >= context->solution_size)
      return false;
    vin = context->solution[n_in];
    vout = context->solution[n_out];
    vgnd = context->solution[n_gnd];
  }

  /* Vset = min(Vtarget, (Vin-Vgnd) - Vdropout); floor at 0 */
  {
    double head = (vin - vgnd) - vdrop;
    if (head >= vtarget) {
      vset = vtarget;
      dVset_dVin = 0.0;
    } else {
      vset = head;
      dVset_dVin = 1.0;
    }
    if (vset < 0.0) {
      vset = 0.0;
      dVset_dVin = 0.0;
    }
  }

  /* I into OUT = Vset*g - (Vout-Vgnd)*g ; optional Ilimit clamp */
  i_out = vset * g - (vout - vgnd) * g;
  if (ilimit > 0.0 && i_out > ilimit) {
    /* Current-source mode: I = Ilimit. The exact derivative is 0, which
     * leaves an unloaded output floating mid-Newton; a 1e-6·g slope keeps
     * the Jacobian regular and still gives I = Ilimit at the linearization
     * point, so converged current-limit solutions are unchanged. */
    i_out = ilimit;
    dI_dVin = 0.0;
    dI_dVout = -1.0e-6 * g;
    dI_dVgnd = 1.0e-6 * g;
    ieq_out = i_out - dI_dVout * vout - dI_dVgnd * vgnd;
    ieq_gnd = -i_out + dI_dVout * vout + dI_dVgnd * vgnd;
  } else {
    /* Vset = a*Vin + b*Vgnd + c; regulation: a=0,b=0; dropout: a=1,b=-1 */
    dI_dVin = dVset_dVin * g;
    dI_dVout = -g;
    if (dVset_dVin > 0.5) {
      /* dropout: Vset=Vin-Vgnd-Vd → I = (Vin-Vd)/R - Vout/R  (Vgnd cancels) */
      dI_dVgnd = 0.0;
    } else {
      dI_dVgnd = g; /* regulation: ∂/∂Vgnd of -(Vout-Vgnd)*g */
    }
    ieq_out = i_out - dI_dVin * vin - dI_dVout * vout - dI_dVgnd * vgnd;
    ieq_gnd = -i_out - (-dI_dVin) * vin - (-dI_dVout) * vout -
              (-dI_dVgnd) * vgnd;
  }

  /* The current delivered into OUT leaves VIN (pass element), not GND. */
  if (!physics2_accumulator_add(context->accumulator, n_out, n_in, dI_dVin) ||
      !physics2_accumulator_add(context->accumulator, n_out, n_out, dI_dVout) ||
      !physics2_accumulator_add(context->accumulator, n_out, n_gnd, dI_dVgnd) ||
      !physics2_accumulator_add(context->accumulator, n_in, n_in, -dI_dVin) ||
      !physics2_accumulator_add(context->accumulator, n_in, n_out,
                                -dI_dVout) ||
      !physics2_accumulator_add(context->accumulator, n_in, n_gnd, -dI_dVgnd))
    return false;

  if (!physics2_accumulator_add_rhs(context->accumulator, n_out, -ieq_out) ||
      !physics2_accumulator_add_rhs(context->accumulator, n_in, -ieq_gnd))
    return false;

  return true;
}

static uint8_t ldo_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 3;
}

static uint8_t ldo_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void ldo_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;
  fprintf(stream,
          "Primitive %s: LDO(behavioral_lumped) Vtarget=%.4g Vdo=%.4g "
          "Rout=%.4g Ilim=%.4g\n",
          primitive->name, primitive->payload.ldo.vtarget,
          primitive->payload.ldo.vdropout, primitive->payload.ldo.rout,
          primitive->payload.ldo.ilimit);
}

static const PhysicsPrimitiveOps ldo_ops = {ldo_stamp, ldo_terminal_count,
                                            ldo_extra_unknowns, ldo_print};

static bool battery_stamp(const PhysicsPrimitive *primitive,
                          PhysicsExecutionContext *context,
                          const NodeId *terminals, uint8_t terminal_count,
                          BranchId branch) {
  NodeId np, nn;
  double voc, r, g;
  (void)branch;
  if (!primitive || !context || !terminals || terminal_count != 2)
    return false;
  np = terminals[0];
  nn = terminals[1];
  if (np == PHYSICS2_NODE_NONE || nn == PHYSICS2_NODE_NONE)
    return false;
  voc = primitive->payload.battery.voc;
  r = primitive->payload.battery.rint;
  if (!(r > 0.0) || !isfinite(voc) || !isfinite(r))
    return false;
  g = 1.0 / r;
  /* Thevenin: G between +/- plus I = Voc*G from − into + */
  if (!physics2_accumulator_add(context->accumulator, np, np, g) ||
      !physics2_accumulator_add(context->accumulator, np, nn, -g) ||
      !physics2_accumulator_add(context->accumulator, nn, np, -g) ||
      !physics2_accumulator_add(context->accumulator, nn, nn, g))
    return false;
  if (!physics2_accumulator_add_rhs(context->accumulator, np, voc * g) ||
      !physics2_accumulator_add_rhs(context->accumulator, nn, -voc * g))
    return false;
  return true;
}

static uint8_t battery_terminal_count(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 2;
}

static uint8_t battery_extra_unknowns(const PhysicsPrimitive *primitive) {
  (void)primitive;
  return 0;
}

static void battery_print(const PhysicsPrimitive *primitive, FILE *stream) {
  if (!primitive || !stream)
    return;
  fprintf(stream, "Primitive %s: BATTERY Voc=%.4g Rint=%.4g\n", primitive->name,
          primitive->payload.battery.voc, primitive->payload.battery.rint);
}

static const PhysicsPrimitiveOps battery_ops = {
    battery_stamp, battery_terminal_count, battery_extra_unknowns,
    battery_print};

bool physics2_primitive_init_switch(PhysicsPrimitive *primitive, const char *name,
                                    double ron_ohm, double roff_ohm, int on) {
  if (!primitive || !name)
    return false;
  if (!isfinite(ron_ohm) || ron_ohm <= 0.0)
    return false;
  if (!isfinite(roff_ohm) || roff_ohm <= 0.0)
    return false;
  memset(primitive, 0, sizeof(*primitive));
  primitive->ops = &switch_ops;
  primitive->kind = PHYS_PRIM_SWITCH;
  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';
  primitive->payload.sw.ron = ron_ohm;
  primitive->payload.sw.roff = roff_ohm;
  primitive->payload.sw.on = on ? 1 : 0;
  return true;
}

bool physics2_primitive_init_opamp(PhysicsPrimitive *primitive, const char *name,
                                   double gain) {
  if (!primitive || !name)
    return false;
  if (!isfinite(gain) || gain == 0.0)
    return false;
  memset(primitive, 0, sizeof(*primitive));
  primitive->ops = &opamp_ops;
  primitive->kind = PHYS_PRIM_OPAMP;
  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';
  primitive->payload.opamp.gain = gain;
  return true;
}

bool physics2_primitive_init_opamp_railed(PhysicsPrimitive *primitive,
                                          const char *name, double gain) {
  if (!physics2_primitive_init_opamp(primitive, name, gain))
    return false;
  primitive->payload.opamp.railed = 1;
  return true;
}

bool physics2_primitive_init_ldo(PhysicsPrimitive *primitive, const char *name,
                                 double vtarget, double vdropout, double rout,
                                 double ilimit) {
  if (!primitive || !name)
    return false;
  if (!isfinite(vtarget) || vtarget <= 0.0)
    return false;
  if (!isfinite(vdropout) || vdropout < 0.0)
    return false;
  if (!isfinite(rout) || rout <= 0.0)
    return false;
  if (!isfinite(ilimit) || ilimit < 0.0)
    return false;
  memset(primitive, 0, sizeof(*primitive));
  primitive->ops = &ldo_ops;
  primitive->kind = PHYS_PRIM_LDO;
  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';
  primitive->payload.ldo.vtarget = vtarget;
  primitive->payload.ldo.vdropout = vdropout;
  primitive->payload.ldo.rout = rout;
  primitive->payload.ldo.ilimit = ilimit;
  return true;
}

bool physics2_primitive_init_battery(PhysicsPrimitive *primitive,
                                     const char *name, double voc,
                                     double rint_ohm) {
  if (!primitive || !name)
    return false;
  if (!isfinite(voc))
    return false;
  if (!isfinite(rint_ohm) || rint_ohm <= 0.0)
    return false;
  memset(primitive, 0, sizeof(*primitive));
  primitive->ops = &battery_ops;
  primitive->kind = PHYS_PRIM_BATTERY;
  strncpy(primitive->name, name, sizeof(primitive->name) - 1);
  primitive->name[sizeof(primitive->name) - 1] = '\0';
  primitive->payload.battery.voc = voc;
  primitive->payload.battery.rint = rint_ohm;
  return true;
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

  case PHYS_PRIM_SWITCH:
    return PHYS_OP_SWITCH;

  case PHYS_PRIM_OPAMP:
    return PHYS_OP_OPAMP;

  case PHYS_PRIM_LDO:
    return PHYS_OP_LDO;

  case PHYS_PRIM_BATTERY:
    return PHYS_OP_BATTERY;

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
    case PHYS_PRIM_INDUCTOR: /* DC short / BE branch current I_L */
    case PHYS_PRIM_VCVS:
    case PHYS_PRIM_CCVS:
    case PHYS_PRIM_CCCS:
    case PHYS_PRIM_OPAMP:
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

  /* timestep == 0 → DC operating point (C open, L short). */
  if (timestep < 0.0 || !isfinite(timestep))
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

  context->newton_max_iter = 100;
  context->newton_abs_tol = 1.0e-9;
  context->newton_rel_tol = 1.0e-6;
  context->newton_max_dv = 0.25;
  context->quiet = 0;
  memset(&context->newton, 0, sizeof(context->newton));
  context->newton.status = PHYSICS2_NEWTON_LINEAR;

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

void physics2_context_set_newton_limits(PhysicsExecutionContext *context,
                                        size_t max_iter, double abs_tol,
                                        double rel_tol, double max_dv) {
  if (!context)
    return;
  if (max_iter > 0)
    context->newton_max_iter = max_iter;
  if (isfinite(abs_tol) && abs_tol > 0.0)
    context->newton_abs_tol = abs_tol;
  if (isfinite(rel_tol) && rel_tol > 0.0)
    context->newton_rel_tol = rel_tol;
  if (isfinite(max_dv) && max_dv > 0.0)
    context->newton_max_dv = max_dv;
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

    if (primitive->ops->print && !interpreter->context->quiet)
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

/* ||A(x)x - b(x)||inf at context->solution, from the current assembly. The
 * companions are exact at their own linearization point, so this is the true
 * KCL / branch residual. *scale_out gets the largest term (relative gate). */
static double assembled_residual(const PhysicsExecutionContext *context,
                                 NodeId reference_node, double *scale_out) {
  double r_norm = 0.0, scale = 0.0;
  size_t row, col;
  for (row = 0; row < context->solution_size; row++) {
    double r;
    if (row == reference_node)
      continue;
    r = -physics2_accumulator_get_rhs(context->accumulator, row);
    if (fabs(r) > scale)
      scale = fabs(r);
    for (col = 0; col < context->solution_size; col++) {
      double term;
      if (col == reference_node)
        continue;
      term = physics2_accumulator_get(context->accumulator, row, col) *
             context->solution[col];
      if (fabs(term) > scale)
        scale = fabs(term);
      r += term;
    }
    if (fabs(r) > r_norm)
      r_norm = fabs(r);
  }
  if (scale_out)
    *scale_out = scale;
  return r_norm;
}

bool physics2_context_step(PhysicsExecutionContext *context,
                           NodeId reference_node) {
  PhysicsInterpreter interpreter;
  size_t i;
  size_t iter;
  int has_nonlinear = 0;
  double *x_trial = NULL;
  double *x_cand = NULL;
  double *x_base = NULL;
  PhysicsPrimitiveState *snap_states = NULL;
  double *snap_solution = NULL;
  double snap_time = 0.0;
  int transactional = 0;

  if (!context || !context->program || !context->accumulator ||
      !context->solution)
    return false;

  /* BE transaction: snapshot; commit C/L history + time only on success. */
  if (context->timestep > 0.0) {
    transactional = 1;
    snap_time = context->time;
    if (context->state_count > 0) {
      snap_states = malloc(context->state_count * sizeof(*snap_states));
      if (!snap_states)
        return false;
      memcpy(snap_states, context->states,
             context->state_count * sizeof(*snap_states));
    }
    if (context->solution_size > 0) {
      snap_solution = malloc(context->solution_size * sizeof(*snap_solution));
      if (!snap_solution) {
        free(snap_states);
        return false;
      }
      memcpy(snap_solution, context->solution,
             context->solution_size * sizeof(*snap_solution));
    }
  }

  memset(&context->newton, 0, sizeof(context->newton));
  context->newton.status = PHYSICS2_NEWTON_LINEAR;
  context->newton.damping = 1.0;

  for (i = 0; i < context->program->instruction_count; i++) {
    PhysicsPrimitive *p = physics2_registry_get(
        &context->program->primitives,
        context->program->instructions[i].primitive_id);
    if (p && (p->kind == PHYS_PRIM_DIODE || p->kind == PHYS_PRIM_LDO ||
              (p->kind == PHYS_PRIM_OPAMP && p->payload.opamp.railed) ||
              (p->kind == PHYS_PRIM_TRANSISTOR &&
               (p->payload.transistor.subtype == PHYS_XSTR_BJT ||
                p->payload.transistor.subtype == PHYS_XSTR_NMOS ||
                p->payload.transistor.subtype == PHYS_XSTR_PMOS))))
      has_nonlinear = 1;
  }

  interpreter.program = context->program;
  interpreter.context = context;

  if (!has_nonlinear) {
    if (!physics2_interpreter_execute(&interpreter))
      goto step_fail;
    if (!physics2_accumulator_solve(context->accumulator, reference_node,
                                    context->solution)) {
      context->newton.status = PHYSICS2_NEWTON_SINGULAR;
      snprintf(context->newton.failure, sizeof(context->newton.failure),
               "MATRIX_SINGULAR");
      goto step_fail;
    }
    for (i = 0; i < context->solution_size; i++) {
      if (!isfinite(context->solution[i])) {
        context->newton.status = PHYSICS2_NEWTON_NONFINITE;
        snprintf(context->newton.failure, sizeof(context->newton.failure),
                 "NONFINITE_SOLUTION");
        goto step_fail;
      }
    }
    context->newton.status = PHYSICS2_NEWTON_LINEAR;
  } else {
    /* Global Newton: devices stamp companions; runtime owns J Δx = -F, damping. */
    const size_t max_newton =
        context->newton_max_iter > 0 ? context->newton_max_iter : 100;
    const double abs_tol =
        context->newton_abs_tol > 0.0 ? context->newton_abs_tol : 1.0e-9;
    const double rel_tol =
        context->newton_rel_tol > 0.0 ? context->newton_rel_tol : 1.0e-6;
    const double max_dv =
        context->newton_max_dv > 0.0 ? context->newton_max_dv : 0.25;

    x_trial = calloc(context->solution_size, sizeof(*x_trial));
    x_cand = calloc(context->solution_size, sizeof(*x_cand));
    x_base = calloc(context->solution_size, sizeof(*x_base));
    if (context->solution_size > 0 && (!x_trial || !x_cand || !x_base))
      goto newton_fail;

    context->quiet = 1;

    for (iter = 0; iter < max_newton; iter++) {
      double x_norm = 0.0;
      double update_norm = 0.0;
      double alpha, r0, best_r, best_alpha;
      int accepted = 0;
      size_t k;

      if (!physics2_interpreter_execute(&interpreter))
        goto newton_fail;
      r0 = assembled_residual(context, reference_node, NULL);
      if (!physics2_accumulator_solve(context->accumulator, reference_node,
                                      x_trial)) {
        context->newton.status = PHYSICS2_NEWTON_SINGULAR;
        snprintf(context->newton.failure, sizeof(context->newton.failure),
                 "MATRIX_SINGULAR");
        goto newton_fail;
      }

      for (i = 0; i < context->solution_size; i++) {
        if (!isfinite(x_trial[i])) {
          context->newton.status = PHYSICS2_NEWTON_NONFINITE;
          snprintf(context->newton.failure, sizeof(context->newton.failure),
                   "NONFINITE_TRIAL");
          goto newton_fail;
        }
        if (fabs(context->solution[i]) > x_norm)
          x_norm = fabs(context->solution[i]);
      }

      /* Full Newton direction with voltage limiting on the raw step. */
      for (i = 0; i < context->solution_size; i++) {
        double d = x_trial[i] - context->solution[i];
        if (d > max_dv)
          d = max_dv;
        if (d < -max_dv)
          d = -max_dv;
        x_trial[i] = context->solution[i] + d;
        if (fabs(d) > update_norm)
          update_norm = fabs(d);
      }

      context->newton.iterations = iter + 1;
      context->newton.update_norm = update_norm;

      if (update_norm < abs_tol ||
          update_norm < rel_tol * (1.0 + x_norm)) {
        /* Small step is necessary, not sufficient: check ||F(x)||inf. */
        double r_norm, scale;
        memcpy(context->solution, x_trial,
               context->solution_size * sizeof(*context->solution));
        if (!physics2_interpreter_execute(&interpreter))
          goto newton_fail;
        r_norm = assembled_residual(context, reference_node, &scale);
        context->newton.residual_norm = r_norm;
        context->newton.damping = 1.0;
        if (r_norm > abs_tol + rel_tol * scale) {
          context->newton.status = PHYSICS2_NEWTON_DIVERGED;
          snprintf(context->newton.failure, sizeof(context->newton.failure),
                   "NEWTON_RESIDUAL %.3g", r_norm);
          goto newton_fail;
        }
        context->newton.status = PHYSICS2_NEWTON_OK;
        accepted = 1;
        break;
      }

      /* Backtracking line search on ||F||inf: α = 1, 1/2, ... 1/64; take the
       * first α that lowers the residual, else the lowest one seen. */
      memcpy(x_base, context->solution,
             context->solution_size * sizeof(*x_base));
      best_r = INFINITY;
      best_alpha = 0.0;
      for (alpha = 1.0; alpha >= 1.0 / 64.0; alpha *= 0.5) {
        double r;
        for (k = 0; k < context->solution_size; k++) {
          x_cand[k] = x_base[k] + alpha * (x_trial[k] - x_base[k]);
          if (!isfinite(x_cand[k]))
            break;
        }
        if (k < context->solution_size)
          continue;
        memcpy(context->solution, x_cand,
               context->solution_size * sizeof(*context->solution));
        if (!physics2_interpreter_execute(&interpreter))
          goto newton_fail;
        r = assembled_residual(context, reference_node, NULL);
        if (isfinite(r) && r < best_r) {
          best_r = r;
          best_alpha = alpha;
        }
        if (r < r0)
          break;
      }
      if (best_alpha > 0.0) {
        for (k = 0; k < context->solution_size; k++)
          context->solution[k] =
              x_base[k] + best_alpha * (x_trial[k] - x_base[k]);
        context->newton.damping = best_alpha;
        context->newton.residual_norm = best_r;
        accepted = 1;
      }

      if (!accepted) {
        context->newton.status = PHYSICS2_NEWTON_NONFINITE;
        snprintf(context->newton.failure, sizeof(context->newton.failure),
                 "DAMPING_FAILED");
        goto newton_fail;
      }
    }

    context->quiet = 0;

    if (context->newton.status != PHYSICS2_NEWTON_OK) {
      context->newton.status = PHYSICS2_NEWTON_DIVERGED;
      snprintf(context->newton.failure, sizeof(context->newton.failure),
               "NEWTON_DIVERGED iter=%zu update=%.3g",
               context->newton.iterations, context->newton.update_norm);
      goto newton_fail;
    }

    free(x_trial);
    free(x_cand);
    free(x_base);
    x_trial = NULL;
    x_cand = NULL;
    x_base = NULL;
  }

  for (i = 0; i < context->program->instruction_count; i++) {
    PhysicsInstruction *instruction = &context->program->instructions[i];

    PhysicsPrimitive *primitive = physics2_registry_get(
        &context->program->primitives, instruction->primitive_id);

    if (!primitive)
      goto step_fail;

    if (primitive->kind == PHYS_PRIM_CAPACITOR) {
      NodeId p = instruction->terminals[0];
      NodeId n = instruction->terminals[1];

      if (p >= context->solution_size || n >= context->solution_size)
        goto step_fail;

      context->states[i].capacitor_previous_voltage =
          context->solution[p] - context->solution[n];
    }

    if (primitive->kind == PHYS_PRIM_INDUCTOR) {
      BranchId br = context->program->instructions[i].branch;

      if (br == PHYSICS2_BRANCH_NONE || br >= context->solution_size)
        goto step_fail;
      context->states[i].inductor_previous_current = context->solution[br];
    }
  }

  if (context->timestep > 0.0)
    context->time += context->timestep;

  free(snap_states);
  free(snap_solution);
  return true;

newton_fail:
  context->quiet = 0;
  free(x_trial);
  free(x_cand);
  free(x_base);
step_fail:
  if (transactional) {
    if (snap_states && context->states)
      memcpy(context->states, snap_states,
             context->state_count * sizeof(*snap_states));
    if (snap_solution && context->solution)
      memcpy(context->solution, snap_solution,
             context->solution_size * sizeof(*snap_solution));
    context->time = snap_time;
  }
  free(snap_states);
  free(snap_solution);
  return false;
}

bool physics2_context_run_steps(PhysicsExecutionContext *context,
                                NodeId reference_node, size_t n_steps) {
  size_t s;
  if (!context || !(context->timestep > 0.0))
    return false;
  for (s = 0; s < n_steps; s++) {
    if (!physics2_context_step(context, reference_node))
      return false;
  }
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
