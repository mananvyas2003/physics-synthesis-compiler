#include "schematic_load.h"

#include "cJSON.h"
#include "cli.h"
#include "diag_error.h"
#include "gemini_schematic.h"
#include "part_lib.h"
#include "unit_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path) {
  FILE *fp = fopen(path, "rb");
  long size;
  char *buf;
  if (!fp)
    return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);
  buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
    free(buf);
    fclose(fp);
    return NULL;
  }
  buf[size] = '\0';
  fclose(fp);
  return buf;
}

static int part_type_supported(const char *t) { return part_lib_supported(t); }

static int role_in_list(cJSON *components, const char *role) {
  cJSON *item;
  cJSON_ArrayForEach(item, components) {
    const char *r =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
    if (r && strcmp(r, role) == 0)
      return 1;
  }
  return 0;
}

static int node_in_list(cJSON *nodes, const char *node) {
  cJSON *item;
  cJSON_ArrayForEach(item, nodes) {
    if (cJSON_IsString(item) && item->valuestring &&
        strcmp(item->valuestring, node) == 0)
      return 1;
  }
  return 0;
}

static int validate_root(cJSON *root) {
  cJSON *name;
  cJSON *desc;
  cJSON *cat;
  cJSON *parts;
  cJSON *components;
  cJSON *nodes;
  cJSON *connections;
  cJSON *item;
  int has_gnd = 0;
  int has_power = 0;
  char roles_seen[64][64];
  int role_count = 0;

  diag_clear_error();
  if (!root) {
    diag_set_error("IR is not valid JSON.");
    return 1;
  }

  name = cJSON_GetObjectItemCaseSensitive(root, "name");
  desc = cJSON_GetObjectItemCaseSensitive(root, "description");
  cat = cJSON_GetObjectItemCaseSensitive(root, "category");
  parts = cJSON_GetObjectItemCaseSensitive(root, "parts");
  components = cJSON_GetObjectItemCaseSensitive(root, "components");
  nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
  connections = cJSON_GetObjectItemCaseSensitive(root, "connections");

  if (!cJSON_IsString(name) || name->valuestring[0] == '\0') {
    diag_set_error("IR missing required string field 'name'.");
    return 1;
  }
  if (!cJSON_IsString(desc) || !cJSON_IsString(cat)) {
    diag_set_error("IR missing 'description' or 'category'.");
    return 1;
  }
  if (!cJSON_IsArray(parts) || cJSON_GetArraySize(parts) < 1) {
    diag_set_error("IR 'parts' must be a non-empty array of catalogue entries.");
    return 1;
  }
  if (!cJSON_IsArray(components) || cJSON_GetArraySize(components) < 1) {
    diag_set_error("IR 'components' must be a non-empty array.");
    return 1;
  }
  if (!cJSON_IsArray(nodes) || cJSON_GetArraySize(nodes) < 1) {
    diag_set_error("IR 'nodes' must list at least one net name.");
    return 1;
  }
  if (!cJSON_IsArray(connections) || cJSON_GetArraySize(connections) < 1) {
    diag_set_error("IR 'connections' must be a non-empty array.");
    return 1;
  }

  cJSON_ArrayForEach(item, parts) {
    cJSON *mpn = cJSON_GetObjectItemCaseSensitive(item, "mpn");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
    cJSON *package = cJSON_GetObjectItemCaseSensitive(item, "package");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(item, "value");
    double parsed = 0.0;
    const char *ts;

    if (!cJSON_IsString(mpn) || !cJSON_IsString(type) ||
        !cJSON_IsString(package)) {
      diag_set_error(
          "Each parts[] entry needs string fields mpn, type, and package.");
      return 1;
    }
    ts = type->valuestring;
    if (!part_type_supported(ts)) {
      diag_set_error(
          "%s components are not currently available in the active library. "
          "Supported: resistor, capacitor, inductor, diode, led, transistor, "
          "bjt, mosfet, opamp, regulator, ldo, switch, battery, connector.",
          ts);
      return 1;
    }
    if (unit_parse_number_or_string(value, &parsed) != 0 || parsed <= 0.0) {
      diag_set_error(
          "Part '%s' has invalid value (need positive number or engineering "
          "string like 1k / 100nF / 2.0).",
          mpn->valuestring);
      return 1;
    }
  }

  cJSON_ArrayForEach(item, components) {
    cJSON *role = cJSON_GetObjectItemCaseSensitive(item, "role");
    cJSON *ptype = cJSON_GetObjectItemCaseSensitive(item, "part_type");
    cJSON *quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
    cJSON *tv = cJSON_GetObjectItemCaseSensitive(item, "target_value");
    const char *ts;
    int r;
    double parsed = 0.0;

    if (!cJSON_IsString(role) || !cJSON_IsString(ptype) ||
        !cJSON_IsNumber(quantity)) {
      diag_set_error(
          "Each components[] entry needs role (string), part_type (string), "
          "quantity (number).");
      return 1;
    }
    ts = ptype->valuestring;
    if (!part_type_supported(ts)) {
      diag_set_error(
          "%s components are not currently available in the active library. "
          "Supported: resistor, capacitor, inductor, diode, led, transistor, "
          "bjt, mosfet, opamp, regulator, ldo, switch, battery, connector.",
          ts);
      return 1;
    }
    for (r = 0; r < role_count; r++) {
      if (strcmp(roles_seen[r], role->valuestring) == 0) {
        diag_set_error("Duplicate component role '%s'.", role->valuestring);
        return 1;
      }
    }
    if (role_count < 64) {
      strncpy(roles_seen[role_count], role->valuestring,
              sizeof(roles_seen[0]) - 1);
      role_count++;
    }
    if (tv && unit_parse_number_or_string(tv, &parsed) != 0) {
      diag_set_error(
          "Component '%s' has invalid target_value (use SI number or "
          "engineering string).",
          role->valuestring);
      return 1;
    }
  }

  cJSON_ArrayForEach(item, nodes) {
    if (!cJSON_IsString(item) || !item->valuestring[0]) {
      diag_set_error("Each nodes[] entry must be a non-empty string.");
      return 1;
    }
    if (strcmp(item->valuestring, "GND") == 0)
      has_gnd = 1;
    if (strcmp(item->valuestring, "VIN") == 0 ||
        strcmp(item->valuestring, "VBUS") == 0 ||
        strcmp(item->valuestring, "VCC") == 0 ||
        strcmp(item->valuestring, "3V3") == 0 ||
        strcmp(item->valuestring, "5V") == 0)
      has_power = 1;
  }

  cJSON_ArrayForEach(item, connections) {
    const char *role =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
    const char *pin =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "pin"));
    const char *node =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "node"));
    const char *ptype = NULL;
    cJSON *comp;
    char npin[32];

    if (!role || !pin || !node) {
      diag_set_error(
          "Each connections[] entry needs string fields role, pin, node.");
      return 1;
    }
    if (!role_in_list(components, role)) {
      diag_set_error(
          "Connection references unknown role '%s'. Add it to components[] "
          "or fix the connection.",
          role);
      return 1;
    }
    if (!node_in_list(nodes, node)) {
      diag_set_error(
          "Connection role '%s' pin '%s' references unknown node '%s'. Add "
          "it to nodes[] or fix the connection.",
          role, pin, node);
      return 1;
    }
    cJSON_ArrayForEach(comp, components) {
      const char *r =
          cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(comp, "role"));
      if (r && strcmp(r, role) == 0) {
        ptype = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(comp, "part_type"));
        break;
      }
    }
    if (part_lib_normalize_pin(ptype, pin, npin, sizeof(npin)) != 0) {
      diag_set_error(
          "Role '%s' (%s) uses unsupported pin '%s'.", role,
          ptype ? ptype : "?", pin);
      return 1;
    }
  }

  /* Unconnected-pin check against part library required pins */
  {
    int ci;
    for (ci = 0; ci < role_count; ci++) {
      const char *ptype = NULL;
      const char *req[PART_LIB_MAX_PINS];
      int nreq;
      int pi;
      cJSON *comp;
      cJSON_ArrayForEach(comp, components) {
        const char *r = cJSON_GetStringValue(
            cJSON_GetObjectItemCaseSensitive(comp, "role"));
        if (r && strcmp(r, roles_seen[ci]) == 0) {
          ptype = cJSON_GetStringValue(
              cJSON_GetObjectItemCaseSensitive(comp, "part_type"));
          break;
        }
      }
      nreq = part_lib_required_pins(ptype, req, PART_LIB_MAX_PINS);
      if (nreq < 0) {
        diag_set_error("Component '%s' has unknown part_type.", roles_seen[ci]);
        return 1;
      }
      for (pi = 0; pi < nreq; pi++) {
        int found = 0;
        cJSON_ArrayForEach(item, connections) {
          const char *role = cJSON_GetStringValue(
              cJSON_GetObjectItemCaseSensitive(item, "role"));
          const char *pin = cJSON_GetStringValue(
              cJSON_GetObjectItemCaseSensitive(item, "pin"));
          char npin[32];
          if (!role || !pin || strcmp(role, roles_seen[ci]) != 0)
            continue;
          if (part_lib_normalize_pin(ptype, pin, npin, sizeof(npin)) == 0 &&
              strcmp(npin, req[pi]) == 0)
            found = 1;
        }
        if (!found) {
          diag_set_error(
              "Component '%s' (%s) is missing required pin '%s'.",
              roles_seen[ci], ptype ? ptype : "?", req[pi]);
          return 1;
        }
      }
    }
  }

  if (!has_gnd) {
    diag_set_error(
        "Design has no GND node. Add a ground reference net named GND.");
    return 1;
  }
  if (!has_power) {
    diag_set_error(
        "Design has no power reference (expected one of VIN, VBUS, VCC, 3V3, "
        "5V).");
    return 1;
  }

  /* Optional nets[] IR (net-centric view); must agree with connections. */
  {
    cJSON *nets = cJSON_GetObjectItemCaseSensitive(root, "nets");
    if (cJSON_IsArray(nets)) {
      cJSON *net;
      cJSON_ArrayForEach(net, nets) {
        const char *nname =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(net, "name"));
        cJSON *conns =
            cJSON_GetObjectItemCaseSensitive(net, "connections");
        cJSON *cref;
        if (!nname || !cJSON_IsArray(conns)) {
          diag_set_error(
              "Each nets[] entry needs string 'name' and array 'connections'.");
          return 1;
        }
        if (!node_in_list(nodes, nname)) {
          diag_set_error(
              "nets[] name '%s' is not listed in nodes[].", nname);
          return 1;
        }
        cJSON_ArrayForEach(cref, conns) {
          const char *ref = cJSON_IsString(cref) ? cref->valuestring : NULL;
          char role[64];
          char pin[32];
          const char *dot;
          int matched = 0;
          if (!ref || !(dot = strchr(ref, '.'))) {
            diag_set_error(
                "nets[] connection '%s' must look like ROLE.PIN.",
                ref ? ref : "?");
            return 1;
          }
          if ((size_t)(dot - ref) >= sizeof(role))
            return 1;
          memcpy(role, ref, (size_t)(dot - ref));
          role[dot - ref] = '\0';
          strncpy(pin, dot + 1, sizeof(pin) - 1);
          pin[sizeof(pin) - 1] = '\0';
          cJSON_ArrayForEach(item, connections) {
            const char *r = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(item, "role"));
            const char *p = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(item, "pin"));
            const char *n = cJSON_GetStringValue(
                cJSON_GetObjectItemCaseSensitive(item, "node"));
            char np[32];
            const char *ptype = NULL;
            cJSON *comp;
            if (!r || !p || !n || strcmp(r, role) != 0 || strcmp(n, nname) != 0)
              continue;
            cJSON_ArrayForEach(comp, components) {
              const char *rr = cJSON_GetStringValue(
                  cJSON_GetObjectItemCaseSensitive(comp, "role"));
              if (rr && strcmp(rr, r) == 0) {
                ptype = cJSON_GetStringValue(
                    cJSON_GetObjectItemCaseSensitive(comp, "part_type"));
                break;
              }
            }
            if (part_lib_normalize_pin(ptype, p, np, sizeof(np)) == 0 &&
                (strcmp(np, pin) == 0 || strcmp(p, pin) == 0))
              matched = 1;
          }
          if (!matched) {
            diag_set_error(
                "nets[] claims %s on net %s but connections[] disagrees.",
                ref, nname);
            return 1;
          }
        }
      }
    }
  }

  /* Shorted power rails: any 2-pin part bridging two power nets with ~0 ohm. */
  cJSON_ArrayForEach(item, components) {
    const char *role =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
    const char *ptype = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(item, "part_type"));
    cJSON *tv = cJSON_GetObjectItemCaseSensitive(item, "target_value");
    double val = 0.0;
    char n1[64] = {0};
    char n2[64] = {0};
    cJSON *conn;
    int got = 0;
    if (!role || !ptype)
      continue;
    if (strcmp(ptype, "resistor") != 0 && strcmp(ptype, "switch") != 0 &&
        strcmp(ptype, "connector") != 0)
      continue;
    cJSON_ArrayForEach(conn, connections) {
      const char *r =
          cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(conn, "role"));
      const char *n =
          cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(conn, "node"));
      if (!r || !n || strcmp(r, role) != 0)
        continue;
      if (got == 0) {
        strncpy(n1, n, sizeof(n1) - 1);
        got = 1;
      } else if (got == 1) {
        strncpy(n2, n, sizeof(n2) - 1);
        got = 2;
      }
    }
    if (got == 2) {
      int p1 = (strcmp(n1, "VIN") == 0 || strcmp(n1, "VBUS") == 0 ||
                strcmp(n1, "5V") == 0 || strcmp(n1, "3V3") == 0 ||
                strcmp(n1, "VCC") == 0);
      int p2 = (strcmp(n2, "VIN") == 0 || strcmp(n2, "VBUS") == 0 ||
                strcmp(n2, "5V") == 0 || strcmp(n2, "3V3") == 0 ||
                strcmp(n2, "VCC") == 0);
      int g1 = strcmp(n1, "GND") == 0;
      int g2 = strcmp(n2, "GND") == 0;
      (void)unit_parse_number_or_string(tv, &val);
      if (p1 && p2 && strcmp(n1, n2) != 0) {
        diag_set_error(
            "Conflicting power nets: '%s' bridges %s and %s.", role, n1, n2);
        return 1;
      }
      if (((p1 && g2) || (p2 && g1)) &&
          (strcmp(ptype, "switch") == 0 || strcmp(ptype, "connector") == 0 ||
           (strcmp(ptype, "resistor") == 0 && val > 0.0 && val < 0.1))) {
        diag_set_error(
            "Shorted power rail risk: '%s' (%s) bridges %s and %s.", role,
            ptype, n1, n2);
        return 1;
      }
    }
  }

  return 0;
}

int schematic_ir_validate_file(const char *path) {
  char *text;
  cJSON *root;
  int rc;

  text = read_all(path);
  if (!text) {
    diag_set_error("Cannot read IR file '%s'.", path ? path : "(null)");
    return 1;
  }
  root = cJSON_Parse(text);
  free(text);
  if (!root) {
    diag_set_error("IR file '%s' is not valid JSON.", path);
    return 1;
  }
  rc = validate_root(root);
  cJSON_Delete(root);
  return rc;
}

int schematic_ir_load_and_validate(const char *path, SchematicIrMeta *out) {
  char *text;
  cJSON *root;
  cJSON *name;

  if (!path || !out)
    return 1;
  memset(out, 0, sizeof(*out));

  text = read_all(path);
  if (!text) {
    diag_set_error("Cannot read IR file '%s'.", path);
    return 1;
  }
  root = cJSON_Parse(text);
  free(text);
  if (!root) {
    diag_set_error("IR file '%s' is not valid JSON.", path);
    return 1;
  }

  if (validate_root(root) != 0) {
    cJSON_Delete(root);
    snprintf(out->clarifying_question, sizeof(out->clarifying_question), "%s",
             diag_last_error()[0] ? diag_last_error()
                                  : "Schematic IR invalid.");
    return 1;
  }

  name = cJSON_GetObjectItemCaseSensitive(root, "name");
  strncpy(out->name, name->valuestring, sizeof(out->name) - 1);
  cJSON_Delete(root);
  return 0;
}

static int prompt_id_from_path(const char *prompt_path, char *num,
                               size_t num_len) {
  const char *slash;
  const char *base;
  size_t i;
  size_t n = 0;

  if (!prompt_path || !num || num_len == 0)
    return 1;
  slash = strrchr(prompt_path, '/');
  if (!slash)
    slash = strrchr(prompt_path, '\\');
  base = slash ? slash + 1 : prompt_path;
  for (i = 0; base[i] && base[i] != '.' && n + 1 < num_len; i++)
    num[n++] = base[i];
  num[n] = '\0';
  return n == 0 ? 1 : 0;
}

static int offline_from_prompt(const char *prompt_path, char *out_ir_path,
                               size_t out_len, SchematicIrMeta *meta) {
  char num[16];
  char *joined;
  int attempt;

  if (prompt_id_from_path(prompt_path, num, sizeof(num)) != 0) {
    snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
             "Prompt path missing id; use fixtures/prompts/NNN.txt or set "
             "GEMINI_API_KEY for live mode");
    return 1;
  }

  joined = cli_join_path(cli_fixture_root(), "fixtures/schematics/");
  if (!joined)
    return 1;
  snprintf(out_ir_path, out_len, "%s%s.json", joined, num);
  free(joined);

  for (attempt = 0; attempt < 3; attempt++) {
    if (schematic_ir_load_and_validate(out_ir_path, meta) == 0)
      return 0;
  }

  snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
           "%s",
           diag_last_error()[0]
               ? diag_last_error()
               : "No valid schematic IR for prompt after 3 attempts.");
  return 1;
}

