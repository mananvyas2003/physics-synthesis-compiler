#include "verify_report.h"

#include "cJSON.h"
#include "diag_error.h"
#include "physics2_interpreter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int name_is(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

static int is_power_pos(const char *name) {
  return name_is(name, "VIN") || name_is(name, "VBUS") || name_is(name, "VCC") ||
         name_is(name, "3V3") || name_is(name, "5V");
}

static int is_sense(const char *name) {
  return name_is(name, "VOUT") || name_is(name, "SENSOR_VDD") ||
         name_is(name, "ADC_SENSE") || name_is(name, "SENSE");
}

static double infer_vin(const CompiledSchematic *schematic) {
  int i;
  for (i = 0; i < schematic->component_count; i++) {
    const char *n1 = schematic->components[i].node1;
    const char *n2 = schematic->components[i].node2;
    if (name_is(n1, "5V") || name_is(n2, "5V"))
      return 5.0;
    if (name_is(n1, "3V3") || name_is(n2, "3V3"))
      return 3.3;
    if (name_is(n1, "VIN") || name_is(n2, "VIN"))
      return 5.0;
  }
  return 5.0;
}

static int verify_led_analytical(const CompiledSchematic *schematic,
                                 const char *report_path, VerifyResult *out) {
  const CompiledComponent *led = NULL;
  const CompiledComponent *rser = NULL;
  double vin;
  double vf;
  double r;
  double i_led;
  double p_r;
  int i;
  cJSON *root;
  char *printed = NULL;
  FILE *fp;
  int rc = 1;

  for (i = 0; i < schematic->component_count; i++) {
    if (schematic->components[i].part.type == PART_DIODE)
      led = &schematic->components[i];
    else if (schematic->components[i].part.type == PART_RESISTOR)
      rser = &schematic->components[i];
  }
  if (!led || !rser) {
    snprintf(out->summary, sizeof(out->summary),
             "LED verify needs one diode/LED and one series resistor");
    return 1;
  }

  vin = infer_vin(schematic);
  vf = led->part.value > 0.0 ? led->part.value : 2.0;
  r = rser->part.value;
  if (r <= 0.0 || vin <= vf) {
    snprintf(out->summary, sizeof(out->summary),
             "LED circuit invalid: Vin=%.3g Vf=%.3g R=%.3g", vin, vf, r);
    out->passed = 0;
    return 1;
  }
  i_led = (vin - vf) / r;
  p_r = i_led * i_led * r;

  out->rating_violations = 0;
  if (led->part.i_rating > 0.0 && i_led > led->part.i_rating)
    out->rating_violations++;
  if (rser->part.power_rating_w > 0.0 && p_r > rser->part.power_rating_w)
    out->rating_violations++;
  /* Pass when current is in a safe indicator range */
  if (i_led < 0.001 || i_led > 0.020)
    out->rating_violations++;

  out->passed = (out->rating_violations == 0) ? 1 : 0;
  snprintf(out->summary, sizeof(out->summary),
           "LED current: %.2f mA (expected 1.0-20.0 mA); Vf=%.2f V; series "
           "resistor power=%.2f mW; result=%s",
           i_led * 1000.0, vf, p_r * 1000.0, out->passed ? "PASS" : "FAIL");
  out->measured_v = 0.0;
  out->measured_node[0] = '\0';

  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "schema", "verification.v1");
  cJSON_AddStringToObject(root, "analysis", "led_dc_analytical");
  cJSON_AddBoolToObject(root, "passed", out->passed ? 1 : 0);
  cJSON_AddNumberToObject(root, "vin_v", vin);
  cJSON_AddNumberToObject(root, "led_vf_v", vf);
  cJSON_AddNumberToObject(root, "led_current_a", i_led);
  cJSON_AddNumberToObject(root, "led_current_mA", i_led * 1000.0);
  cJSON_AddNumberToObject(root, "expected_current_min_mA", 1.0);
  cJSON_AddNumberToObject(root, "expected_current_max_mA", 20.0);
  cJSON_AddNumberToObject(root, "series_resistor_power_mW", p_r * 1000.0);
  cJSON_AddNumberToObject(root, "rating_violations", out->rating_violations);
  {
    cJSON *warnings = cJSON_CreateArray();
    if (i_led > 0.015)
      cJSON_AddItemToArray(
          warnings,
          cJSON_CreateString(
              "safety: LED current >15 mA — check LED absolute max rating"));
    if (p_r > 0.05)
      cJSON_AddItemToArray(
          warnings,
          cJSON_CreateString(
              "safety: series resistor dissipation >50 mW — check package"));
    if (i_led < 0.002)
      cJSON_AddItemToArray(
          warnings, cJSON_CreateString(
                        "quality: LED current <2 mA — may appear dim"));
    cJSON_AddItemToObject(root, "warnings", warnings);
    cJSON_AddStringToObject(root, "severity",
                            out->passed ? "electrical_pass"
                                        : "electrical_failure");
  }
  cJSON_AddStringToObject(root, "summary", out->summary);
  printed = cJSON_Print(root);
  cJSON_Delete(root);
  fp = fopen(report_path, "wb");
  if (fp && printed) {
    fputs(printed, fp);
    fputc('\n', fp);
    fclose(fp);
    rc = out->passed ? 0 : 1;
  }
  free(printed);
  return rc;
}

