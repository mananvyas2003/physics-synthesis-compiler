#ifndef COMPILER_H
#define COMPILER_H

#include "db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char role[64];
  DBPart part;

  char pin1[32];
  char pin2[32];

  char node1[64];
  char node2[64];
} CompiledComponent;

typedef struct {
  char name[64];

  CompiledComponent *components;
  int component_count;
} CompiledSchematic;

DBResult compiler_compile_resistor_divider(DB *db, const char *topology_name,
                                           CompiledSchematic *out);

void compiler_free_schematic(CompiledSchematic *schematic);

int compiler_write_kicad_sch(const char *filename,
                            const CompiledSchematic *schematic);

void compiler_print_schematic(const CompiledSchematic *schematic);

#ifdef __cplusplus
}
#endif

#endif
