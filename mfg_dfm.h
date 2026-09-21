#ifndef MFG_DFM_H
#define MFG_DFM_H

#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Manufacturing DFM profiles + schematic checks
 * (adapted from Abheesht04/electronics_vendor_v2_next src/dfm.c).
 * Physics stays in verify/; this only checks fab capability / structure.
 */

typedef struct {
  char name[64];
  int layer_count;
  double copper_weight_oz;
  double min_trace_width_mm;
  double min_clearance_mm;
  double min_via_diameter_mm;
  double min_drill_mm;
  double min_annular_ring_mm;
  double board_edge_clearance_mm;
  double max_component_height_mm;
} MfgDfmProfile;

typedef struct {
  int passed;
  int error_count;
  char summary[256];
} MfgDfmResult;

MfgDfmProfile mfg_dfm_profile_standard(void);
MfgDfmProfile mfg_dfm_profile_wearable(void);

/* Load profile JSON; on failure leave *out unchanged and return 1. */
int mfg_dfm_profile_load_json(const char *path, MfgDfmProfile *out);

/* Builtin rules: floating_pin, missing_footprint, component_height. */
int mfg_dfm_check_schematic(const CompiledSchematic *schematic,
                            const MfgDfmProfile *profile,
                            const char *report_path, MfgDfmResult *out);

#ifdef __cplusplus
}
#endif

#endif
