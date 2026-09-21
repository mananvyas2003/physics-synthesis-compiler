#include "vendor_bridge.h"

#include "cJSON.h"

#include "component.h"
#include "design.h"
#include "dfm.h"
#include "diagnostic.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DfmProfile mfg_to_vendor_profile(const MfgDfmProfile *p) {
  DfmProfile out;
  memset(&out, 0, sizeof(out));
  if (!p)
    return profile_standard_default();
  out.layer_count = (uint8_t)(p->layer_count > 0 ? p->layer_count : 2);
  out.copper_weight_oz = p->copper_weight_oz;
  out.min_trace_width_mm = p->min_trace_width_mm;
  out.min_clearance_mm = p->min_clearance_mm;
  out.min_via_diameter_mm = p->min_via_diameter_mm;
  out.min_drill_mm = p->min_drill_mm;
  out.min_annular_ring_mm = p->min_annular_ring_mm;
  out.board_edge_clearance_mm = p->board_edge_clearance_mm;
  out.max_component_height_mm = p->max_component_height_mm;
  return out;
}

static double package_height_mm(const char *package) {
  if (!package || !package[0])
    return 0.0;
  if (strcmp(package, "0402") == 0)
    return 0.35;
  if (strcmp(package, "0603") == 0)
    return 0.45;
  if (strcmp(package, "0805") == 0)
    return 0.55;
  if (strcmp(package, "1206") == 0)
    return 0.65;
  return 0.5;
}

static int footprint_for_package(const char *package, char *out, size_t n) {
  if (!package || !package[0]) {
    out[0] = '\0';
    return 1;
  }
  snprintf(out, n, "Resistor_SMD:R_%s", package);
  return 0;
}