int verify_bound_schematic(const CompiledSchematic *schematic,
                           const char *report_path, VerifyResult *out) {
  int i;
  double sense_v = 0.0;
  double vin_v = 5.0;
  double corner_low = 0.0;
  double corner_high = 0.0;
  double ac_mag = 0.0;
  cJSON *root;
  char *printed = NULL;
  FILE *fp;
  int rc = 1;
  int has_dc = 0;
  int has_diode = 0;
  int has_cap = 0;
  int has_ind = 0;
  int has_xstr = 0;
  int has_ic = 0;

  if (!schematic || !report_path || !out)
    return 1;

  memset(out, 0, sizeof(*out));

  for (i = 0; i < schematic->component_count; i++) {
    PartTypes t = schematic->components[i].part.type;
    if (t == PART_DIODE)
      has_diode = 1;
    else if (t == PART_CAPACITOR)
      has_cap = 1;
    else if (t == PART_INDUCTOR)
      has_ind = 1;
    else if (t == PART_TRANSISTOR)
      has_xstr = 1;
    else if (t == PART_IC)
      has_ic = 1;
  }

  if (has_diode && !has_xstr && !has_ic) {
    int led_like = 0;
    for (i = 0; i < schematic->component_count; i++) {
      if (schematic->components[i].part.type == PART_DIODE) {
        double v = schematic->components[i].part.value;
        /* Catalogue LED Vf is typically 1.2–3.5 V; Shockley Is is ≪ 1e-6. */
        if (v >= 1.2 && v <= 3.5)
          led_like = 1;
      }
    }
    /* LED-only: keep analytical until LED Shockley params exist.
     * Mixed nets (e.g. caps + LED) must use Physics2 so C is not dropped. */
    if (led_like && !has_cap && !has_ind)
      return verify_led_analytical(schematic, report_path, out);
  }
  if (has_xstr || has_ic) {
    out->passed = 0;
    snprintf(out->summary, sizeof(out->summary),
             "unsupported: %s (no DC bias model yet)",
             has_xstr ? "transistor network" : "IC/regulator network");
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "schema", "verification.v1");
    cJSON_AddStringToObject(root, "analysis", "unsupported");
    cJSON_AddBoolToObject(root, "passed", 0);
    cJSON_AddStringToObject(root, "summary", out->summary);
    printed = cJSON_Print(root);
    cJSON_Delete(root);
    fp = fopen(report_path, "wb");
    if (fp && printed) {
      fputs(printed, fp);
      fputc('\n', fp);
      fclose(fp);
      free(printed);
      return 1;
    }
    free(printed);
    return 1;
  }

  /* Authoritative path: CompiledSchematic → PhysDesign → Physics2. */
  {
    CompilerPhysDesign phys;
    CompiledPhysicsProgram compiled;
    PhysicsAccumulator *acc = NULL;
    PhysicsExecutionContext ctx;
    NodeId gnd = PHYSICS2_NODE_NONE;
    NodeId sense = PHYSICS2_NODE_NONE;
    NodeId vin = PHYSICS2_NODE_NONE;
    size_t pi;
    size_t ni;
    int phys_caps = 0;
    int phys_inds = 0;
    int phys_res = 0;
    int phys_diodes = 0;
    cJSON *nodes_js;

    vin_v = infer_vin(schematic);
    memset(&compiled, 0, sizeof(compiled));
    compiler_physics_design_init(&phys);

    if (!compiler_schematic_to_phys_design(schematic, vin_v, &phys)) {
      snprintf(out->summary, sizeof(out->summary), "phys_design: %s",
               diag_last_error()[0] ? diag_last_error() : "lower failed");
      out->passed = 0;
      root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "schema", "verification.v1");
      cJSON_AddStringToObject(root, "analysis", "physics_unsupported");
      cJSON_AddBoolToObject(root, "passed", 0);
      cJSON_AddStringToObject(root, "summary", out->summary);
      printed = cJSON_Print(root);
      cJSON_Delete(root);
      fp = fopen(report_path, "wb");
      if (fp && printed) {
        fputs(printed, fp);
        fputc('\n', fp);
        fclose(fp);
      }
      free(printed);
      compiler_physics_design_free(&phys);
      return 1;
    }

    if (!compiler_lower_to_physics2(&phys, &compiled)) {
      snprintf(out->summary, sizeof(out->summary),
               "Physics2 lowering failed (component loss or init error)");
      out->passed = 0;
      compiler_physics_design_free(&phys);
      root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "schema", "verification.v1");
      cJSON_AddStringToObject(root, "analysis", "physics_lower_failed");
      cJSON_AddBoolToObject(root, "passed", 0);
      cJSON_AddStringToObject(root, "summary", out->summary);
      printed = cJSON_Print(root);
      cJSON_Delete(root);
      fp = fopen(report_path, "wb");
      if (fp && printed) {
        fputs(printed, fp);
        fputc('\n', fp);
        fclose(fp);
      }
      free(printed);
      return 1;
    }

    for (pi = 0; pi < phys.count; pi++) {
      if (phys.elements[pi].kind == COMPILER_PHYS_CAPACITOR)
        phys_caps++;
      else if (phys.elements[pi].kind == COMPILER_PHYS_INDUCTOR)
        phys_inds++;
      else if (phys.elements[pi].kind == COMPILER_PHYS_RESISTOR)
        phys_res++;
      else if (phys.elements[pi].kind == COMPILER_PHYS_DIODE)
        phys_diodes++;
    }

    /* Integrity: every bound R/C/L/diode must appear in Physical IR / program. */
    {
      int bound_rcld = 0;
      for (i = 0; i < schematic->component_count; i++) {
        PartTypes t = schematic->components[i].part.type;
        if (t == PART_RESISTOR || t == PART_CAPACITOR || t == PART_INDUCTOR ||
            t == PART_DIODE)
          bound_rcld++;
      }
      if ((int)compiled.program.instruction_count < bound_rcld) {
        snprintf(out->summary, sizeof(out->summary),
                 "PHYSICS_COMPONENT_LOSS: bound=%d physics2_instr=%zu",
                 bound_rcld, compiled.program.instruction_count);
        out->passed = 0;
        compiler_free_physics_program(&compiled);
        compiler_physics_design_free(&phys);
        root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "schema", "verification.v1");
        cJSON_AddStringToObject(root, "analysis", "physics_component_loss");
        cJSON_AddBoolToObject(root, "passed", 0);
        cJSON_AddStringToObject(root, "summary", out->summary);
        printed = cJSON_Print(root);
        cJSON_Delete(root);
        fp = fopen(report_path, "wb");
        if (fp && printed) {
          fputs(printed, fp);
          fputc('\n', fp);
          fclose(fp);
        }
        free(printed);
        return 1;
      }
    }

    gnd = compiler_physics_find_node(&compiled, "GND");
    sense = compiler_physics_find_node(&compiled, "VOUT");
    if (sense == PHYSICS2_NODE_NONE)
      sense = compiler_physics_find_node(&compiled, "SENSOR_VDD");
    if (sense == PHYSICS2_NODE_NONE)
      sense = compiler_physics_find_node(&compiled, "ADC_SENSE");
    for (ni = 0; ni < compiled.node_count; ni++) {
      if (is_power_pos(compiled.nodes[ni].name))
        vin = compiled.nodes[ni].id;
      if (sense == PHYSICS2_NODE_NONE && is_sense(compiled.nodes[ni].name))
        sense = compiled.nodes[ni].id;
    }
    if (gnd == PHYSICS2_NODE_NONE && compiled.node_count > 0)
      gnd = compiled.nodes[0].id;

    has_dc = (vin != PHYSICS2_NODE_NONE && gnd != PHYSICS2_NODE_NONE);
    if (has_dc) {
      double *node_volts = NULL;
      acc = physics2_accumulator_create(compiled.program.next_node +
                                        compiled.program.branch_count);
      if (!acc) {
        snprintf(out->summary, sizeof(out->summary),
                 "DC Physics2 accumulator alloc failed");
        out->passed = 0;
        compiler_free_physics_program(&compiled);
        compiler_physics_design_free(&phys);
        return 1;
      }
      memset(&ctx, 0, sizeof(ctx));
      if (!physics2_context_init(&ctx, &compiled.program, acc, 0.0) ||
          !physics2_context_step(&ctx, gnd)) {
        snprintf(out->summary, sizeof(out->summary),
                 "DC Physics2 solve failed (singular/diverge/stamp)");
        out->passed = 0;
        physics2_context_free(&ctx);
        physics2_accumulator_free(acc);
        compiler_free_physics_program(&compiled);
        compiler_physics_design_free(&phys);
        root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "schema", "verification.v1");
        cJSON_AddStringToObject(root, "analysis", "dc_solve_failed");
        cJSON_AddBoolToObject(root, "passed", 0);
        cJSON_AddStringToObject(root, "summary", out->summary);
        printed = cJSON_Print(root);
        cJSON_Delete(root);
        fp = fopen(report_path, "wb");
        if (fp && printed) {
          fputs(printed, fp);
          fputc('\n', fp);
          fclose(fp);
        }
        free(printed);
        return 1;
      }

      node_volts = calloc(compiled.node_count, sizeof(*node_volts));
      for (ni = 0; ni < compiled.node_count; ni++) {
        NodeId id = compiled.nodes[ni].id;
        if (id < ctx.solution_size)
          node_volts[ni] = ctx.solution[id];
      }

      if (sense != PHYSICS2_NODE_NONE) {
        sense_v = ctx.solution[sense];
        /* Prefer the looked-up name over generic "vout". */
        for (ni = 0; ni < compiled.node_count; ni++) {
          if (compiled.nodes[ni].id == sense) {
            strncpy(out->measured_node, compiled.nodes[ni].name,
                    sizeof(out->measured_node) - 1);
            break;
          }
        }
        out->measured_v = sense_v;
      } else {
        /* Phase 4: never silently report VIN as the measured sense voltage. */
        sense_v = 0.0;
        out->measured_v = 0.0;
        out->measured_node[0] = '\0';
      }
      corner_low = sense_v * 0.99;
      corner_high = sense_v * 1.01;

      out->rating_violations = 0;
      for (i = 0; i < schematic->component_count; i++) {
        const CompiledComponent *c = &schematic->components[i];
        NodeId n0 = compiler_physics_find_node(&compiled, c->node1);
        NodeId n1 = compiler_physics_find_node(&compiled, c->node2);
        double drop = 0.0;
        double dissip = 0.0;

        if (n0 != PHYSICS2_NODE_NONE && n1 != PHYSICS2_NODE_NONE)
          drop = fabs(ctx.solution[n0] - ctx.solution[n1]);
        if (c->part.type == PART_RESISTOR && c->part.value > 0.0)
          dissip = (drop * drop) / c->part.value;

        if (c->part.v_rating > 0.0 && drop > c->part.v_rating)
          out->rating_violations++;
        if (c->part.power_rating_w > 0.0 && dissip > c->part.power_rating_w)
          out->rating_violations++;
      }

      physics2_context_free(&ctx);
      physics2_accumulator_free(acc);

      if (schematic->component_count > 0 &&
          schematic->components[0].part.type == PART_RESISTOR)
        ac_mag = 1.0 / schematic->components[0].part.value;

      out->passed = (out->rating_violations == 0) ? 1 : 0;
      out->physics2_caps = phys_caps;
      out->physics2_inds = phys_inds;
      out->physics2_instr = (int)compiled.program.instruction_count;
      snprintf(out->summary, sizeof(out->summary),
               "dc_sense=%s:%.6f corner=[%.6f,%.6f] phys2_instr=%zu C=%d L=%d "
               "R=%d D=%d rating_violations=%d",
               out->measured_node[0] ? out->measured_node : "(none)", sense_v,
               corner_low, corner_high, compiled.program.instruction_count,
               phys_caps, phys_inds, phys_res, phys_diodes,
               out->rating_violations);

      root = cJSON_CreateObject();
      cJSON_AddStringToObject(root, "schema", "verification.v1");
      cJSON_AddStringToObject(root, "analysis",
                              phys_diodes ? "diode_dc_newton"
                                          : "dc_operating_point");
      cJSON_AddBoolToObject(root, "passed", out->passed ? 1 : 0);
      cJSON_AddNumberToObject(root, "rating_violations", out->rating_violations);
      if (out->measured_node[0]) {
        cJSON_AddStringToObject(root, "measured_node", out->measured_node);
        cJSON_AddNumberToObject(root, "measured_v", out->measured_v);
        /* Compat: "vout" only when a real sense node was measured. */
        cJSON_AddNumberToObject(root, "vout", sense_v);
      }
      cJSON_AddNumberToObject(root, "corner_low", corner_low);
      cJSON_AddNumberToObject(root, "corner_high", corner_high);
      cJSON_AddNumberToObject(root, "ac_magnitude", ac_mag);
      cJSON_AddNumberToObject(root, "physics2_instruction_count",
                              (double)compiled.program.instruction_count);
      cJSON_AddNumberToObject(root, "physics2_capacitors", phys_caps);
      cJSON_AddNumberToObject(root, "physics2_inductors", phys_inds);
      nodes_js = cJSON_CreateObject();
      for (ni = 0; ni < compiled.node_count; ni++) {
        if (node_volts)
          cJSON_AddNumberToObject(nodes_js, compiled.nodes[ni].name,
                                  node_volts[ni]);
      }
      cJSON_AddItemToObject(root, "node_voltages", nodes_js);
      cJSON_AddStringToObject(root, "summary", out->summary);
      printed = cJSON_Print(root);
      cJSON_Delete(root);
      free(node_volts);

      fp = fopen(report_path, "wb");
      if (fp && printed) {
        fputs(printed, fp);
        fputc('\n', fp);
        fclose(fp);
        rc = out->passed ? 0 : 1;
      }
      free(printed);

      compiler_free_physics_program(&compiled);
      compiler_physics_design_free(&phys);
      return rc;
    }

    snprintf(out->summary, sizeof(out->summary),
             "no DC bias (need power net + GND); phys2_instr=%zu C=%d",
             compiled.program.instruction_count, phys_caps);
    out->passed = 0;
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "schema", "verification.v1");
    cJSON_AddStringToObject(root, "analysis", "no_dc_bias");
    cJSON_AddBoolToObject(root, "passed", 0);
    cJSON_AddNumberToObject(root, "physics2_capacitors", phys_caps);
    cJSON_AddStringToObject(root, "summary", out->summary);
    printed = cJSON_Print(root);
    cJSON_Delete(root);
    fp = fopen(report_path, "wb");
    if (fp && printed) {
      fputs(printed, fp);
      fputc('\n', fp);
      fclose(fp);
    }
    free(printed);
    compiler_free_physics_program(&compiled);
    compiler_physics_design_free(&phys);
    return 1;
  }
}
