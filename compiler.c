#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const TopologyConnectionRow *
find_connection(const TopologyConnectionRow *connections, int count,
                const char *role, const char *pin) {
  if (!connections || !role || !pin)
    return NULL;

  for (int i = 0; i < count; i++) {
    if (strcmp(connections[i].component_role, role) == 0 &&
        strcmp(connections[i].pin_name, pin) == 0) {
      return &connections[i];
    }
  }

  return NULL;
}

static const char *find_node_name(const TopologyNodeRow *nodes, int count,
                                  const char *node_name) {
  if (!nodes || !node_name)
    return NULL;

  for (int i = 0; i < count; i++) {
    if (strcmp(nodes[i].node_name, node_name) == 0)
      return nodes[i].node_name;
  }

  return NULL;
}

static void clear_schematic(CompiledSchematic *schematic) {
  if (!schematic)
    return;

  memset(schematic, 0, sizeof(*schematic));
}

DBResult compiler_compile_resistor_divider(DB *db, const char *topology_name,
                                           CompiledSchematic *out) {
  if (!db || !topology_name || !out)
    return DB_ERROR;

  clear_schematic(out);

  TopologyComponentRow components[32];
  TopologyNodeRow nodes[32];
  TopologyConnectionRow connections[64];

  int component_count = 0;
  int node_count = 0;
  int connection_count = 0;

  DBResult result =
      DB_GetTopology(db, topology_name, components, 32, &component_count, nodes,
                     32, &node_count, connections, 64, &connection_count);

  if (result != DB_OK)
    return result;

  strncpy(out->name, topology_name, sizeof(out->name) - 1);

  out->components = calloc((size_t)component_count, sizeof(*out->components));

  if (!out->components)
    return DB_ERROR;

  for (int i = 0; i < component_count; i++) {

    const TopologyComponentRow *tc = &components[i];

    if (tc->part_type != PART_RESISTOR) {
      compiler_free_schematic(out);
      return DB_ERROR;
    }

    CompiledComponent *cc = &out->components[i];

    strncpy(cc->role, tc->role_name, sizeof(cc->role) - 1);

    const TopologyConnectionRow *pin1 =
        find_connection(connections, connection_count, tc->role_name, "1");

    const TopologyConnectionRow *pin2 =
        find_connection(connections, connection_count, tc->role_name, "2");

    if (!pin1 || !pin2) {
      compiler_free_schematic(out);
      return DB_ERROR;
    }

    strncpy(cc->pin1, pin1->pin_name, sizeof(cc->pin1) - 1);

    strncpy(cc->pin2, pin2->pin_name, sizeof(cc->pin2) - 1);

    const char *node1 = find_node_name(nodes, node_count, pin1->node_name);

    const char *node2 = find_node_name(nodes, node_count, pin2->node_name);

    if (!node1 || !node2) {
      compiler_free_schematic(out);
      return DB_ERROR;
    }

    strncpy(cc->node1, node1, sizeof(cc->node1) - 1);

    strncpy(cc->node2, node2, sizeof(cc->node2) - 1);

    result = DB_FindClosestPart(db, PART_RESISTOR, 10000.0, "0603",
                                TOLERANCE_E96, &cc->part);

    if (result != DB_OK) {
      compiler_free_schematic(out);
      return result;
    }

    out->component_count++;
  }

  return DB_OK;
}

void compiler_free_schematic(CompiledSchematic *schematic) {
  if (!schematic)
    return;

  free(schematic->components);

  schematic->components = NULL;
  schematic->component_count = 0;
}

void compiler_print_schematic(const CompiledSchematic *schematic) {
  if (!schematic)
    return;

  printf("\n=== COMPILED SCHEMATIC ===\n");
  printf("Name: %s\n\n", schematic->name);

  printf("Components:\n");

  for (int i = 0; i < schematic->component_count; i++) {

    const CompiledComponent *c = &schematic->components[i];

    printf("  %s -> %s (%s, %s)\n", c->role, c->part.mpn, c->part.package,
           c->node1);

    printf("      pin %s -> %s\n", c->pin1, c->node1);

    printf("      pin %s -> %s\n", c->pin2, c->node2);
  }

  printf("\nConnections:\n");

  for (int i = 0; i < schematic->component_count; i++) {

    const CompiledComponent *c = &schematic->components[i];

    printf("  %s.1 ---- %s\n", c->role, c->node1);

    printf("  %s.2 ---- %s\n", c->role, c->node2);
  }

  printf("\n");
}

static void make_uuid(char *out, size_t out_len, unsigned int index) {
  snprintf(out, out_len, "00000000-0000-4000-8000-%012u", index);
}

static const char *kicad_footprint_for_package(const char *package) {
  if (!package)
    return "";

  if (strcmp(package, "0402") == 0)
    return "Resistor_SMD:R_0402_1005Metric";
  if (strcmp(package, "0603") == 0)
    return "Resistor_SMD:R_0603_1608Metric";
  if (strcmp(package, "0805") == 0)
    return "Resistor_SMD:R_0805_2012Metric";
  if (strcmp(package, "1206") == 0)
    return "Resistor_SMD:R_1206_3216Metric";

  return "";
}

static void write_resistor_library_symbol(FILE *fp) {
  fprintf(fp, "\t\t(symbol \"Device:R\"\n"
              "\t\t\t(pin_numbers\n"
              "\t\t\t\t(hide yes)\n"
              "\t\t\t)\n"
              "\t\t\t(pin_names\n"
              "\t\t\t\t(offset 0)\n"
              "\t\t\t)\n"
              "\t\t\t(exclude_from_sim no)\n"
              "\t\t\t(in_bom yes)\n"
              "\t\t\t(on_board yes)\n"
              "\t\t\t(property \"Reference\" \"R\"\n"
              "\t\t\t\t(at 2.032 0 90)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(property \"Value\" \"R\"\n"
              "\t\t\t\t(at 0 0 90)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(property \"Footprint\" \"\"\n"
              "\t\t\t\t(at -1.778 0 90)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t\t(hide yes)\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(property \"Datasheet\" \"~\"\n"
              "\t\t\t\t(at 0 0 0)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t\t(hide yes)\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(property \"Description\" \"Resistor\"\n"
              "\t\t\t\t(at 0 0 0)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t\t(hide yes)\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(property \"ki_keywords\" \"R res resistor\"\n"
              "\t\t\t\t(at 0 0 0)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t\t(hide yes)\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(property \"ki_fp_filters\" \"R_*\"\n"
              "\t\t\t\t(at 0 0 0)\n"
              "\t\t\t\t(effects\n"
              "\t\t\t\t\t(font (size 1.27 1.27))\n"
              "\t\t\t\t\t(hide yes)\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(symbol \"R_0_1\"\n"
              "\t\t\t\t(rectangle\n"
              "\t\t\t\t\t(start -1.016 -2.54)\n"
              "\t\t\t\t\t(end 1.016 2.54)\n"
              "\t\t\t\t\t(stroke (width 0.254) (type default))\n"
              "\t\t\t\t\t(fill (type none))\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(symbol \"R_1_1\"\n"
              "\t\t\t\t(pin passive line\n"
              "\t\t\t\t\t(at 0 3.81 270)\n"
              "\t\t\t\t\t(length 1.27)\n"
              "\t\t\t\t\t(name \"~\" (effects (font (size 1.27 1.27))))\n"
              "\t\t\t\t\t(number \"1\" (effects (font (size 1.27 1.27))))\n"
              "\t\t\t\t)\n"
              "\t\t\t\t(pin passive line\n"
              "\t\t\t\t\t(at 0 -3.81 90)\n"
              "\t\t\t\t\t(length 1.27)\n"
              "\t\t\t\t\t(name \"~\" (effects (font (size 1.27 1.27))))\n"
              "\t\t\t\t\t(number \"2\" (effects (font (size 1.27 1.27))))\n"
              "\t\t\t\t)\n"
              "\t\t\t)\n"
              "\t\t\t(embedded_fonts no)\n"
              "\t\t)\n");
}

static void write_placed_resistor(FILE *fp, const CompiledComponent *component,
                                  double x, double y, const char *uuid,
                                  const char *pin1_uuid,
                                  const char *pin2_uuid) {
  const char *footprint = kicad_footprint_for_package(component->part.package);

  fprintf(fp,
          "\t(symbol\n"
          "\t\t(lib_id \"Device:R\")\n"
          "\t\t(at %.2f %.2f 180)\n"
          "\t\t(unit 1)\n"
          "\t\t(exclude_from_sim no)\n"
          "\t\t(in_bom yes)\n"
          "\t\t(on_board yes)\n"
          "\t\t(dnp no)\n"
          "\t\t(fields_autoplaced yes)\n"
          "\t\t(uuid \"%s\")\n"
          "\t\t(property \"Reference\" \"%s\"\n"
          "\t\t\t(at %.2f %.2f 0)\n"
          "\t\t\t(effects\n"
          "\t\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t\t(justify left)\n"
          "\t\t\t)\n"
          "\t\t)\n"
          "\t\t(property \"Value\" \"%s\"\n"
          "\t\t\t(at %.2f %.2f 0)\n"
          "\t\t\t(effects\n"
          "\t\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t\t(justify left)\n"
          "\t\t\t)\n"
          "\t\t)\n"
          "\t\t(property \"Footprint\" \"%s\"\n"
          "\t\t\t(at %.2f %.2f 90)\n"
          "\t\t\t(effects\n"
          "\t\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t\t(hide yes)\n"
          "\t\t\t)\n"
          "\t\t)\n"
          "\t\t(property \"Datasheet\" \"~\"\n"
          "\t\t\t(at %.2f %.2f 0)\n"
          "\t\t\t(effects\n"
          "\t\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t\t(hide yes)\n"
          "\t\t\t)\n"
          "\t\t)\n"
          "\t\t(property \"Description\" \"Resistor\"\n"
          "\t\t\t(at %.2f %.2f 0)\n"
          "\t\t\t(effects\n"
          "\t\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t\t(hide yes)\n"
          "\t\t\t)\n"
          "\t\t)\n"
          "\t\t(pin \"1\" (uuid \"%s\"))\n"
          "\t\t(pin \"2\" (uuid \"%s\"))\n"
          "\t\t(instances\n"
          "\t\t\t(project \"\"\n"
          "\t\t\t\t(path \"/00000000-0000-4000-8000-000000000001\"\n"
          "\t\t\t\t\t(reference \"%s\")\n"
          "\t\t\t\t\t(unit 1)\n"
          "\t\t\t\t)\n"
          "\t\t\t)\n"
          "\t\t)\n"
          "\t)\n",
          x, y, uuid, component->role, x + 2, y - 5, component->part.mpn, x + 2,
          y + 5, footprint, x - 2, y, x, y, x, y, pin1_uuid, pin2_uuid,
          component->role);
}

static void write_wire(FILE *fp, double x1, double y1, double x2, double y2,
                       unsigned int id) {
  char uuid[64];
  make_uuid(uuid, sizeof(uuid), id);

  fprintf(fp,
          "\t(wire\n"
          "\t\t(pts\n"
          "\t\t\t(xy %.2f %.2f) (xy %.2f %.2f)\n"
          "\t\t)\n"
          "\t\t(stroke (width 0) (type default))\n"
          "\t\t(uuid \"%s\")\n"
          "\t)\n",
          x1, y1, x2, y2, uuid);
}

static void write_label(FILE *fp, const char *text, double x, double y,
                        unsigned int id) {
  char uuid[64];
  make_uuid(uuid, sizeof(uuid), id);

  fprintf(fp,
          "\t(label \"%s\"\n"
          "\t\t(at %.2f %.2f 0)\n"
          "\t\t(effects\n"
          "\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t(justify left bottom)\n"
          "\t\t)\n"
          "\t\t(uuid \"%s\")\n"
          "\t)\n",
          text, x, y, uuid);
}

int compiler_write_kicad_sch(const char *filename,
                             const CompiledSchematic *schematic) {
  if (!filename || !schematic)
    return 0;

  if (schematic->component_count != 2)
    return 0;

  FILE *fp = fopen(filename, "w");
  if (!fp)
    return 0;

  fprintf(fp, "(kicad_sch\n"
              "\t(version 20250114)\n"
              "\t(generator \"eeschema\")\n"
              "\t(generator_version \"9.0\")\n"
              "\t(uuid \"00000000-0000-4000-8000-000000000001\")\n"
              "\t(paper \"A4\")\n"
              "\t(lib_symbols\n");

  write_resistor_library_symbol(fp);
  fprintf(fp, "\t)\n");

  const CompiledComponent *r1 = &schematic->components[0];
  const CompiledComponent *r2 = &schematic->components[1];

  write_placed_resistor(fp, r1, 100, 80, "00000000-0000-4000-8000-000000000101",
                        "00000000-0000-4000-8000-000000000111",
                        "00000000-0000-4000-8000-000000000112");

  write_placed_resistor(fp, r2, 100, 110,
                        "00000000-0000-4000-8000-000000000102",
                        "00000000-0000-4000-8000-000000000121",
                        "00000000-0000-4000-8000-000000000122");

  write_wire(fp, 100, 76.19, 100, 65.00, 201);
  write_wire(fp, 100, 83.81, 100, 106.19, 202);
  write_wire(fp, 100, 113.81, 100, 125.00, 203);

  write_label(fp, "VIN", 100, 65, 301);
  write_label(fp, "VOUT", 100, 95, 302);
  write_label(fp, "GND", 100, 125, 303);

  fprintf(fp, "\t(sheet_instances\n"
              "\t\t(path \"/\"\n"
              "\t\t\t(page \"1\")\n"
              "\t\t)\n"
              "\t)\n"
              "\t(embedded_fonts no)\n"
              ")\n");

  fclose(fp);
  return 1;
}