int vendor_dfm_check_schematic(const CompiledSchematic *schematic,
                               const MfgDfmProfile *profile,
                               const char *report_path, MfgDfmResult *out) {
  Design design;
  DfmRegistry registry;
  DiagnosticList diagnostics;
  DfmProfile vprof;
  cJSON *root = NULL;
  cJSON *errors = NULL;
  char *printed = NULL;
  FILE *fp = NULL;
  int i;
  int nerr = 0;
  int rc = 1;
  uint32_t *comp_ids = NULL;
  char **net_names = NULL;
  uint32_t *net_ids = NULL;
  int net_count = 0;

  if (!schematic || !profile || !report_path || !out)
    return 1;

  memset(out, 0, sizeof(*out));
  if (design_init(&design) != 0)
    return 1;
  if (dfm_registry_init(&registry) != 0) {
    design_free(&design);
    return 1;
  }
  if (dfm_register_builtin_rules(&registry) != 0) {
    dfm_registry_free(&registry);
    design_free(&design);
    return 1;
  }
  diagnostic_list_init(&diagnostics);
  vprof = mfg_to_vendor_profile(profile);

  comp_ids = calloc((size_t)schematic->component_count, sizeof(*comp_ids));
  net_names = calloc(64, sizeof(*net_names));
  net_ids = calloc(64, sizeof(*net_ids));
  if (!comp_ids || !net_names || !net_ids)
    goto done;

  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *cc = &schematic->components[i];
    Component c;
    char fp_buf[96];
    char ref[16];
    uint32_t cid = 0;

    component_init(&c);
    snprintf(ref, sizeof(ref), "%.15s", cc->role[0] ? cc->role : "U?");
    strncpy(c.reference, ref, sizeof(c.reference) - 1);
    strncpy(c.manufacturer_part_number, cc->part.mpn,
            sizeof(c.manufacturer_part_number) - 1);
    c.package = cc->part.package[0] ? cc->part.package : NULL;
    if (footprint_for_package(cc->part.package, fp_buf, sizeof(fp_buf)) == 0)
      c.footprint = fp_buf;
    else
      c.footprint = NULL;
    c.symbol = "Device:R";
    c.value = cc->part.value;
    c.model.kind = COMPONENT_RESISTOR;
    if (cc->part.type == PART_CAPACITOR) {
      c.symbol = "Device:C";
      c.model.kind = COMPONENT_CAPACITOR;
      component_set_value(&c, cc->part.value > 0 ? cc->part.value : 100e-9, 1.0);
      c.model.data.capacitor.capacitance_f = c.value_range;
      c.model.data.capacitor.voltage_rating_v =
          cc->part.v_rating > 0 ? cc->part.v_rating : 50.0;
    } else if (cc->part.type == PART_DIODE) {
      c.symbol = "Device:LED";
      c.model.kind = COMPONENT_LED;
      component_set_value(&c, cc->part.value > 0 ? cc->part.value : 2.0, 1.0);
      c.model.data.diode.forward_voltage_v = c.value_range;
      c.model.data.diode.reverse_voltage_v =
          cc->part.v_rating > 0 ? cc->part.v_rating : 5.0;
    } else {
      component_set_value(&c, cc->part.value > 0 ? cc->part.value : 1000.0, 1.0);
      c.model.data.resistor.resistance_ohm = c.value_range;
    }
    c.electrical.max_voltage = cc->part.v_rating;
    c.electrical.max_power = cc->part.power_rating_w;
    c.dimensions.height_mm = package_height_mm(cc->part.package);
    if (component_add_pin(&c, 1, "1") != 0 ||
        component_add_pin(&c, 2, "2") != 0) {
      component_free(&c);
      goto done;
    }
    if (cc->pin_count >= 3 && component_add_pin(&c, 3, cc->pin3[0] ? cc->pin3 : "3") != 0) {
      component_free(&c);
      goto done;
    }
    if (design_add_component(&design, &c, &cid) != 0) {
      component_free(&c);
      goto done;
    }
    component_free(&c);
    comp_ids[i] = cid;

    {
      int pc = cc->pin_count > 0 ? cc->pin_count : 2;
      int k;
      for (k = 0; k < pc && k < 8; k++) {
        const char *nn = cc->nodes[k][0] ? cc->nodes[k]
                         : (k == 0       ? cc->node1
                            : k == 1     ? cc->node2
                            : k == 2     ? cc->node3
                                          : "");
        uint16_t pin = (uint16_t)(k + 1);
        int found = -1;
        int j;
        uint32_t nid = 0;
        if (!nn || !nn[0])
          continue;
        for (j = 0; j < net_count; j++) {
          if (strcmp(net_names[j], nn) == 0) {
            found = j;
            break;
          }
        }
        if (found < 0) {
          if (net_count >= 64)
            continue;
          if (design_add_net(&design, nn, &nid) != 0)
            goto done;
          net_names[net_count] = (char *)nn;
          net_ids[net_count] = nid;
          found = net_count++;
        }
        if (design_connect_pin(&design, cid, pin, net_ids[found]) != 0)
          goto done;
      }
    }
  }

  nerr = dfm_run_all(&registry, &design, &vprof, &diagnostics);
  if (nerr < 0)
    goto done;

  root = cJSON_CreateObject();
  errors = cJSON_CreateArray();
  if (!root || !errors)
    goto done;
  cJSON_AddStringToObject(root, "schema", "mfg-dfm.v1");
  cJSON_AddStringToObject(root, "engine", "electronics_vendor_v2_next");
  cJSON_AddStringToObject(root, "profile", profile->name);
  cJSON_AddNumberToObject(root, "layer_count", vprof.layer_count);
  cJSON_AddNumberToObject(root, "max_component_height_mm",
                          vprof.max_component_height_mm);
  cJSON_AddItemToObject(root, "errors", errors);

  for (i = 0; i < (int)vec_len(diagnostics.items); i++) {
    const Diagnostic *d = &diagnostics.items[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "rule", d->rule_name ? d->rule_name : "unknown");
    cJSON_AddStringToObject(e, "message", d->message);
    cJSON_AddNumberToObject(e, "severity", (double)d->severity);
    cJSON_AddItemToArray(errors, e);
  }

  out->error_count = nerr;
  out->passed = (nerr == 0) ? 1 : 0;
  if (out->passed)
    snprintf(out->summary, sizeof(out->summary),
             "vendor dfm ok profile=%s", profile->name);
  else
    snprintf(out->summary, sizeof(out->summary),
             "vendor dfm failed profile=%s errors=%d", profile->name, nerr);

  cJSON_AddBoolToObject(root, "passed", out->passed);
  cJSON_AddNumberToObject(root, "error_count", nerr);
  cJSON_AddStringToObject(root, "summary", out->summary);

  printed = cJSON_Print(root);
  if (!printed)
    goto done;
  fp = fopen(report_path, "wb");
  if (!fp)
    goto done;
  fputs(printed, fp);
  fputc('\n', fp);
  fclose(fp);
  fp = NULL;
  rc = out->passed ? 0 : 2;

done:
  if (fp)
    fclose(fp);
  free(printed);
  cJSON_Delete(root);
  free(comp_ids);
  free(net_names);
  free(net_ids);
  diagnostic_list_free(&diagnostics);
  dfm_registry_free(&registry);
  design_free(&design);
  return rc;
}
