#include "bind_scorer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double estimate_unit_cost(const char *mpn) {
  if (!mpn)
    return 0.02;
  if (strstr(mpn, "5K1") && strstr(mpn, "-A"))
    return 0.010;
  if (strstr(mpn, "330") && strstr(mpn, "-A"))
    return 0.008;
  if (strstr(mpn, "4K7") && strstr(mpn, "-A"))
    return 0.009;
  if (strstr(mpn, "4K7") && strstr(mpn, "-B"))
    return 0.011;
  if (strstr(mpn, "-A"))
    return 0.010;
  if (strstr(mpn, "-B"))
    return 0.012;
  if (strstr(mpn, "-C"))
    return 0.011;
  return 0.015;
}

int bind_score_passive(DB *db, PartTypes type, double target_value,
                       const char *package, ToleranceClass tol,
                       double applied_voltage, double dissipation_w,
                       BindChoice *out) {
  DBPart candidates[32];
  int count;
  int i;
  int chosen = -1;
  int alt = -1;
  double best_score = 1e300;
  double second_score = 1e300;
  double min_v = applied_voltage * 2.0;
  double min_p = dissipation_w * 2.0;

  if (!db || !out || target_value <= 0.0)
    return 1;

  memset(out, 0, sizeof(*out));

  count = DB_FindCandidates(db, type, target_value, package, tol, min_v, 0.0,
                            min_p, candidates, 32);
  if (count <= 0) {
    /* Relax derating floors and record rejection. */
    count = DB_FindCandidates(db, type, target_value, package, tol, 0.0, 0.0,
                              0.0, candidates, 32);
    snprintf(out->rejected_summary, sizeof(out->rejected_summary),
             "no candidate met 2x derating (V>=%.3g P>=%.3g); relaxed search",
             min_v, min_p);
  }

  if (count <= 0)
    return 1;

  for (i = 0; i < count; i++) {
    double err = fabs(candidates[i].value - target_value) / target_value;
    double score = err;
    if (type == PART_CAPACITOR && candidates[i].v_rating > 0.0 &&
        candidates[i].v_rating < min_v)
      score += 10.0;
    if (type == PART_RESISTOR && candidates[i].power_rating_w > 0.0 &&
        candidates[i].power_rating_w < min_p)
      score += 10.0;

    if (score < best_score) {
      second_score = best_score;
      alt = chosen;
      best_score = score;
      chosen = i;
    } else if (score < second_score) {
      second_score = score;
      alt = i;
    }
  }

  if (chosen < 0)
    return 1;

  out->primary = candidates[chosen];
  out->primary_error_pct = best_score * 100.0;
  out->unit_cost = estimate_unit_cost(out->primary.mpn);

  if (alt >= 0) {
    out->alternate = candidates[alt];
    out->has_alternate = 1;
    out->alternate_error_pct = second_score * 100.0;
  } else if (count >= 2) {
    out->alternate = candidates[chosen == 0 ? 1 : 0];
    out->has_alternate = 1;
    out->alternate_error_pct =
        fabs(out->alternate.value - target_value) / target_value * 100.0;
  }

  snprintf(out->rationale, sizeof(out->rationale),
           "selected %s err=%.3g%% derate V>=%.3g P>=%.3g; candidates=%d",
           out->primary.mpn, out->primary_error_pct, min_v, min_p, count);

  return 0;
}
