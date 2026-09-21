#ifndef VENDOR_BRIDGE_H
#define VENDOR_BRIDGE_H

#include "compiler.h"
#include "mfg_dfm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Map CompiledSchematic → teammate Design IR, then run
 * electronics_vendor_v2_next DFM (and optionally stamp MNA context).
 */
int vendor_dfm_check_schematic(const CompiledSchematic *schematic,
                               const MfgDfmProfile *profile,
                               const char *report_path, MfgDfmResult *out);

#ifdef __cplusplus
}
#endif

#endif
