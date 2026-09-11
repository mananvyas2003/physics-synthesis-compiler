#ifndef BIND_SCORER_H
#define BIND_SCORER_H

#include "db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  DBPart primary;
  DBPart alternate;
  int has_alternate;
  double primary_error_pct;
  double alternate_error_pct;
  char rationale[256];
  char rejected_summary[256];
  double unit_cost;
} BindChoice;

/*
 * Scored binding with derating floors and E-series error reporting.
 * applied_voltage / dissipation used for 2x derating checks.
 */
int bind_score_passive(DB *db, PartTypes type, double target_value,
                       const char *package, ToleranceClass tol,
                       double applied_voltage, double dissipation_w,
                       BindChoice *out);

#ifdef __cplusplus
}
#endif

#endif
