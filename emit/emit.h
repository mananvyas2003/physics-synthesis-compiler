#ifndef EMIT_H
#define EMIT_H

#include "compiler.h"
#include "db.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int emit_ki_cad_netlist(const char *path, const CompiledSchematic *schematic);

int emit_bom_csv(const char *path, const CompiledSchematic *schematic);

int emit_design_snapshot_v1(const char *path, const CompiledSchematic *schematic);

/* Validate a snapshot JSON file against the frozen v1 shape (structural). */
int emit_snapshot_validate_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif
