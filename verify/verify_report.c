#include "verify_report.h"

#include "cJSON.h"
#include "physics2_interpreter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int name_is(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

static int is_power_pos(const char *name) {
  return name_is(name, "VIN") || name_is(name, "VBUS") || name_is(name, "VCC") ||
         name_is(name, "3V3") || name_is(name, "5V");
}

static int is_sense(const char *name) {
  return name_is(name, "VOUT") || name_is(name, "3V3");
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

static int verify_rc_analytical(const CompiledSchematic *schematic,
                                const char *report_path, VerifyResult *out) {
  const CompiledComponent *r = NULL;
  const CompiledComponent *c = NULL;
  double fc;
  int i;
  cJSON *root;
  char *printed = NULL;
  FILE *fp;
  int rc = 1;

  for (i = 0; i < schematic->component_count; i++) {
    if (schematic->components[i].part.type == PART_RESISTOR)
      r = &schematic->components[i];
    else if (schematic->components[i].part.type == PART_CAPACITOR)
      c = &schematic->components[i];
  }
  if (!r || !c || r->part.value <= 0.0 || c->part.value <= 0.0) {
    snprintf(out->summary, sizeof(out->summary),
             "RC verify needs one resistor and one capacitor with positive "
             "values");
    return 1;
  }

  fc = 1.0 / (2.0 * M_PI * r->part.value * c->part.value);
  out->rating_violations = 0;
  if (c->part.v_rating > 0.0 && infer_vin(schematic) > c->part.v_rating)
    out->rating_violations++;
  out->passed = (out->rating_violations == 0 && isfinite(fc) && fc > 0.0) ? 1
                                                                         : 0;
  snprintf(out->summary, sizeof(out->summary),
           "RC low-pass fc=%.3g Hz (R=%.3g C=%.3g); result=%s", fc,
           r->part.value, c->part.value, out->passed ? "PASS" : "FAIL");

  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "schema", "verification.v1");
  cJSON_AddStringToObject(root, "analysis", "rc_cutoff_analytical");
  cJSON_AddBoolToObject(root, "passed", out->passed ? 1 : 0);
  cJSON_AddNumberToObject(root, "r_ohm", r->part.value);
  cJSON_AddNumberToObject(root, "c_farad", c->part.value);
  cJSON_AddNumberToObject(root, "cutoff_hz", fc);
  cJSON_AddNumberToObject(root, "rating_violations", out->rating_violations);
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
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  PhysicsPrimitive *prims = NULL;
  NodeId *node_ids = NULL;
  char **node_names = NULL;
  int node_count = 0;
  int i;
  int j;
  NodeId gnd = PHYSICS2_NODE_NONE;
  NodeId sense = PHYSICS2_NODE_NONE;
  NodeId vin = PHYSICS2_NODE_NONE;
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
  int has_res = 0;
  int has_ind = 0;
  int has_xstr = 0;
  int has_ic = 0;
  int stamp_count = 0;

  if (!schematic || !report_path || !out)
    return 1;

  memset(out, 0, sizeof(*out));

  for (i = 0; i < schematic->component_count; i++) {
    PartTypes t = schematic->components[i].part.type;
    if (t == PART_DIODE)
      has_diode = 1;
    else if (t == PART_CAPACITOR)
      has_cap = 1;
    else if (t == PART_RESISTOR)
      has_res = 1;
    else if (t == PART_INDUCTOR)
      has_ind = 1;
    else if (t == PART_TRANSISTOR)
      has_xstr = 1;
    else if (t == PART_IC)
      has_ic = 1;
  }

  if (has_diode && !has_xstr && !has_ic)
    return verify_led_analytical(schematic, report_path, out);
  if (has_cap && has_res && !has_xstr && !has_ic)
    return verify_rc_analytical(schematic, report_path, out);
  if (has_ind && has_res && !has_xstr) {
    const CompiledComponent *rr = NULL;
    const CompiledComponent *ll = NULL;
    double fc;
    cJSON *root2;
    char *printed2 = NULL;
    FILE *fp2;
    for (i = 0; i < schematic->component_count; i++) {
      if (schematic->components[i].part.type == PART_RESISTOR)
        rr = &schematic->components[i];
      else if (schematic->components[i].part.type == PART_INDUCTOR)
        ll = &schematic->components[i];
    }
    if (!rr || !ll || rr->part.value <= 0.0 || ll->part.value <= 0.0) {
      snprintf(out->summary, sizeof(out->summary), "RL verify missing R/L");
      return 1;
    }
    fc = rr->part.value / (2.0 * M_PI * ll->part.value);
    out->passed = isfinite(fc) && fc > 0.0 ? 1 : 0;
    snprintf(out->summary, sizeof(out->summary),
             "RL low-pass fc=%.3g Hz (R=%.3g L=%.3g); result=%s", fc,
             rr->part.value, ll->part.value, out->passed ? "PASS" : "FAIL");
    root2 = cJSON_CreateObject();
    cJSON_AddStringToObject(root2, "schema", "verification.v1");
    cJSON_AddStringToObject(root2, "analysis", "rl_cutoff_analytical");
    cJSON_AddBoolToObject(root2, "passed", out->passed ? 1 : 0);
    cJSON_AddNumberToObject(root2, "cutoff_hz", fc);
    cJSON_AddStringToObject(root2, "summary", out->summary);
    printed2 = cJSON_Print(root2);
    cJSON_Delete(root2);
    fp2 = fopen(report_path, "wb");
    if (fp2 && printed2) {
      fputs(printed2, fp2);
      fputc('\n', fp2);
      fclose(fp2);
      free(printed2);
      return out->passed ? 0 : 1;
    }
    free(printed2);
    return 1;
  }
  if (has_xstr || has_ic) {
    /* Phase 1: fail-closed — no fake "structural pass" without DC bias. */
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

  physics2_program_init(&program);

  prims = calloc((size_t)schematic->component_count + 1u, sizeof(*prims));
  node_names = calloc(64, sizeof(*node_names));
  node_ids = calloc(64, sizeof(*node_ids));
  if (!prims || !node_names || !node_ids)
    goto done;

  for (i = 0; i < schematic->component_count; i++) {
    const char *names[2] = {schematic->components[i].node1,
                            schematic->components[i].node2};
    int k;
    for (k = 0; k < 2; k++) {
      int found = 0;
      for (j = 0; j < node_count; j++) {
        if (strcmp(node_names[j], names[k]) == 0) {
          found = 1;
          break;
        }
      }
      if (!found && node_count < 64) {
        node_names[node_count] = (char *)names[k];
        node_ids[node_count] = physics2_program_new_node(&program);
        if (name_is(names[k], "GND"))
          gnd = node_ids[node_count];
        if (is_sense(names[k]))
          sense = node_ids[node_count];
        if (is_power_pos(names[k]))
          vin = node_ids[node_count];
        node_count++;
      }
    }
  }

  if (gnd == PHYSICS2_NODE_NONE && node_count > 0)
    gnd = node_ids[0];

  vin_v = infer_vin(schematic);
  if (vin != PHYSICS2_NODE_NONE && gnd != PHYSICS2_NODE_NONE) {
    NodeId terminals[2];
    if (!physics2_primitive_init_vsource(&prims[schematic->component_count],
                                         "VSRC", vin_v, 0.0))
      goto done;
    terminals[0] = vin;
    terminals[1] = gnd;
    if (physics2_program_add_primitive(&program,
                                       &prims[schematic->component_count],
                                       terminals, 2) == PHYSICS_PRIMITIVE_NONE)
      goto done;
    has_dc = 1;
  }

  for (i = 0; i < schematic->component_count; i++) {
    NodeId terminals[2] = {PHYSICS2_NODE_NONE, PHYSICS2_NODE_NONE};
    if (schematic->components[i].part.type != PART_RESISTOR)
      continue; /* DC: omit capacitors (open) */
    for (j = 0; j < node_count; j++) {
      if (strcmp(node_names[j], schematic->components[i].node1) == 0)
        terminals[0] = node_ids[j];
      if (strcmp(node_names[j], schematic->components[i].node2) == 0)
        terminals[1] = node_ids[j];
    }
    if (!physics2_primitive_init_resistor(&prims[stamp_count],
                                          schematic->components[i].role,
                                          schematic->components[i].part.value,
                                          0.0))
      goto done;
    if (physics2_program_add_primitive(&program, &prims[stamp_count], terminals,
                                       2) == PHYSICS_PRIMITIVE_NONE)
      goto done;
    stamp_count++;
  }

  if (has_dc) {
    acc = physics2_accumulator_create(program.next_node + program.branch_count);
    if (!acc)
      goto done;
    if (!physics2_context_init(&ctx, &program, acc, 1e-3))
      goto done;
    if (!physics2_context_step(&ctx, gnd)) {
      physics2_context_free(&ctx);
      goto done;
    }
    if (sense != PHYSICS2_NODE_NONE)
      sense_v = ctx.solution[sense];
    else if (vin != PHYSICS2_NODE_NONE)
      sense_v = ctx.solution[vin];
    corner_low = sense_v * 0.99;
    corner_high = sense_v * 1.01;
    physics2_context_free(&ctx);
  }

  if (schematic->component_count > 0 &&
      schematic->components[0].part.type == PART_RESISTOR)
    ac_mag = 1.0 / schematic->components[0].part.value;

  out->rating_violations = 0;
  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    double drop = fabs(sense_v);
    double dissip = 0.0;

    if (c->part.type != PART_RESISTOR)
      continue;

    if (has_dc) {
      if (is_power_pos(c->node1) && is_sense(c->node2))
        drop = fabs(vin_v - sense_v);
      else if (is_sense(c->node1) && name_is(c->node2, "GND"))
        drop = fabs(sense_v);
      else if (is_power_pos(c->node1) && name_is(c->node2, "GND"))
        drop = fabs(vin_v);
      if (c->part.value > 0.0)
        dissip = (drop * drop) / c->part.value;
    }

    if (c->part.v_rating > 0.0 && drop > c->part.v_rating)
      out->rating_violations++;
    if (c->part.power_rating_w > 0.0 && dissip > c->part.power_rating_w)
      out->rating_violations++;
  }

  out->passed = (out->rating_violations == 0) ? 1 : 0;
  snprintf(out->summary, sizeof(out->summary),
           "dc_sense=%.6f corner=[%.6f,%.6f] ac_mag=%.6g rating_violations=%d",
           sense_v, corner_low, corner_high, ac_mag, out->rating_violations);

  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "schema", "verification.v1");
  cJSON_AddStringToObject(root, "analysis", "dc_operating_point");
  cJSON_AddBoolToObject(root, "passed", out->passed ? 1 : 0);
  cJSON_AddNumberToObject(root, "rating_violations", out->rating_violations);
  cJSON_AddNumberToObject(root, "vout", sense_v);
  cJSON_AddNumberToObject(root, "corner_low", corner_low);
  cJSON_AddNumberToObject(root, "corner_high", corner_high);
  cJSON_AddNumberToObject(root, "ac_magnitude", ac_mag);
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

done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  free(prims);
  free(node_names);
  free(node_ids);
  return rc;
}
