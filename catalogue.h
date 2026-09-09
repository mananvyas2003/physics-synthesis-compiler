#pragma once

#include "db.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*MpnFormatter)(double value, const char *package, char *out,
                             size_t outlen);

int GenerateSeriesParts(DB *db, PartTypes type, const double *mantissas,
                        int mantissa_count, int decade_min, int decade_max,
                        const char **packages, int package_count,
                        MpnFormatter format_mpn,
                        ToleranceClass tolerance_class);

int GenerateE24Resistors(DB *db);
int GenerateE96Resistors(DB *db);
int GenerateE6Capacitors(DB *db);
DBResult SeedFabRules(DB *db);

void FormatResistanceCode(double ohms, char *out, size_t outlen);
void FormatResistorMpn(double ohms, const char *package, char *out,
                       size_t outlen);
void FormatCapacitanceCode(double farads, char *out, size_t outlen);
void FormatCapacitorMpn(double farads, const char *package, char *out,
                        size_t outlen);

#ifdef __cplusplus
}
#endif
