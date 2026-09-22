#ifndef COMPILER_H
#define COMPILER_H

#include "db.h"
#include "physics2_interpreter.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char role[64];
  DBPart part;

  /* Up to PART_LIB_MAX_PINS terminals; pin1/node1 kept as pins[0]/nodes[0]. */
  int pin_count;
  char pins[8][32];
  char nodes[8][64];

  char pin1[32];
  char pin2[32];
  char pin3[32];
  char node1[64];
  char node2[64];
  char node3[64];

  /* M4 scored binding metadata */
  char rationale[256];
  char alternate_mpn[64];
  int has_alternate;
  double unit_cost;
} CompiledComponent;

typedef struct {
  char name[64];

  CompiledComponent *components;
  int component_count;
} CompiledSchematic;

DBResult compiler_compile_resistor_divider(DB *db, const char *topology_name,
                                           CompiledSchematic *out);

/*
 * Compile topology from DB connectivity + optional bind hints in design JSON
 * (components[].target_value / package). Missing hints default to 10k/0603.
 */
DBResult compiler_compile_from_design(DB *db, const char *topology_name,
                                      const char *design_json_path,
                                      CompiledSchematic *out);

void compiler_free_schematic(CompiledSchematic *schematic);

int compiler_write_kicad_sch(const char *filename,
                             const CompiledSchematic *schematic);

void compiler_print_schematic(const CompiledSchematic *schematic);

/* =========================================================
 * Physics2 physical design IR + deterministic lowering
 *
 * Numerical/physical only. No MPNs in the Physics2 stream.
 * ========================================================= */

typedef enum {
  COMPILER_PHYS_NONE = 0,
  COMPILER_PHYS_RESISTOR,
  COMPILER_PHYS_CAPACITOR,
  COMPILER_PHYS_INDUCTOR,
  COMPILER_PHYS_VSOURCE,
  COMPILER_PHYS_ISOURCE,
  COMPILER_PHYS_DIODE,
  COMPILER_PHYS_VCVS,
  COMPILER_PHYS_VCCS,
  COMPILER_PHYS_CCVS,
  COMPILER_PHYS_CCCS
} CompilerPhysKind;

typedef struct {
  CompilerPhysKind kind;
  char name[64];
  double value; /* R/C/L/V/I/diode Isat; controlled-source gain (μ/gm/Rm/β) */
  double tolerance_pct;
  double diode_n;  /* DIODE only; 0 → default 1 */
  double diode_vt; /* DIODE only; 0 → default kT/q @300K */
  uint8_t terminal_count;
  char terminals[PHYSICS2_MAX_TERMINALS][64];
} CompilerPhysElement;

typedef struct {
  CompilerPhysElement *elements;
  size_t count;
  size_t capacity;
} CompilerPhysDesign;

typedef struct {
  char name[64];
  NodeId id;
} CompiledPhysicsNode;

typedef struct {
  PhysicsProgram program;
  PhysicsPrimitive *primitives;
  size_t primitive_count;
  CompiledPhysicsNode *nodes;
  size_t node_count;
} CompiledPhysicsProgram;

void compiler_physics_design_init(CompilerPhysDesign *design);
void compiler_physics_design_free(CompilerPhysDesign *design);

bool compiler_physics_design_add(CompilerPhysDesign *design,
                                 CompilerPhysKind kind, const char *name,
                                 double value, double tolerance_pct,
                                 const char *const *terminals,
                                 uint8_t terminal_count);

/* Bound schematic → numerical Physical IR (no MPNs). Fail closed on unsupported. */
bool compiler_schematic_to_phys_design(const CompiledSchematic *schematic,
                                       double supply_v,
                                       CompilerPhysDesign *out);

bool compiler_lower_to_physics2(const CompilerPhysDesign *design,
                                CompiledPhysicsProgram *out);

NodeId compiler_physics_find_node(const CompiledPhysicsProgram *compiled,
                                  const char *name);

void compiler_free_physics_program(CompiledPhysicsProgram *compiled);

#ifdef __cplusplus
}
#endif

#endif
