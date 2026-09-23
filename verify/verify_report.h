#ifndef VERIFY_REPORT_H
#define VERIFY_REPORT_H

#include "compiler.h"
#include "db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int passed;
  int rating_violations;
  char summary[256];
  /* Named-node measurement (never silently substituted with VIN). */
  double measured_v;
  char measured_node[64];
  int physics2_caps;
  int physics2_inds;
  int physics2_instr;
  /* Resistor tolerance corners on the measured node (count 0 → not run). */
  double corner_min, corner_max;
  int corner_count;
} VerifyResult;

/*
 * Physics2 DC operating point on the bound schematic, solved-stress ratings,
 * spec measurement + limits, tolerance corners, AC for linear networks.
 * Emits verification.v1.json. Returns 0 only when everything passes.
 */
int verify_bound_schematic(const CompiledSchematic *schematic,
                           const char *report_path, VerifyResult *out);

#ifdef __cplusplus
}
#endif

#endif
