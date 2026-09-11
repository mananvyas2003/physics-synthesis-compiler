#ifndef SPEC_LOAD_H
#define SPEC_LOAD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char rails[8][32];
  int rail_count;
  double current_a;
  double ripple_v;
  double efficiency_target;
  char form_factor[64];
  char part_preference[64];
  char required_interfaces[16][32];
  int interface_count;
  bool inferred_current;
  bool inferred_efficiency;
  char clarifying_question[256];
} SpecV1;

int spec_load_file(const char *path, SpecV1 *out);
int spec_validate(const SpecV1 *spec);
int spec_load_and_validate(const char *path, SpecV1 *out);

/* Map a validated spec onto a seed design path for offline generate. */
int spec_resolve_design_path(const SpecV1 *spec, char *out_path, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif
