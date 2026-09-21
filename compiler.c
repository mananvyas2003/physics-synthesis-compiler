#include "compiler.h"

#include "bind_scorer.h"
#include "cJSON.h"
#include "diag_error.h"
#include "part_lib.h"
#include "unit_parse.h"

#include <math.h>
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

static const TopologyConnectionRow *
find_connection_any(const TopologyConnectionRow *connections, int count,
                    const char *role, const char *const *pins, int npins) {
  int p;
  for (p = 0; p < npins; p++) {
    const TopologyConnectionRow *row =
        find_connection(connections, count, role, pins[p]);
    if (row)
      return row;
  }
  return NULL;
}

static double default_target_for_type(PartTypes type) {
  switch (type) {
  case PART_CAPACITOR:
    return 100e-9;
  case PART_INDUCTOR:
    return 10e-6;
  case PART_DIODE:
    return 2.0;
  case PART_TRANSISTOR:
  case PART_IC:
  case PART_CONNECTOR:
    return 1.0;
  case PART_OTHER:
    return 3.7;
  case PART_RESISTOR:
  default:
    return 10000.0;
  }
}

static int type_is_bindable(PartTypes type) {
  return type == PART_RESISTOR || type == PART_CAPACITOR ||
         type == PART_INDUCTOR || type == PART_DIODE ||
         type == PART_TRANSISTOR || type == PART_IC || type == PART_CONNECTOR ||
         type == PART_OTHER;
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

typedef struct {
  char role[64];
  double target_value;
  char package[32];
  int has_hint;
} BindHint;

static int load_bind_hints(const char *design_json_path, BindHint *hints,
                           int max_hints, int *out_count) {
  FILE *fp;
  long size;
  char *buf;
  cJSON *root;
  cJSON *components;
  cJSON *item;
  int count = 0;

  if (out_count)
    *out_count = 0;
  if (!design_json_path || !hints || max_hints <= 0)
    return 1;

  fp = fopen(design_json_path, "rb");
  if (!fp)
    return 1;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 1;
  }
  size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return 1;
  }
  rewind(fp);
  buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(fp);
    return 1;
  }
  if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
    free(buf);
    fclose(fp);
    return 1;
  }
  buf[size] = '\0';
  fclose(fp);

  root = cJSON_Parse(buf);
  free(buf);
  if (!root)
    return 1;

  components = cJSON_GetObjectItemCaseSensitive(root, "components");
  if (cJSON_IsArray(components)) {
    cJSON_ArrayForEach(item, components) {
      const char *role;
      cJSON *tv;
      cJSON *pkg;
      if (count >= max_hints)
        break;
      role = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
      if (!role)
        continue;
      memset(&hints[count], 0, sizeof(hints[count]));
      strncpy(hints[count].role, role, sizeof(hints[count].role) - 1);
      tv = cJSON_GetObjectItemCaseSensitive(item, "target_value");
      pkg = cJSON_GetObjectItemCaseSensitive(item, "package");
      if (unit_parse_number_or_string(tv, &hints[count].target_value) == 0 &&
          hints[count].target_value > 0.0) {
        hints[count].has_hint = 1;
      } else {
        hints[count].target_value = 0.0;
      }
      if (cJSON_IsString(pkg))
        strncpy(hints[count].package, pkg->valuestring,
                sizeof(hints[count].package) - 1);
      else
        strncpy(hints[count].package, "0603", sizeof(hints[count].package) - 1);
      count++;
    }
  }

  cJSON_Delete(root);
  if (out_count)
    *out_count = count;
  return 0;
}

static const BindHint *find_hint(const BindHint *hints, int count,
                                 const char *role) {
  int i;
  for (i = 0; i < count; i++) {
    if (strcmp(hints[i].role, role) == 0)
      return &hints[i];
  }
  return NULL;
}

DBResult compiler_compile_from_design(DB *db, const char *topology_name,
                                      const char *design_json_path,
                                      CompiledSchematic *out) {
  TopologyComponentRow components[32];
  TopologyNodeRow nodes[32];
  TopologyConnectionRow connections[64];
  BindHint hints[32];
  int component_count = 0;
  int node_count = 0;
  int connection_count = 0;
  int hint_count = 0;
  DBResult result;
  int i;

  if (!db || !topology_name || !out)
    return DB_ERROR;

  clear_schematic(out);
  (void)load_bind_hints(design_json_path, hints, 32, &hint_count);

  result =
      DB_GetTopology(db, topology_name, components, 32, &component_count, nodes,
                     32, &node_count, connections, 64, &connection_count);
  if (result != DB_OK)
    return result;

  strncpy(out->name, topology_name, sizeof(out->name) - 1);
  out->components = calloc((size_t)component_count, sizeof(*out->components));
  if (!out->components)
    return DB_ERROR;

  for (i = 0; i < component_count; i++) {
    const TopologyComponentRow *tc = &components[i];
    CompiledComponent *cc;
    const BindHint *hint;
    BindChoice choice;
    double target;
    const char *package = "0603";
    double applied_v = 5.0;
    double dissip = 0.0;
    ToleranceClass tol = TOLERANCE_E24;
    const char *req_pins[8];
    int nreq;
    int p;
    const char *type_name = part_lib_type_label(tc->part_type);

    if (!type_is_bindable(tc->part_type)) {
      diag_set_error(
          "%s components are not currently available (role '%s').",
          part_lib_type_label(tc->part_type), tc->role_name);
      compiler_free_schematic(out);
      return DB_ERROR;
    }

    nreq = -1;
    {
      static const char *try_names[] = {
          "resistor", "capacitor", "inductor", "led", "diode", "transistor",
          "mosfet", "regulator", "ldo", "opamp", "switch", "battery",
          "connector", NULL};
      int t;
      for (t = 0; try_names[t]; t++) {
        const char *candidate[8];
        int nc;
        int ok = 1;
        int q;
        if (part_lib_db_type(try_names[t]) != tc->part_type)
          continue;
        nc = part_lib_required_pins(try_names[t], candidate, 8);
        if (nc <= 0)
          continue;
        for (q = 0; q < nc; q++) {
          if (!find_connection(connections, connection_count, tc->role_name,
                               candidate[q])) {
            const PartLibEntry *ent = part_lib_find(try_names[t]);
            int a;
            int hit = 0;
            if (ent && q < ent->pin_count) {
              for (a = 0; a < ent->pins[q].alias_count; a++) {
                if (find_connection(connections, connection_count,
                                    tc->role_name, ent->pins[q].aliases[a])) {
                  hit = 1;
                  break;
                }
              }
            }
            if (!hit) {
              ok = 0;
              break;
            }
          }
        }
        if (ok) {
          for (q = 0; q < nc; q++)
            req_pins[q] = candidate[q];
          nreq = nc;
          type_name = try_names[t];
          break;
        }
      }
    }
    if (nreq < 0) {
      req_pins[0] = "1";
      req_pins[1] = "2";
      nreq = 2;
    }

    cc = &out->components[i];
    memset(cc, 0, sizeof(*cc));
    strncpy(cc->role, tc->role_name, sizeof(cc->role) - 1);
    cc->pin_count = nreq;

    for (p = 0; p < nreq; p++) {
      const TopologyConnectionRow *row =
          find_connection(connections, connection_count, tc->role_name,
                          req_pins[p]);
      const char *node;
      if (!row) {
        const PartLibEntry *ent = part_lib_find(type_name);
        int a;
        if (ent && p < ent->pin_count) {
          for (a = 0; a < ent->pins[p].alias_count && !row; a++) {
            row = find_connection(connections, connection_count, tc->role_name,
                                  ent->pins[p].aliases[a]);
          }
        }
      }
      if (!row) {
        diag_set_error(
            "Component '%s' is missing pin '%s'. No schematic was emitted.",
            tc->role_name, req_pins[p]);
        compiler_free_schematic(out);
        return DB_ERROR;
      }
      node = find_node_name(nodes, node_count, row->node_name);
      if (!node) {
        diag_set_error(
            "Component '%s' pin '%s' connects to unknown net '%s'.",
            tc->role_name, req_pins[p], row->node_name);
        compiler_free_schematic(out);
        return DB_ERROR;
      }
      strncpy(cc->pins[p], req_pins[p], sizeof(cc->pins[p]) - 1);
      strncpy(cc->nodes[p], node, sizeof(cc->nodes[p]) - 1);
      if (p == 0) {
        strncpy(cc->pin1, req_pins[p], sizeof(cc->pin1) - 1);
        strncpy(cc->node1, node, sizeof(cc->node1) - 1);
      } else if (p == 1) {
        strncpy(cc->pin2, req_pins[p], sizeof(cc->pin2) - 1);
        strncpy(cc->node2, node, sizeof(cc->node2) - 1);
      } else if (p == 2) {
        strncpy(cc->pin3, req_pins[p], sizeof(cc->pin3) - 1);
        strncpy(cc->node3, node, sizeof(cc->node3) - 1);
      }
    }

    target = default_target_for_type(tc->part_type);
    hint = find_hint(hints, hint_count, tc->role_name);
    if (hint) {
      if (hint->has_hint && hint->target_value > 0.0)
        target = hint->target_value;
      package = hint->package;
    }

    if (tc->part_type == PART_RESISTOR && target > 0.0)
      dissip = (applied_v * applied_v) / target;
    if (tc->part_type == PART_CAPACITOR || tc->part_type == PART_INDUCTOR ||
        tc->part_type == PART_DIODE || tc->part_type == PART_TRANSISTOR ||
        tc->part_type == PART_IC)
      tol = TOLERANCE_E12;

    if (bind_score_passive(db, tc->part_type, target, package, tol, applied_v,
                           dissip, &choice) != 0) {
      if (bind_score_passive(db, tc->part_type, target, "0603", tol, applied_v,
                             dissip, &choice) != 0 &&
          bind_score_passive(db, tc->part_type, target, "SOT-23", tol,
                             applied_v, dissip, &choice) != 0) {
        diag_set_error(
            "No %s matching ~%.4g in package %s in the active catalogue "
            "(role '%s'). Upload a parts CSV or include the part in IR "
            "parts[]. No schematic was emitted.",
            part_lib_type_label(tc->part_type), target, package, tc->role_name);
        compiler_free_schematic(out);
        return DB_NOT_FOUND;
      }
    }

    cc->part = choice.primary;
    strncpy(cc->rationale, choice.rationale, sizeof(cc->rationale) - 1);
    cc->unit_cost = choice.unit_cost;
    cc->has_alternate = choice.has_alternate;
    if (choice.has_alternate)
      strncpy(cc->alternate_mpn, choice.alternate.mpn,
              sizeof(cc->alternate_mpn) - 1);

    out->component_count++;
  }

  return DB_OK;
}

DBResult compiler_compile_resistor_divider(DB *db, const char *topology_name,
                                           CompiledSchematic *out) {
  return compiler_compile_from_design(db, topology_name, NULL, out);
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

static const char *kicad_lib_id(PartTypes type) {
  return part_lib_kicad_id(type);
}

static const char *kicad_footprint_for_package(const char *package,
                                              PartTypes type) {
  const char *prefix = "Resistor_SMD:R_";
  static char buf[96];
  if (!package)
    return "";
  if (type == PART_CAPACITOR)
    prefix = "Capacitor_SMD:C_";
  else if (type == PART_INDUCTOR)
    prefix = "Inductor_SMD:L_";
  else if (type == PART_DIODE)
    prefix = "LED_SMD:LED_";
  if (strcmp(package, "0402") == 0 || strcmp(package, "0603") == 0 ||
      strcmp(package, "0805") == 0 || strcmp(package, "1206") == 0) {
    if (type == PART_DIODE && strcmp(package, "0603") == 0)
      snprintf(buf, sizeof(buf), "LED_SMD:LED_0603_1608Metric");
    else if (type == PART_CAPACITOR && strcmp(package, "0603") == 0)
      snprintf(buf, sizeof(buf), "Capacitor_SMD:C_0603_1608Metric");
    else if (type == PART_RESISTOR && strcmp(package, "0402") == 0)
      return "Resistor_SMD:R_0402_1005Metric";
    else if (type == PART_RESISTOR && strcmp(package, "0603") == 0)
      return "Resistor_SMD:R_0603_1608Metric";
    else if (type == PART_RESISTOR && strcmp(package, "0805") == 0)
      return "Resistor_SMD:R_0805_2012Metric";
    else if (type == PART_RESISTOR && strcmp(package, "1206") == 0)
      return "Resistor_SMD:R_1206_3216Metric";
    else {
      snprintf(buf, sizeof(buf), "%s%s", prefix, package);
    }
    return buf;
  }
  return "";
}

static void write_twoterm_library_symbol(FILE *fp, const char *lib_id,
                                        const char *ref_prefix,
                                        const char *desc) {
  fprintf(fp,
          "\t\t(symbol \"%s\"\n"
          "\t\t\t(pin_numbers (hide yes))\n"
          "\t\t\t(pin_names (offset 0))\n"
          "\t\t\t(exclude_from_sim no)\n"
          "\t\t\t(in_bom yes)\n"
          "\t\t\t(on_board yes)\n"
          "\t\t\t(property \"Reference\" \"%s\" (at 2.032 0 90)\n"
          "\t\t\t\t(effects (font (size 1.27 1.27))))\n"
          "\t\t\t(property \"Value\" \"%s\" (at 0 0 90)\n"
          "\t\t\t\t(effects (font (size 1.27 1.27))))\n"
          "\t\t\t(property \"Footprint\" \"\" (at -1.778 0 90)\n"
          "\t\t\t\t(effects (font (size 1.27 1.27)) (hide yes)))\n"
          "\t\t\t(property \"Datasheet\" \"~\" (at 0 0 0)\n"
          "\t\t\t\t(effects (font (size 1.27 1.27)) (hide yes)))\n"
          "\t\t\t(property \"Description\" \"%s\" (at 0 0 0)\n"
          "\t\t\t\t(effects (font (size 1.27 1.27)) (hide yes)))\n"
          "\t\t\t(symbol \"%s_0_1\"\n"
          "\t\t\t\t(rectangle (start -1.016 -2.54) (end 1.016 2.54)\n"
          "\t\t\t\t\t(stroke (width 0.254) (type default)) (fill (type none))))\n"
          "\t\t\t(symbol \"%s_1_1\"\n"
          "\t\t\t\t(pin passive line (at 0 3.81 270) (length 1.27)\n"
          "\t\t\t\t\t(name \"~\" (effects (font (size 1.27 1.27))))\n"
          "\t\t\t\t\t(number \"1\" (effects (font (size 1.27 1.27)))))\n"
          "\t\t\t\t(pin passive line (at 0 -3.81 90) (length 1.27)\n"
          "\t\t\t\t\t(name \"~\" (effects (font (size 1.27 1.27))))\n"
          "\t\t\t\t\t(number \"2\" (effects (font (size 1.27 1.27)))))\n"
          "\t\t\t(embedded_fonts no)\n"
          "\t\t)\n",
          lib_id, ref_prefix, ref_prefix, desc, ref_prefix, ref_prefix);
}

static void write_resistor_library_symbol(FILE *fp) {
  write_twoterm_library_symbol(fp, "Device:R", "R", "Resistor");
}

static void write_placed_resistor(FILE *fp, const CompiledComponent *component,
                                  double x, double y, const char *uuid,
                                  const char *pin1_uuid,
                                  const char *pin2_uuid) {
  const char *footprint =
      kicad_footprint_for_package(component->part.package, component->part.type);
  const char *lib_id = kicad_lib_id(component->part.type);
  const char *desc = component->part.type == PART_CAPACITOR ? "Capacitor"
                     : component->part.type == PART_INDUCTOR  ? "Inductor"
                     : component->part.type == PART_DIODE     ? "LED"
                     : component->part.type == PART_TRANSISTOR ? "Transistor"
                     : component->part.type == PART_IC        ? "IC"
                     : component->part.type == PART_OTHER     ? "Battery"
                                                              : "Resistor";

  fprintf(fp,
          "\t(symbol\n"
          "\t\t(lib_id \"%s\")\n"
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
          "\t\t(property \"Description\" \"%s\"\n"
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
          lib_id, x, y, uuid, component->role, x + 2, y - 5, component->part.mpn,
          x + 2, y + 5, footprint, x - 2, y, x, y, desc, x, y, pin1_uuid,
          pin2_uuid, component->role);
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

static void write_global_label(FILE *fp, const char *text, double x, double y,
                               unsigned int id) {
  char uuid[64];
  make_uuid(uuid, sizeof(uuid), id);

  fprintf(fp,
          "\t(global_label \"%s\"\n"
          "\t\t(shape input)\n"
          "\t\t(at %.2f %.2f 0)\n"
          "\t\t(effects\n"
          "\t\t\t(font (size 1.27 1.27))\n"
          "\t\t\t(justify left)\n"
          "\t\t)\n"
          "\t\t(uuid \"%s\")\n"
          "\t)\n",
          text, x, y, uuid);
}

int compiler_write_kicad_sch(const char *filename,
                             const CompiledSchematic *schematic) {
  FILE *fp;
  int i;
  char uuid[64];
  char pin1_uuid[64];
  char pin2_uuid[64];

  if (!filename || !schematic)
    return 0;

  if (schematic->component_count <= 0)
    return 0;

  fp = fopen(filename, "w");
  if (!fp)
    return 0;

  fprintf(fp, "(kicad_sch\n"
              "\t(version 20231120)\n"
              "\t(generator \"eeschema\")\n"
              "\t(generator_version \"8.0\")\n"
              "\t(uuid \"00000000-0000-4000-8000-000000000001\")\n"
              "\t(paper \"A4\")\n"
              "\t(lib_symbols\n");

  write_resistor_library_symbol(fp);
  write_twoterm_library_symbol(fp, "Device:C", "C", "Capacitor");
  write_twoterm_library_symbol(fp, "Device:L", "L", "Inductor");
  write_twoterm_library_symbol(fp, "Device:LED", "D", "LED");
  write_twoterm_library_symbol(fp, "Device:Q_NPN_BCE", "Q", "NPN");
  write_twoterm_library_symbol(fp, "Device:LDO", "U", "Regulator");
  write_twoterm_library_symbol(fp, "Device:SW", "SW", "Switch");
  write_twoterm_library_symbol(fp, "Device:Battery", "BT", "Battery");
  write_twoterm_library_symbol(fp, "Device:OpAmp", "U", "OpAmp");
  fprintf(fp, "\t)\n");

  /*
   * Special-case the validated 2-resistor divider layout so g05 stays stable.
   * Otherwise place components on a vertical naive grid.
   */
  if (schematic->component_count == 2 &&
      strcmp(schematic->components[0].node1, "VIN") == 0 &&
      strcmp(schematic->components[0].node2, "VOUT") == 0 &&
      strcmp(schematic->components[1].node1, "VOUT") == 0 &&
      strcmp(schematic->components[1].node2, "GND") == 0) {
    const CompiledComponent *r1 = &schematic->components[0];
    const CompiledComponent *r2 = &schematic->components[1];

    write_placed_resistor(fp, r1, 100, 80,
                          "00000000-0000-4000-8000-000000000101",
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
  } else {
    /*
     * Naive grid: stub wires pin→global_label only.
     * Do NOT wire consecutive parts — that falsely shorted unrelated nets
     * and broke Gate4 composed ERC.
     */
    for (i = 0; i < schematic->component_count; i++) {
      double x = 100.0 + (i % 4) * 40.0;
      double y = 80.0 + (i / 4) * 40.0;
      make_uuid(uuid, sizeof(uuid), (unsigned)(100 + i));
      make_uuid(pin1_uuid, sizeof(pin1_uuid), (unsigned)(200 + i * 2));
      make_uuid(pin2_uuid, sizeof(pin2_uuid), (unsigned)(201 + i * 2));
      write_placed_resistor(fp, &schematic->components[i], x, y, uuid, pin1_uuid,
                            pin2_uuid);
      write_wire(fp, x, y - 3.81, x, y - 10.0, (unsigned)(400 + i * 2));
      write_wire(fp, x, y + 3.81, x, y + 10.0, (unsigned)(401 + i * 2));
      write_global_label(fp, schematic->components[i].node1, x, y - 10.0,
                         (unsigned)(300 + i * 2));
      write_global_label(fp, schematic->components[i].node2, x, y + 10.0,
                         (unsigned)(301 + i * 2));
    }
  }

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

/* =========================================================
 * Physics2 design IR + lowering
 * ========================================================= */

void compiler_physics_design_init(CompilerPhysDesign *design) {
  if (!design)
    return;

  design->elements = NULL;
  design->count = 0;
  design->capacity = 0;
}

void compiler_physics_design_free(CompilerPhysDesign *design) {
  if (!design)
    return;

  free(design->elements);
  design->elements = NULL;
  design->count = 0;
  design->capacity = 0;
}

bool compiler_physics_design_add(CompilerPhysDesign *design,
                                 CompilerPhysKind kind, const char *name,
                                 double value, double tolerance_pct,
                                 const char *const *terminals,
                                 uint8_t terminal_count) {
  CompilerPhysElement *elements;
  CompilerPhysElement *element;
  uint8_t i;
  size_t capacity;

  if (!design || !name || !terminals)
    return false;

  if (kind == COMPILER_PHYS_NONE)
    return false;

  if (terminal_count == 0 || terminal_count > PHYSICS2_MAX_TERMINALS)
    return false;

  if (!isfinite(value) || !isfinite(tolerance_pct) || tolerance_pct < 0.0)
    return false;

  switch (kind) {
  case COMPILER_PHYS_RESISTOR:
  case COMPILER_PHYS_CAPACITOR:
  case COMPILER_PHYS_INDUCTOR:
    if (value <= 0.0 || terminal_count != 2)
      return false;
    break;
  case COMPILER_PHYS_VSOURCE:
  case COMPILER_PHYS_ISOURCE:
    if (terminal_count != 2)
      return false;
    break;
  default:
    return false;
  }

  for (i = 0; i < terminal_count; i++) {
    if (!terminals[i] || terminals[i][0] == '\0')
      return false;
  }

  if (design->count == design->capacity) {
    capacity = design->capacity ? design->capacity * 2 : 8;

    if (capacity < design->capacity)
      return false;

    elements = realloc(design->elements, capacity * sizeof(*elements));

    if (!elements)
      return false;

    design->elements = elements;
    design->capacity = capacity;
  }

  element = &design->elements[design->count];
  memset(element, 0, sizeof(*element));

  element->kind = kind;
  strncpy(element->name, name, sizeof(element->name) - 1);
  element->name[sizeof(element->name) - 1] = '\0';
  element->value = value;
  element->tolerance_pct = tolerance_pct;
  element->terminal_count = terminal_count;

  for (i = 0; i < terminal_count; i++) {
    strncpy(element->terminals[i], terminals[i],
            sizeof(element->terminals[i]) - 1);
    element->terminals[i][sizeof(element->terminals[i]) - 1] = '\0';
  }

  design->count++;
  return true;
}

typedef struct {
  char name[64];
  NodeId id;
} CompilerNodeMapEntry;

typedef struct {
  CompilerNodeMapEntry *items;
  size_t count;
  size_t capacity;
} CompilerNodeMap;

static void compiler_node_map_init(CompilerNodeMap *map) {
  if (!map)
    return;

  map->items = NULL;
  map->count = 0;
  map->capacity = 0;
}

static void compiler_node_map_free(CompilerNodeMap *map) {
  if (!map)
    return;

  free(map->items);
  map->items = NULL;
  map->count = 0;
  map->capacity = 0;
}

static bool compiler_node_map_get_or_add(CompilerNodeMap *map,
                                         PhysicsProgram *program,
                                         const char *name, NodeId *out_id) {
  size_t i;
  CompilerNodeMapEntry *items;
  size_t capacity;
  NodeId id;

  if (!map || !program || !name || !out_id)
    return false;

  for (i = 0; i < map->count; i++) {
    if (strcmp(map->items[i].name, name) == 0) {
      *out_id = map->items[i].id;
      return true;
    }
  }

  id = physics2_program_new_node(program);

  if (id == PHYSICS2_NODE_NONE)
    return false;

  if (map->count == map->capacity) {
    capacity = map->capacity ? map->capacity * 2 : 8;

    if (capacity < map->capacity)
      return false;

    items = realloc(map->items, capacity * sizeof(*items));

    if (!items)
      return false;

    map->items = items;
    map->capacity = capacity;
  }

  strncpy(map->items[map->count].name, name,
          sizeof(map->items[map->count].name) - 1);
  map->items[map->count].name[sizeof(map->items[map->count].name) - 1] = '\0';
  map->items[map->count].id = id;
  map->count++;

  *out_id = id;
  return true;
}

static bool compiler_init_element_primitive(const CompilerPhysElement *element,
                                            PhysicsPrimitive *primitive) {
  if (!element || !primitive)
    return false;

  switch (element->kind) {
  case COMPILER_PHYS_RESISTOR:
    return physics2_primitive_init_resistor(primitive, element->name,
                                            element->value,
                                            element->tolerance_pct);
  case COMPILER_PHYS_CAPACITOR:
    return physics2_primitive_init_capacitor(primitive, element->name,
                                             element->value,
                                             element->tolerance_pct);
  case COMPILER_PHYS_INDUCTOR:
    return physics2_primitive_init_inductor(primitive, element->name,
                                            element->value,
                                            element->tolerance_pct);
  case COMPILER_PHYS_VSOURCE:
    return physics2_primitive_init_vsource(primitive, element->name,
                                           element->value,
                                           element->tolerance_pct);
  case COMPILER_PHYS_ISOURCE:
    return physics2_primitive_init_isource(primitive, element->name,
                                           element->value,
                                           element->tolerance_pct);
  default:
    return false;
  }
}

void compiler_free_physics_program(CompiledPhysicsProgram *compiled) {
  if (!compiled)
    return;

  physics2_program_free(&compiled->program);
  free(compiled->primitives);
  compiled->primitives = NULL;
  compiled->primitive_count = 0;
}

bool compiler_lower_to_physics2(const CompilerPhysDesign *design,
                                CompiledPhysicsProgram *out) {
  CompilerNodeMap nodes;
  size_t i;
  uint8_t t;
  NodeId terminal_ids[PHYSICS2_MAX_TERMINALS];

  if (!design || !out)
    return false;

  memset(out, 0, sizeof(*out));
  compiler_node_map_init(&nodes);
  physics2_program_init(&out->program);

  if (design->count == 0)
    goto fail;

  out->primitives = calloc(design->count, sizeof(*out->primitives));

  if (!out->primitives)
    goto fail;

  out->primitive_count = design->count;

  for (i = 0; i < design->count; i++) {
    const CompilerPhysElement *element = &design->elements[i];

    if (!compiler_init_element_primitive(element, &out->primitives[i]))
      goto fail;

    for (t = 0; t < element->terminal_count; t++) {
      if (!compiler_node_map_get_or_add(&nodes, &out->program,
                                        element->terminals[t],
                                        &terminal_ids[t]))
        goto fail;
    }

    if (physics2_program_add_primitive(&out->program, &out->primitives[i],
                                       terminal_ids,
                                       element->terminal_count) ==
        PHYSICS_PRIMITIVE_NONE)
      goto fail;
  }

  compiler_node_map_free(&nodes);
  return true;

fail:
  compiler_node_map_free(&nodes);
  compiler_free_physics_program(out);
  return false;
}
