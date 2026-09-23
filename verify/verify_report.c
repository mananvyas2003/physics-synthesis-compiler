#include "verify_report.h"

#include "cJSON.h"
#include "diag_error.h"
#include "physics2_interpreter.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CORNER_PARTS 10 /* 2^10 exhaustive corners; above → skipped */
#define PI 3.14159265358979323846

static int name_is(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

/* Fallback only when the IR has no "measure" (reported as heuristic). */
static int is_sense(const char *name) {
  return name_is(name, "VOUT") || name_is(name, "SENSOR_VDD") ||
         name_is(name, "ADC_SENSE") || name_is(name, "SENSE");
}

static int write_json(const char *path, cJSON *root) {
  char *printed = cJSON_Print(root);
  FILE *fp = printed ? fopen(path, "wb") : NULL;
  int ok = 0;
  if (fp) {
    fputs(printed, fp);
    fputc('\n', fp);
    fclose(fp);
    ok = 1;
  }
  free(printed);
  cJSON_Delete(root);
  return ok;
}

static int fail(const char *path, VerifyResult *out, const char *analysis,
                const char *fmt, ...) {
  va_list ap;
  cJSON *root = cJSON_CreateObject();
  va_start(ap, fmt);
  vsnprintf(out->summary, sizeof(out->summary), fmt, ap);
  va_end(ap);
  out->passed = 0;
  cJSON_AddStringToObject(root, "schema", "verification.v1");
  cJSON_AddStringToObject(root, "analysis", analysis);
  cJSON_AddBoolToObject(root, "passed", 0);
  cJSON_AddStringToObject(root, "summary", out->summary);
  write_json(path, root);
  return 1;
}

typedef struct {
  CompiledPhysicsProgram prog;
  PhysicsAccumulator *acc;
  PhysicsExecutionContext ctx;
  NodeId gnd;
} Solve;

static void solve_free(Solve *s) {
  physics2_context_free(&s->ctx);
  physics2_accumulator_free(s->acc);
  compiler_free_physics_program(&s->prog);
  memset(s, 0, sizeof(*s));
}

/* Lower + DC operating point. 0 on success; diag set on failure. */
static int solve_dc(const CompilerPhysDesign *phys, Solve *s) {
  memset(s, 0, sizeof(*s));
  if (!compiler_lower_to_physics2(phys, &s->prog)) {
    diag_set_error("Physics2 lowering failed");
    return 1;
  }
  s->gnd = compiler_physics_find_node(&s->prog, "GND");
  if (s->gnd == PHYSICS2_NODE_NONE) {
    diag_set_error("TOPOLOGY_ERROR: no GND node in the physics graph");
    return 1;
  }
  s->acc = physics2_accumulator_create(s->prog.program.next_node +
                                       s->prog.program.branch_count);
  if (!s->acc || !physics2_context_init(&s->ctx, &s->prog.program, s->acc, 0.0))
    return 1;
  s->ctx.quiet = 1;
  if (!physics2_context_step(&s->ctx, s->gnd)) {
    diag_set_error("%s", s->ctx.newton.failure[0] ? s->ctx.newton.failure
                                                   : "MATRIX_SINGULAR");
    return 1;
  }
  return 0;
}

static double vnode_ctx(const PhysicsExecutionContext *ctx,
                        const CompiledPhysicsProgram *prog, const char *net) {
  NodeId id = compiler_physics_find_node(prog, net);
  return id < ctx->solution_size ? ctx->solution[id] : 0.0;
}

static double vnode(const Solve *s, const char *net) {
  return vnode_ctx(&s->ctx, &s->prog, net);
}

#define AC_POINTS 11 /* 10 Hz .. 1 MHz, half-decade */

static double ac_freq(int d) { return 10.0 * pow(10.0, d * 0.5); }

/* |H|, phase at AC_POINTS frequencies; returns how many solved. */
static int ac_sweep(const Solve *s, const char *net, double vrail,
                    double *mag, double *ph) {
  NodeId id = compiler_physics_find_node(&s->prog, net);
  double *xr = calloc(s->ctx.solution_size, sizeof(double));
  double *xi = calloc(s->ctx.solution_size, sizeof(double));
  int d = 0;
  for (; xr && xi && id < s->ctx.solution_size && d < AC_POINTS; d++) {
    if (!physics2_context_step_ac(&s->ctx, s->gnd, 2.0 * PI * ac_freq(d), xr,
                                  xi))
      break;
    physics2_ac_mag_phase(xr[id] / vrail, xi[id] / vrail, &mag[d], &ph[d]);
  }
  free(xr);
  free(xi);
  return d;
}

static double diode_current(const CompilerPhysElement *el, double vd) {
  double x = vd / (el->diode_n * el->diode_vt);
  return el->value * (exp(x > 80.0 ? 80.0 : x) - 1.0);
}

/* Recompute BJT terminal currents at the solved OP (same law as the stamp). */
static void bjt_currents(const CompilerPhysElement *el, double vc, double vb,
                         double ve, double *ic, double *ib, double *ie) {
  const double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  double af = el->param[0], ar = el->param[1];
  double ies = el->value / af, ics = el->value / ar;
  double xf = (vb - ve) / vt, xr = (vb - vc) / vt;
  double i_f = ies * (exp(xf > 80.0 ? 80.0 : xf) - 1.0);
  double i_r = ics * (exp(xr > 80.0 ? 80.0 : xr) - 1.0);
  *ic = af * i_f - i_r;
  *ie = -i_f + ar * i_r;
  *ib = (1.0 - af) * i_f + (1.0 - ar) * i_r;
}

/* Level-1 NMOS Id; PMOS via polarity flip (matches interpreter). */
static double mos_id(double vgs, double vds, double vth, double k,
                     double lambda) {
  double vov = vgs - vth;
  if (!(vov > 0.0))
    return 0.0;
  if (vds < 0.0)
    return -mos_id(vgs, -vds, vth, k, lambda);
  if (vds < vov)
    return k * (vov * vds - 0.5 * vds * vds);
  return 0.5 * k * vov * vov * (1.0 + lambda * vds);
}

static void add_violation(cJSON *arr, int *count, const char *who,
                          const char *rule, double actual, double limit) {
  cJSON *v = cJSON_CreateObject();
  cJSON_AddStringToObject(v, "component", who);
  cJSON_AddStringToObject(v, "rule", rule);
  cJSON_AddNumberToObject(v, "actual", actual);
  cJSON_AddNumberToObject(v, "limit", limit);
  cJSON_AddItemToArray(arr, v);
  (*count)++;
}

int verify_bound_schematic(const CompiledSchematic *schematic,
                           const char *report_path, VerifyResult *out) {
  CompilerPhysDesign phys;
  Solve s;
  cJSON *root;
  cJSON *viol;
  cJSON *nodes_js;
  cJSON *rails_js;
  NodeId sense = PHYSICS2_NODE_NONE;
  const char *measure_source = "none";
  int i;
  size_t k;
  int nonlinear = 0, reactive = 0, rail_sources = 0;
  double p_src = 0.0, p_abs = 0.0;
  int balance_ok = 1;

  if (!schematic || !report_path || !out)
    return 1;
  memset(out, 0, sizeof(*out));

  if (!compiler_schematic_to_phys_design(schematic, &phys))
    return fail(report_path, out, "phys_design_failed", "phys_design: %s",
                diag_last_error()[0] ? diag_last_error() : "lower failed");

  /* Integrity: every bound component is element i; sources come after. */
  for (k = 0; k < phys.count; k++) {
    CompilerPhysKind kd = phys.elements[k].kind;
    if (kd == COMPILER_PHYS_VSOURCE)
      rail_sources++;
    else if (kd == COMPILER_PHYS_CAPACITOR)
      out->physics2_caps++, reactive = 1;
    else if (kd == COMPILER_PHYS_INDUCTOR)
      out->physics2_inds++, reactive = 1;
    if (kd == COMPILER_PHYS_DIODE || kd == COMPILER_PHYS_BJT ||
        kd == COMPILER_PHYS_NMOS || kd == COMPILER_PHYS_PMOS ||
        kd == COMPILER_PHYS_LDO ||
        (kd == COMPILER_PHYS_OPAMP && phys.elements[k].terminal_count == 5))
      nonlinear = 1;
  }
  if ((int)phys.count - rail_sources != schematic->component_count) {
    compiler_physics_design_free(&phys);
    return fail(report_path, out, "physics_component_loss",
                "PHYSICS_COMPONENT_LOSS: bound=%d physical=%d",
                schematic->component_count, (int)phys.count - rail_sources);
  }

  if (solve_dc(&phys, &s) != 0) {
    solve_free(&s);
    compiler_physics_design_free(&phys);
    return fail(report_path, out, "dc_solve_failed", "DC solve failed: %s",
                diag_last_error());
  }
  out->physics2_instr = (int)s.prog.program.instruction_count;

  /* Measurement: requested by the spec, else the named-node heuristic. */
  if (schematic->measure_node[0]) {
    sense = compiler_physics_find_node(&s.prog, schematic->measure_node);
    if (sense == PHYSICS2_NODE_NONE) {
      solve_free(&s);
      compiler_physics_design_free(&phys);
      return fail(report_path, out, "measure_node_unknown",
                  "MEASURE_NODE_UNKNOWN: '%s' is not a solved node",
                  schematic->measure_node);
    }
    measure_source = "spec";
  } else {
    for (k = 0; k < s.prog.node_count && sense == PHYSICS2_NODE_NONE; k++)
      if (is_sense(s.prog.nodes[k].name))
        sense = s.prog.nodes[k].id;
    if (sense != PHYSICS2_NODE_NONE)
      measure_source = "heuristic";
  }
  for (k = 0; k < s.prog.node_count; k++) {
    if (s.prog.nodes[k].id == sense) {
      strncpy(out->measured_node, s.prog.nodes[k].name,
              sizeof(out->measured_node) - 1);
      out->measured_v = s.ctx.solution[sense];
    }
  }

  /* Ratings from solved stress. */
  viol = cJSON_CreateArray();
  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    const CompilerPhysElement *el = &phys.elements[i];
    const PhysicsInstruction *ins = &s.prog.program.instructions[i];
    double vmax = 0.0, cur = -1.0, pw = -1.0;
    int a, b;
    for (a = 0; a < c->pin_count; a++)
      for (b = a + 1; b < c->pin_count; b++) {
        double d = fabs(vnode(&s, c->nodes[a]) - vnode(&s, c->nodes[b]));
        if (d > vmax)
          vmax = d;
      }
    switch (el->kind) {
    case COMPILER_PHYS_RESISTOR:
      cur = vmax / el->value;
      pw = vmax * cur;
      break;
    case COMPILER_PHYS_DIODE: {
      double vd = vnode(&s, el->terminals[0]) - vnode(&s, el->terminals[1]);
      cur = diode_current(el, vd);
      pw = vd * cur;
      cur = fabs(cur);
      break;
    }
    case COMPILER_PHYS_INDUCTOR:
      cur = fabs(s.ctx.solution[ins->branch]);
      break;
    case COMPILER_PHYS_BATTERY: {
      double vb = vnode(&s, el->terminals[0]) - vnode(&s, el->terminals[1]);
      cur = (el->value - vb) / el->param[0];
      p_src += vb * cur;
      cur = fabs(cur);
      break;
    }
    case COMPILER_PHYS_SWITCH: {
      double r = el->param[1] >= 0.5 ? el->value : el->param[0];
      cur = vmax / r;
      pw = vmax * cur;
      break;
    }
    case COMPILER_PHYS_BJT: {
      double vc = vnode(&s, el->terminals[0]);
      double vb = vnode(&s, el->terminals[1]);
      double ve = vnode(&s, el->terminals[2]);
      double ic, ib, ie;
      bjt_currents(el, vc, vb, ve, &ic, &ib, &ie);
      cur = fabs(ic);
      if (fabs(ie) > cur)
        cur = fabs(ie);
      if (fabs(ib) > cur)
        cur = fabs(ib);
      pw = fabs(vc * ic + vb * ib + ve * ie);
      break;
    }
    case COMPILER_PHYS_NMOS:
    case COMPILER_PHYS_PMOS: {
      double vd = vnode(&s, el->terminals[0]);
      double vg = vnode(&s, el->terminals[1]);
      double vs = vnode(&s, el->terminals[2]);
      double id;
      if (el->kind == COMPILER_PHYS_PMOS)
        id = -mos_id((-vg) - (-vs), (-vd) - (-vs), el->value, el->param[0],
                     el->param[1]);
      else
        id = mos_id(vg - vs, vd - vs, el->value, el->param[0], el->param[1]);
      cur = fabs(id);
      pw = fabs(vd - vs) * cur;
      break;
    }
    case COMPILER_PHYS_LDO: {
      /* Same law as ldo_stamp; I_in = I_out (pass element). */
      double vin = vnode(&s, el->terminals[0]);
      double vo = vnode(&s, el->terminals[1]);
      double vg = vnode(&s, el->terminals[2]);
      double vset = (vin - vg) - el->param[0];
      double io;
      if (vset > el->value)
        vset = el->value;
      if (vset < 0.0)
        vset = 0.0;
      io = (vset - (vo - vg)) / el->param[1];
      if (el->param[2] > 0.0 && io > el->param[2])
        io = el->param[2];
      cur = fabs(io);
      pw = (vin - vo) * io;
      break;
    }
    case COMPILER_PHYS_OPAMP: {
      double i_out = s.ctx.solution[ins->branch];
      cur = fabs(i_out);
      if (el->terminal_count == 5) {
        double vout = vnode(&s, el->terminals[0]);
        double vee = vnode(&s, el->terminals[1]);
        double vcc = vnode(&s, el->terminals[4]);
        pw = i_out >= 0.0 ? (vcc - vout) * i_out : (vout - vee) * (-i_out);
        if (pw < 0.0)
          pw = 0.0;
      }
      /* Stamp still returns I through VEE; ratings use the rail split. */
      balance_ok = 0;
      break;
    }
    case COMPILER_PHYS_CAPACITOR:
      break; /* open at DC: no current, no power */
    default:
      balance_ok = 0;
      break;
    }
    if (pw > 0.0)
      p_abs += pw;
    if (c->part.v_rating > 0.0 && vmax > c->part.v_rating)
      add_violation(viol, &out->rating_violations, c->role, "voltage", vmax,
                    c->part.v_rating);
    if (cur >= 0.0 && c->part.i_rating > 0.0 && cur > c->part.i_rating)
      add_violation(viol, &out->rating_violations, c->role, "current", cur,
                    c->part.i_rating);
    if (pw >= 0.0 && c->part.power_rating_w > 0.0 &&
        pw > c->part.power_rating_w)
      add_violation(viol, &out->rating_violations, c->role, "power", pw,
                    c->part.power_rating_w);
  }
  /* Rail sources: power delivered = -V * I_branch (MNA branch sign). */
  for (k = (size_t)schematic->component_count; k < phys.count; k++) {
    const PhysicsInstruction *ins = &s.prog.program.instructions[k];
    p_src += -phys.elements[k].value * s.ctx.solution[ins->branch];
  }

  if (out->measured_node[0] && schematic->measure_has_min &&
      out->measured_v < schematic->measure_min)
    add_violation(viol, &out->rating_violations, out->measured_node,
                  "measure_min", out->measured_v, schematic->measure_min);
  if (out->measured_node[0] && schematic->measure_has_max &&
      out->measured_v > schematic->measure_max)
    add_violation(viol, &out->rating_violations, out->measured_node,
                  "measure_max", out->measured_v, schematic->measure_max);

  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "schema", "verification.v1");
  cJSON_AddStringToObject(root, "analysis", "dc_operating_point");

  /* Tolerance corners: every resistor at ±tol, exhaustive up to 2^10. */
  if (out->measured_node[0]) {
    int idx[MAX_CORNER_PARTS];
    int n = 0, too_many = 0;
    cJSON *cj = cJSON_CreateObject();
    for (i = 0; i < schematic->component_count; i++) {
      if (phys.elements[i].kind != COMPILER_PHYS_RESISTOR)
        continue;
      if (n == MAX_CORNER_PARTS)
        too_many = 1;
      else
        idx[n++] = i;
    }
    if (too_many) {
      cJSON_AddStringToObject(cj, "status", "skipped");
      cJSON_AddStringToObject(cj, "reason", "more than 10 resistors");
    } else if (n > 0) {
      double lo = INFINITY, hi = -INFINITY;
      unsigned mask, lo_mask = 0, hi_mask = 0;
      int solved = 1;
      for (mask = 0; mask < (1u << n) && solved; mask++) {
        CompilerPhysDesign cp = phys;
        CompilerPhysElement tmp[64];
        Solve cs;
        double v;
        if (phys.count > 64) {
          solved = 0;
          break;
        }
        memcpy(tmp, phys.elements, phys.count * sizeof(*tmp));
        cp.elements = tmp;
        for (i = 0; i < n; i++) {
          double t = Tolerance_ToPercentage(
                         schematic->components[idx[i]].part.tolerance_class) /
                     100.0;
          tmp[idx[i]].value *= (mask >> i) & 1u ? 1.0 + t : 1.0 - t;
        }
        solved = solve_dc(&cp, &cs) == 0;
        if (solved) {
          v = vnode(&cs, out->measured_node);
          if (v < lo)
            lo = v, lo_mask = mask;
          if (v > hi)
            hi = v, hi_mask = mask;
        }
        solve_free(&cs);
      }
      if (!solved) {
        cJSON_AddStringToObject(cj, "status", "corner_solve_failed");
        add_violation(viol, &out->rating_violations, out->measured_node,
                      "corner_solve", 0.0, 0.0);
      } else {
        cJSON *lo_parts = cJSON_CreateObject();
        cJSON *hi_parts = cJSON_CreateObject();
        for (i = 0; i < n; i++) {
          const CompiledComponent *c = &schematic->components[idx[i]];
          double t = Tolerance_ToPercentage(c->part.tolerance_class) / 100.0;
          cJSON_AddNumberToObject(lo_parts, c->role,
                                  phys.elements[idx[i]].value *
                                      ((lo_mask >> i) & 1u ? 1.0 + t : 1.0 - t));
          cJSON_AddNumberToObject(hi_parts, c->role,
                                  phys.elements[idx[i]].value *
                                      ((hi_mask >> i) & 1u ? 1.0 + t : 1.0 - t));
        }
        cJSON_AddStringToObject(cj, "status", "exhaustive");
        cJSON_AddNumberToObject(cj, "corners", (double)(1u << n));
        cJSON_AddNumberToObject(cj, "min_v", lo);
        cJSON_AddNumberToObject(cj, "max_v", hi);
        cJSON_AddItemToObject(cj, "min_at", lo_parts);
        cJSON_AddItemToObject(cj, "max_at", hi_parts);
        out->corner_min = lo;
        out->corner_max = hi;
        out->corner_count = (int)(1u << n);
        if (schematic->measure_has_min && lo < schematic->measure_min)
          add_violation(viol, &out->rating_violations, out->measured_node,
                        "corner_min", lo, schematic->measure_min);
        if (schematic->measure_has_max && hi > schematic->measure_max)
          add_violation(viol, &out->rating_violations, out->measured_node,
                        "corner_max", hi, schematic->measure_max);
      }
    }
    cJSON_AddItemToObject(root, "tolerance_corners", cj);
  }

  /* AC small-signal, one rail as input: H(jw) = v(measure) / v(rail), with
   * nonlinear devices linearized at the DC operating point. */
  if (out->measured_node[0] && (reactive || nonlinear) && rail_sources == 1) {
    cJSON *ac = cJSON_CreateArray();
    double vrail = phys.elements[phys.count - 1].value;
    double mag[AC_POINTS], ph[AC_POINTS];
    int d, got = ac_sweep(&s, out->measured_node, vrail, mag, ph);
    for (d = 0; d < got; d++) {
      cJSON *pt = cJSON_CreateObject();
      cJSON_AddNumberToObject(pt, "f_hz", ac_freq(d));
      cJSON_AddNumberToObject(pt, "mag", mag[d]);
      cJSON_AddNumberToObject(pt, "phase_deg", ph[d]);
      cJSON_AddItemToArray(ac, pt);
    }
    cJSON_AddItemToObject(root, "ac_transfer", ac);
    cJSON_AddStringToObject(root, "ac_status",
                            got == AC_POINTS ? "ok" : "ac_solve_failed");

    /* AC corners: every R, C, L at ±tol (exhaustive to 2^10); |H| band. */
    if (got == AC_POINTS) {
      int idx[MAX_CORNER_PARTS];
      int n = 0, too_many = 0;
      cJSON *acc_js = cJSON_CreateObject();
      for (i = 0; i < schematic->component_count; i++) {
        CompilerPhysKind kd = phys.elements[i].kind;
        if (kd != COMPILER_PHYS_RESISTOR && kd != COMPILER_PHYS_CAPACITOR &&
            kd != COMPILER_PHYS_INDUCTOR)
          continue;
        if (n == MAX_CORNER_PARTS)
          too_many = 1;
        else
          idx[n++] = i;
      }
      if (too_many || phys.count > 64) {
        cJSON_AddStringToObject(acc_js, "status", "skipped");
        cJSON_AddStringToObject(acc_js, "reason", "more than 10 R/C/L parts");
      } else if (n > 0) {
        double lo[AC_POINTS], hi[AC_POINTS];
        unsigned mask;
        int ok = 1;
        cJSON *band = cJSON_CreateArray();
        for (d = 0; d < AC_POINTS; d++)
          lo[d] = INFINITY, hi[d] = -INFINITY;
        for (mask = 0; mask < (1u << n) && ok; mask++) {
          CompilerPhysDesign cp = phys;
          CompilerPhysElement tmp[64];
          Solve cs;
          double cm[AC_POINTS], cph[AC_POINTS];
          memcpy(tmp, phys.elements, phys.count * sizeof(*tmp));
          cp.elements = tmp;
          for (i = 0; i < n; i++) {
            double t = Tolerance_ToPercentage(
                           schematic->components[idx[i]].part.tolerance_class) /
                       100.0;
            tmp[idx[i]].value *= (mask >> i) & 1u ? 1.0 + t : 1.0 - t;
          }
          ok = solve_dc(&cp, &cs) == 0 &&
               ac_sweep(&cs, out->measured_node, vrail, cm, cph) == AC_POINTS;
          solve_free(&cs);
          for (d = 0; ok && d < AC_POINTS; d++) {
            if (cm[d] < lo[d])
              lo[d] = cm[d];
            if (cm[d] > hi[d])
              hi[d] = cm[d];
          }
        }
        cJSON_AddStringToObject(acc_js, "status",
                                ok ? "exhaustive" : "corner_solve_failed");
        cJSON_AddNumberToObject(acc_js, "corners", (double)(1u << n));
        for (d = 0; ok && d < AC_POINTS; d++) {
          cJSON *pt = cJSON_CreateObject();
          cJSON_AddNumberToObject(pt, "f_hz", ac_freq(d));
          cJSON_AddNumberToObject(pt, "mag_min", lo[d]);
          cJSON_AddNumberToObject(pt, "mag_max", hi[d]);
          cJSON_AddItemToArray(band, pt);
        }
        cJSON_AddItemToObject(acc_js, "band", band);
      }
      cJSON_AddItemToObject(root, "ac_corners", acc_js);
    }
  }

  /* Transient: power-on step (rails 0 → V at t=0, all C/L state zero),
   * Backward Euler, measured node sampled at 10 points. */
  if (out->measured_node[0] && schematic->tran_step_s > 0.0) {
    cJSON *tr = cJSON_CreateObject();
    cJSON *pts = cJSON_CreateArray();
    PhysicsAccumulator *ta = physics2_accumulator_create(
        s.prog.program.next_node + s.prog.program.branch_count);
    PhysicsExecutionContext tc;
    double dt = schematic->tran_step_s;
    size_t nsteps = (size_t)ceil(schematic->tran_stop_s / dt);
    size_t every = nsteps / 10 ? nsteps / 10 : 1;
    size_t st;
    int ok;
    double v = 0.0;
    memset(&tc, 0, sizeof(tc));
    ok = ta && physics2_context_init(&tc, &s.prog.program, ta, dt);
    if (ok)
      tc.quiet = 1;
    for (st = 1; ok && st <= nsteps; st++) {
      ok = physics2_context_step(&tc, s.gnd);
      if (!ok)
        break;
      v = vnode_ctx(&tc, &s.prog, out->measured_node);
      if (st % every == 0 || st == nsteps) {
        cJSON *pt = cJSON_CreateObject();
        cJSON_AddNumberToObject(pt, "t_s", (double)st * dt);
        cJSON_AddNumberToObject(pt, "v", v);
        cJSON_AddItemToArray(pts, pt);
      }
    }
    cJSON_AddStringToObject(tr, "method", "backward_euler");
    cJSON_AddStringToObject(tr, "stimulus", "power_on_step");
    cJSON_AddNumberToObject(tr, "step_s", dt);
    cJSON_AddNumberToObject(tr, "stop_s", schematic->tran_stop_s);
    cJSON_AddItemToObject(tr, "points", pts);
    if (ok) {
      cJSON_AddNumberToObject(tr, "final_v", v);
      cJSON_AddNumberToObject(tr, "dc_v", out->measured_v);
      cJSON_AddStringToObject(tr, "status", "ok");
    } else {
      cJSON_AddStringToObject(tr, "status", "TRANSIENT_DIVERGED");
      cJSON_AddNumberToObject(tr, "failed_step", (double)st);
      add_violation(viol, &out->rating_violations, out->measured_node,
                    "transient_diverged", (double)st, (double)nsteps);
    }
    physics2_context_free(&tc);
    physics2_accumulator_free(ta);
    cJSON_AddItemToObject(root, "transient", tr);
  }

  {
    cJSON *mp = cJSON_CreateObject();
    for (i = 0; i < schematic->component_count; i++) {
      CompilerPhysKind kd = phys.elements[i].kind;
      if (kd == COMPILER_PHYS_RESISTOR || kd == COMPILER_PHYS_CAPACITOR ||
          kd == COMPILER_PHYS_INDUCTOR)
        continue;
      cJSON_AddStringToObject(mp, schematic->components[i].role,
                              phys.elements[i].model_from_part ? "part"
                                                               : "default");
    }
    cJSON_AddItemToObject(root, "model_params", mp);
  }

  out->passed = out->rating_violations == 0;
  snprintf(out->summary, sizeof(out->summary),
           "dc_sense=%s:%.6f phys2_instr=%d C=%d L=%d violations=%d",
           out->measured_node[0] ? out->measured_node : "(none)",
           out->measured_v, out->physics2_instr, out->physics2_caps,
           out->physics2_inds, out->rating_violations);

  cJSON_AddBoolToObject(root, "passed", out->passed);
  cJSON_AddNumberToObject(root, "rating_violations", out->rating_violations);
  cJSON_AddItemToObject(root, "violations", viol);
  cJSON_AddStringToObject(root, "measure_source", measure_source);
  if (out->measured_node[0]) {
    cJSON_AddStringToObject(root, "measured_node", out->measured_node);
    cJSON_AddNumberToObject(root, "measured_v", out->measured_v);
    cJSON_AddNumberToObject(root, "vout", out->measured_v);
  }
  {
    cJSON *nw = cJSON_CreateObject();
    cJSON_AddNumberToObject(nw, "iterations", (double)s.ctx.newton.iterations);
    cJSON_AddNumberToObject(nw, "residual_norm", s.ctx.newton.residual_norm);
    cJSON_AddNumberToObject(nw, "damping", s.ctx.newton.damping);
    cJSON_AddStringToObject(nw, "status", nonlinear ? "converged" : "linear");
    cJSON_AddItemToObject(root, "newton", nw);
  }
  if (balance_ok) {
    cJSON *pb = cJSON_CreateObject();
    cJSON_AddNumberToObject(pb, "sources_w", p_src);
    cJSON_AddNumberToObject(pb, "absorbed_w", p_abs);
    cJSON_AddNumberToObject(pb, "residual_w", p_src - p_abs);
    cJSON_AddItemToObject(root, "power_balance", pb);
  }
  cJSON_AddNumberToObject(root, "physics2_instruction_count",
                          out->physics2_instr);
  cJSON_AddNumberToObject(root, "physics2_capacitors", out->physics2_caps);
  cJSON_AddNumberToObject(root, "physics2_inductors", out->physics2_inds);
  nodes_js = cJSON_CreateObject();
  for (k = 0; k < s.prog.node_count; k++)
    cJSON_AddNumberToObject(nodes_js, s.prog.nodes[k].name,
                            s.ctx.solution[s.prog.nodes[k].id]);
  cJSON_AddItemToObject(root, "node_voltages", nodes_js);
  rails_js = cJSON_CreateArray();
  for (k = (size_t)schematic->component_count; k < phys.count; k++) {
    const char *net = phys.elements[k].terminals[0];
    double rv;
    RailSource src = compiler_schematic_rail(schematic, net, &rv);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "node", net);
    cJSON_AddNumberToObject(r, "voltage", rv);
    cJSON_AddStringToObject(r, "source",
                            src == RAIL_EXPLICIT ? "explicit" : "defaulted");
    cJSON_AddItemToArray(rails_js, r);
  }
  cJSON_AddItemToObject(root, "rails", rails_js);
  cJSON_AddStringToObject(root, "summary", out->summary);

  solve_free(&s);
  compiler_physics_design_free(&phys);
  if (!write_json(report_path, root))
    return 1;
  return out->passed ? 0 : 1;
}
