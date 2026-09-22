#include "dfm.h"

#include "component.h"
#include "design.h"
#include "vec.h"

#include <string.h>

// in dfm.c

DfmProfile profile_standard_default(void) {
  // Rigid 2-4 layer board, 1oz copper -- typical hobbyist/prosumer default
  return (DfmProfile){
      .layer_count = 2,
      .copper_weight_oz = 1.0,
      .min_trace_width_mm = 0.127, // 5 mil, JLCPCB 1-2 layer minimum
      .min_clearance_mm = 0.127,   // pad-to-pad, no hole, different nets
      /* via >= drill + 2*annular (0.3+2*0.13=0.56); use 0.6 JLCPCB-class */
      .min_via_diameter_mm = 0.6,
      .min_drill_mm = 0.3,
      .min_annular_ring_mm = 0.13,     // 1oz copper
      .board_edge_clearance_mm = 0.25, // outer layer copper-to-edge
      .max_component_height_mm = 1.6};
}

DfmProfile profile_wearable_default(void) {
  // Flex/tight-space board -- smartwatch/wearable class, real flex-PCB limits
  return (DfmProfile){
      .layer_count = 2,
      .copper_weight_oz = 0.5,
      .min_trace_width_mm = 0.09, // ~3.5 mil, tighter flex process
      .min_clearance_mm = 0.09,
      .min_via_diameter_mm = 0.35,
      .min_drill_mm = 0.15,
      .min_annular_ring_mm = 0.1, // flex minimum, 0.125 recommended
      .board_edge_clearance_mm = 0.15,
      .max_component_height_mm = 0.8 // tight enclosure constraint
  };
}

static int check_no_floating_pins(const Design *design,
                                  const DfmProfile *profile,
                                  DiagnosticList *out) {
  (void)profile;

  if (!design || !out) {
    return -1;
  }

  int errors = 0;

  for (size_t i = 0; i < vec_len(design->components); ++i) {
    const Component *component = &design->components[i];

    for (size_t p = 0; p < vec_len(component->pins); ++p) {
      const Pin *pin = &component->pins[p];

      if (pin->net_id == -1) {
        if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_COMPONENT,
                           (uint32_t)i, "floating_pin",
                           "%s pin %u (%s) is unconnected",
                           component->reference, pin->number,
                           pin->name ? pin->name : "unnamed") != 0) {
          return -1;
        }
        errors++;
      }
    }
  }

  return errors;
}

static int check_missing_footprints(const Design *design,
                                    const DfmProfile *profile,
                                    DiagnosticList *out) {
  (void)profile;

  if (!design || !out) {
    return -1;
  }

  int errors = 0;

  for (size_t i = 0; i < vec_len(design->components); ++i) {
    const Component *component = &design->components[i];

    if (!component->footprint || component->footprint[0] == '\0') {

      if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_COMPONENT,
                         (uint32_t)i, "missing_footprint",
                         "%s has no PCB footprint",
                         component->reference) != 0) {
        return -1;
      }

      errors++;
    }
  }

  return errors;
}

static int check_invalid_dimensions(const Design *design,
                                    const DfmProfile *profile,
                                    DiagnosticList *out) {
  if (!design || !profile || !out) {
    return -1;
  }

  if (profile->max_component_height_mm <= 0.0) {
    return 0;
  }

  int errors = 0;

  for (size_t i = 0; i < vec_len(design->components); ++i) {
    const Component *component = &design->components[i];

    if (component->dimensions.height_mm > profile->max_component_height_mm) {

      if (diagnostic_add(
              out, DIAGNOSTIC_ERROR, DIAG_TARGET_COMPONENT, (uint32_t)i,
              "component_height",
              "%s height %.3f mm exceeds manufacturing limit %.3f mm",
              component->reference, component->dimensions.height_mm,
              profile->max_component_height_mm) != 0) {
        return -1;
      }

      errors++;
    }
  }

  return errors;
}

/* Typical SMD body short-side (mm); used vs profile clearance/trace. */
static double package_body_short_mm(const char *package) {
  if (!package || !package[0])
    return 0.0;
  if (strcmp(package, "0402") == 0)
    return 0.5;
  if (strcmp(package, "0603") == 0)
    return 0.8;
  if (strcmp(package, "0805") == 0)
    return 1.25;
  if (strcmp(package, "1206") == 0)
    return 1.6;
  return 0.0;
}

static double package_typical_pad_mm(const char *package) {
  if (!package || !package[0])
    return 0.0;
  if (strcmp(package, "0402") == 0)
    return 0.5;
  if (strcmp(package, "0603") == 0)
    return 0.8;
  if (strcmp(package, "0805") == 0)
    return 1.0;
  if (strcmp(package, "1206") == 0)
    return 1.2;
  return 0.0;
}

