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
} VerifyResult;

/*
 * DC solve on a bound schematic (resistor divider path) and assert
 * voltages against part ratings. Emits verification.v1.json.
 */
int verify_bound_schematic(const CompiledSchematic *schematic,
                           const char *report_path, VerifyResult *out);

#ifdef __cplusplus
}
#endif

#endif
