#include "catalogue.h"
#include "db.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Defines a statically linked lookup structure mapped to IEC 60115 standards */
typedef struct {
  const char *package;
  double default_power_w;
  double default_max_v;
} PackageRatingMap;

static const PackageRatingMap RESISTOR_RATINGS[] = {
    {"0402", 0.0625, 50.0}, {"0603", 0.1, 50.0}, {"0805", 0.125, 150.0}};
static const int RESISTOR_RATINGS_COUNT =
    sizeof(RESISTOR_RATINGS) / sizeof(PackageRatingMap);

static double DefaultResistorPowerRating(const char *package) {
  for (int i = 0; i < RESISTOR_RATINGS_COUNT; i++) {
    if (strcmp(package, RESISTOR_RATINGS[i].package) == 0) {
      return RESISTOR_RATINGS[i].default_power_w;
    }
  }
  return 0.0;
}

static double DefaultResistorVoltageRating(const char *package) {
  for (int i = 0; i < RESISTOR_RATINGS_COUNT; i++) {
    if (strcmp(package, RESISTOR_RATINGS[i].package) == 0) {
      return RESISTOR_RATINGS[i].default_max_v;
    }
  }
  return 0.0;
}

static const double E96_MANTISSAS[96] = {
    1.00, 1.02, 1.05, 1.07, 1.10, 1.13, 1.15, 1.18, 1.21, 1.24, 1.27, 1.30,
    1.33, 1.37, 1.40, 1.43, 1.47, 1.50, 1.54, 1.58, 1.62, 1.65, 1.69, 1.74,
    1.78, 1.82, 1.87, 1.91, 1.96, 2.00, 2.05, 2.10, 2.15, 2.21, 2.26, 2.32,
    2.37, 2.43, 2.49, 2.55, 2.61, 2.67, 2.74, 2.80, 2.87, 2.94, 3.01, 3.09,
    3.16, 3.24, 3.32, 3.40, 3.48, 3.57, 3.65, 3.74, 3.83, 3.92, 4.02, 4.12,
    4.22, 4.32, 4.42, 4.53, 4.64, 4.75, 4.87, 4.99, 5.11, 5.23, 5.36, 5.49,
    5.62, 5.76, 5.90, 6.04, 6.19, 6.34, 6.49, 6.65, 6.81, 6.98, 7.15, 7.32,
    7.50, 7.68, 7.87, 8.06, 8.25, 8.45, 8.66, 8.87, 9.09, 9.31, 9.53, 9.76};

static const double E24_MANTISSAS[24] = {
    1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
    3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1};

static const double E6_MANTISSAS[6] = {1.0, 1.5, 2.2, 3.3, 4.7, 6.8};

static const char *STANDARD_PACKAGES[3] = {"0402", "0603", "0805"};

/* --- Resistor Formatting --- */
void FormatResistanceCode(double ohms, char *out, size_t outlen) {
  char unit = 'R';
  double base = ohms;

  if (base >= 1000000.0) {
    unit = 'M';
    base /= 1000000.0;
  } else if (base >= 1000.0) {
    unit = 'K';
    base /= 1000.0;
  }

  if (base < 10.0) {
    int major = (int)base;
    int minor = (int)round((base - major) * 10.0);
    snprintf(out, outlen, "%d%c%d", major, unit, minor);
  } else {
    snprintf(out, outlen, "%d%c", (int)round(base), unit);
  }
}

void FormatResistorMpn(double ohms, const char *package, char *out,
                       size_t outlen) {
  char code[16];
  FormatResistanceCode(ohms, code, sizeof(code));
  snprintf(out, outlen, "RES-%s-%s", package, code);
}