int schematic_provider_from_prompt(const char *prompt_path,
                                   const char *prompt_text_override,
                                   const char *preferred_out_path,
                                   int force_offline, char *out_ir_path,
                                   size_t out_len, SchematicIrMeta *meta) {
  char *prompt_text = NULL;
  int rc = 1;

  if (!out_ir_path || out_len == 0 || !meta)
    return 1;
  memset(meta, 0, sizeof(*meta));
  out_ir_path[0] = '\0';

  if (!force_offline && gemini_api_key_present()) {
    if (prompt_text_override && prompt_text_override[0]) {
      prompt_text = malloc(strlen(prompt_text_override) + 1);
      if (prompt_text)
        memcpy(prompt_text, prompt_text_override,
               strlen(prompt_text_override) + 1);
    } else if (prompt_path) {
      prompt_text = read_all(prompt_path);
    }
    if (!prompt_text) {
      snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
               "Cannot read prompt text for Gemini");
      return 1;
    }
    if (!preferred_out_path) {
      snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
               "Live Gemini requires an output IR path");
      free(prompt_text);
      return 1;
    }
    strncpy(out_ir_path, preferred_out_path, out_len - 1);
    out_ir_path[out_len - 1] = '\0';
    rc = gemini_schematic_from_prompt(prompt_text, out_ir_path, meta);
    free(prompt_text);
    return rc;
  }

  if (!prompt_path) {
    snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
             "Offline mode needs --prompt fixtures/prompts/NNN.txt "
             "(or set GEMINI_API_KEY)");
    return 1;
  }
  return offline_from_prompt(prompt_path, out_ir_path, out_len, meta);
}
