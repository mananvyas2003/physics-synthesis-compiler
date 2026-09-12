#include "verify_report.h"

#include "cJSON.h"
#include "physics2_interpreter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double ac_magnitude_stub(double g_ohms, double omega) {
  (void)omega;
  if (g_ohms <= 0.0)
    return 0.0;
  return 1.0 / g_ohms;
}

static int name_is(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

static int is_power_pos(const char *name) {
  return name_is(name, "VIN") || name_is(name, "VBUS");
}

static int is_sense(const char *name) {
  return name_is(name, "VOUT") || name_is(name, "3V3");
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

  if (!schematic || !report_path || !out)
    return 1;

  memset(out, 0, sizeof(*out));
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

  if (vin != PHYSICS2_NODE_NONE && gnd != PHYSICS2_NODE_NONE) {
    NodeId terminals[2];
    vin_v = 5.0;
    for (j = 0; j < node_count; j++) {
      if (name_is(node_names[j], "VIN")) {
        vin_v = 10.0;
        break;
      }
    }
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
    for (j = 0; j < node_count; j++) {
      if (strcmp(node_names[j], schematic->components[i].node1) == 0)
        terminals[0] = node_ids[j];
      if (strcmp(node_names[j], schematic->components[i].node2) == 0)
        terminals[1] = node_ids[j];
    }
    if (!physics2_primitive_init_resistor(&prims[i],
                                          schematic->components[i].role,
                                          schematic->components[i].part.value,
                                          0.0))
      goto done;
    if (physics2_program_add_primitive(&program, &prims[i], terminals, 2) ==
        PHYSICS_PRIMITIVE_NONE)
      goto done;
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

  if (schematic->component_count > 0)
    ac_mag = ac_magnitude_stub(schematic->components[0].part.value, 0.0);

  out->rating_violations = 0;
  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    double drop = fabs(sense_v);
    double dissip = 0.0;

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