/* --- Capacitor EIA 3-Digit Formatting --- */
void FormatCapacitanceCode(double farads, char *out, size_t outlen) {
  double picofarads = farads * 1.0e12;

  if (picofarads < 10.0) {
    int major = (int)picofarads;
    int minor = (int)round((picofarads - major) * 10.0);
    snprintf(out, outlen, "%dC%d", major, minor);
  } else {
    int exp = (int)floor(log10(picofarads)) - 1;
    if (exp < 0)
      exp = 0;

    int mantissa = (int)round(picofarads / pow(10.0, exp));
    if (mantissa >= 100) {
      mantissa /= 10;
      exp++;
    }
    snprintf(out, outlen, "%02d%d", mantissa, exp);
  }
}

void FormatCapacitorMpn(double farads, const char *package, char *out,
                        size_t outlen) {
  char code[16];
  FormatCapacitanceCode(farads, code, sizeof(code));
  snprintf(out, outlen, "CAP-%s-%s", package, code);
}

/* --- Generalized Generator --- */
int GenerateSeriesParts(DB *db, PartTypes type, const double *mantissas,
                        int mantissa_count, int decade_min, int decade_max,
                        const char **packages, int package_count,
                        MpnFormatter format_mpn,
                        ToleranceClass tolerance_class) {

  double first_val = mantissas[1] * pow(10.0, decade_min);
  DBPart probe;

  if (DB_FindCandidates(db, type, first_val, packages[0], tolerance_class, 0.0,
                        0.0, 0.0, &probe, 1) > 0) {
    return 0; /* Catalog is already seeded, skip generation silently */
  }

  if (DB_BeginTransaction(db) != DB_OK) {
    fprintf(stderr,
            "[GenerateSeriesParts ERROR]: Failed to start transaction.\n");
    return 0;
  }

  int inserted = 0;
  for (int decade = decade_min; decade <= decade_max; decade++) {
    double multiplier = pow(10.0, decade);
    for (int m = 0; m < mantissa_count; m++) {
      double value = mantissas[m] * multiplier;
      for (int p = 0; p < package_count; p++) {
        const char *pkg = packages[p];
        DBPart part;
        memset(&part, 0, sizeof(part));

        part.type = type;
        part.value = value;
        part.tolerance_class =
            tolerance_class; /* <-- FIX: Explicitly assign tolerance class */
        snprintf(part.package, sizeof(part.package), "%s", pkg);

        if (type == PART_RESISTOR) {
          part.power_rating_w = DefaultResistorPowerRating(pkg);
          part.v_rating = DefaultResistorVoltageRating(pkg);
        } else if (type == PART_CAPACITOR) {
          part.v_rating = 25.0;
        }

        format_mpn(value, pkg, part.mpn, sizeof(part.mpn));

        int64_t new_id = 0;
        if (DB_InsertPartFull(db, &part, &new_id) == DB_OK) {
          inserted++;
        }
      }
    }
  }

  if (DB_CommitTransaction(db) != DB_OK) {
    fprintf(stderr,
            "[GenerateSeriesParts ERROR]: Failed to commit transaction.\n");
    DB_RollbackTransaction(db);
    return 0;
  }

  return inserted;
}

int GenerateE24Resistors(DB *db) {
  return GenerateSeriesParts(db, PART_RESISTOR, E24_MANTISSAS, 24, -1, 6,
                             STANDARD_PACKAGES, 3, FormatResistorMpn,
                             TOLERANCE_E24);
}

int GenerateE96Resistors(DB *db) {
  return GenerateSeriesParts(db, PART_RESISTOR, E96_MANTISSAS, 96, -1, 6,
                             STANDARD_PACKAGES, 3, FormatResistorMpn,
                             TOLERANCE_E96);
}

int GenerateE6Capacitors(DB *db) {
  return GenerateSeriesParts(db, PART_CAPACITOR, E6_MANTISSAS, 6, -12, -6,
                             STANDARD_PACKAGES, 3, FormatCapacitorMpn,
                             TOLERANCE_E12);
}

DBResult SeedFabRules(DB *db) {
  int64_t id = 0;
  return DB_InsertFabRule(db, "JLCPCB-2Layer", 2, 0.127, 0.127, 0.3, 0.15, &id);
}