/* Profile numerical fields must be positive and geometrically consistent. */
static int check_profile_consistency(const Design *design,
                                     const DfmProfile *profile,
                                     DiagnosticList *out) {
  int errors = 0;
  double via_need;

  (void)design;
  if (!profile || !out)
    return -1;

  if (profile->layer_count < 1 || profile->layer_count > 16) {
    if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_DESIGN,
                       DIAGNOSTIC_INVALID_ID, "profile_layer_count",
                       "layer_count %u out of range [1,16]",
                       (unsigned)profile->layer_count) != 0)
      return -1;
    errors++;
  }
  if (profile->min_trace_width_mm <= 0.0) {
    if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_DESIGN,
                       DIAGNOSTIC_INVALID_ID, "profile_trace_width",
                       "min_trace_width_mm must be > 0 (got %.4f)",
                       profile->min_trace_width_mm) != 0)
      return -1;
    errors++;
  }
  if (profile->min_clearance_mm <= 0.0) {
    if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_DESIGN,
                       DIAGNOSTIC_INVALID_ID, "profile_clearance",
                       "min_clearance_mm must be > 0 (got %.4f)",
                       profile->min_clearance_mm) != 0)
      return -1;
    errors++;
  }
  if (profile->min_drill_mm <= 0.0 || profile->min_via_diameter_mm <= 0.0) {
    if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_DESIGN,
                       DIAGNOSTIC_INVALID_ID, "profile_via_drill",
                       "min_via/drill must be > 0 (via=%.4f drill=%.4f)",
                       profile->min_via_diameter_mm, profile->min_drill_mm) != 0)
      return -1;
    errors++;
  }
  via_need = profile->min_drill_mm + 2.0 * profile->min_annular_ring_mm;
  if (profile->min_annular_ring_mm > 0.0 &&
      profile->min_via_diameter_mm + 1e-9 < via_need) {
    if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_DESIGN,
                       DIAGNOSTIC_INVALID_ID, "profile_annular_ring",
                       "min_via_diameter_mm %.4f < drill+2*annular %.4f",
                       profile->min_via_diameter_mm, via_need) != 0)
      return -1;
    errors++;
  }
  if (profile->board_edge_clearance_mm < 0.0) {
    if (diagnostic_add(out, DIAGNOSTIC_ERROR, DIAG_TARGET_DESIGN,
                       DIAGNOSTIC_INVALID_ID, "profile_board_edge",
                       "board_edge_clearance_mm must be >= 0 (got %.4f)",
                       profile->board_edge_clearance_mm) != 0)
      return -1;
    errors++;
  }

  return errors;
}

/*
 * Without PCB layout, compare known package body/pad sizes to profile
 * clearance and trace limits (stubs that actually consume profile numbers).
 */
static int check_package_vs_profile(const Design *design,
                                    const DfmProfile *profile,
                                    DiagnosticList *out) {
  int errors = 0;

  if (!design || !profile || !out)
    return -1;

  for (size_t i = 0; i < vec_len(design->components); ++i) {
    const Component *component = &design->components[i];
    double body = package_body_short_mm(component->package);
    double pad = package_typical_pad_mm(component->package);

    if (body > 0.0 && profile->min_clearance_mm > body) {
      if (diagnostic_add(
              out, DIAGNOSTIC_ERROR, DIAG_TARGET_COMPONENT, (uint32_t)i,
              "clearance_vs_package",
              "%s package %s body %.3f mm < min_clearance_mm %.3f",
              component->reference,
              component->package ? component->package : "?", body,
              profile->min_clearance_mm) != 0)
        return -1;
      errors++;
    }
    if (pad > 0.0 && profile->min_trace_width_mm > pad) {
      if (diagnostic_add(
              out, DIAGNOSTIC_ERROR, DIAG_TARGET_COMPONENT, (uint32_t)i,
              "trace_vs_package",
              "%s package %s pad ~%.3f mm < min_trace_width_mm %.3f",
              component->reference,
              component->package ? component->package : "?", pad,
              profile->min_trace_width_mm) != 0)
        return -1;
      errors++;
    }
  }

  return errors;
}

static const DfmRule BUILTIN_RULES[] = {
    {"floating_pin", check_no_floating_pins},
    {"missing_footprint", check_missing_footprints},
    {"component_height", check_invalid_dimensions},
    {"profile_consistency", check_profile_consistency},
    {"package_vs_profile", check_package_vs_profile}};

int dfm_registry_init(DfmRegistry *registry) {
  if (!registry) {
    return -1;
  }

  memset(registry, 0, sizeof(*registry));
  return 0;
}

void dfm_registry_free(DfmRegistry *registry) {
  if (!registry) {
    return;
  }

  vec_free(registry->rules);
}

int dfm_register_rule(DfmRegistry *registry, const DfmRule *rule) {
  if (!registry || !rule || !rule->name || !rule->check) {
    return -1;
  }

  return vec_push(registry->rules, *rule);
}

int dfm_run_all(const DfmRegistry *registry, const Design *design,
                const DfmProfile *profile, DiagnosticList *out) {
  if (!registry || !design || !profile || !out) {
    return -1;
  }

  int total_errors = 0;

  for (size_t i = 0; i < vec_len(registry->rules); ++i) {
    const DfmRule *rule = &registry->rules[i];

    int result = rule->check(design, profile, out);

    if (result < 0) {
      return -1;
    }

    total_errors += result;
  }

  return total_errors;
}

int dfm_register_builtin_rules(DfmRegistry *registry) {
  if (!registry) {
    return -1;
  }

  for (size_t i = 0; i < sizeof(BUILTIN_RULES) / sizeof(BUILTIN_RULES[0]);
       ++i) {
    if (dfm_register_rule(registry, &BUILTIN_RULES[i]) != 0) {
      return -1;
    }
  }

  return 0;
}
