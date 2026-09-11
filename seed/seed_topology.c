#include "seed_topology.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PartTypes part_type_from_string(const char *text) {
  if (!text)
    return PART_OTHER;

  if (strcmp(text, "resistor") == 0)
    return PART_RESISTOR;
  if (strcmp(text, "capacitor") == 0)
    return PART_CAPACITOR;
  if (strcmp(text, "inductor") == 0)
    return PART_INDUCTOR;
  if (strcmp(text, "diode") == 0)
    return PART_DIODE;
  if (strcmp(text, "transistor") == 0)
    return PART_TRANSISTOR;

  return PART_OTHER;
}

static char *read_entire_file(const char *path, size_t *out_size) {
  FILE *fp;
  long size;
  char *buffer;

  if (out_size)
    *out_size = 0;

  fp = fopen(path, "rb");
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

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  buffer = malloc((size_t)size + 1u);
  if (!buffer) {
    fclose(fp);
    return NULL;
  }

  if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
    free(buffer);
    fclose(fp);
    return NULL;
  }

  buffer[size] = '\0';
  fclose(fp);

  if (out_size)
    *out_size = (size_t)size;

  return buffer;
}

static int64_t find_component_id(const TopologyComponentRow *components,
                                 int count, const char *role) {
  int i;

  for (i = 0; i < count; i++) {
    if (strcmp(components[i].role_name, role) == 0)
      return components[i].id;
  }

  return 0;
}

static int64_t find_node_id(const TopologyNodeRow *nodes, int count,
                            const char *name) {
  int i;

  for (i = 0; i < count; i++) {
    if (strcmp(nodes[i].node_name, name) == 0)
      return nodes[i].id;
  }

  return 0;
}

int seed_load_topology_json(DB *db, const char *json_path) {
  char *text;
  cJSON *root;
  cJSON *parts;
  cJSON *components;
  cJSON *nodes;
  cJSON *connections;
  cJSON *item;
  const char *name;
  const char *description;
  const char *category;
  int64_t topology_id = 0;
  DBResult result;
  TopologyComponentRow component_rows[32];
  TopologyNodeRow node_rows[32];
  TopologyConnectionRow connection_rows[64];
  int component_count = 0;
  int node_count = 0;
  int connection_count = 0;

  if (!db || !json_path)
    return 1;

  text = read_entire_file(json_path, NULL);
  if (!text) {
    fprintf(stderr, "[SEED] Failed to read %s\n", json_path);
    return 1;
  }

  root = cJSON_Parse(text);
  free(text);

  if (!root) {
    fprintf(stderr, "[SEED] Invalid JSON in %s\n", json_path);
    return 1;
  }

  name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "name"));
  description =
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "description"));
  category =
      cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "category"));

  if (!name || !description || !category) {
    cJSON_Delete(root);
    fprintf(stderr, "[SEED] Missing name/description/category\n");
    return 1;
  }

  parts = cJSON_GetObjectItemCaseSensitive(root, "parts");
  if (cJSON_IsArray(parts)) {
    cJSON_ArrayForEach(item, parts) {
      DBPart part;
      int64_t part_id = 0;
      const char *mpn;
      const char *type;
      const char *package;
      cJSON *value;

      memset(&part, 0, sizeof(part));

      mpn = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "mpn"));
      type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "type"));
      package =
          cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "package"));
      value = cJSON_GetObjectItemCaseSensitive(item, "value");

      if (!mpn || !type || !package || !cJSON_IsNumber(value)) {
        cJSON_Delete(root);
        fprintf(stderr, "[SEED] Invalid parts entry\n");
        return 1;
      }

      strncpy(part.mpn, mpn, sizeof(part.mpn) - 1);
      strncpy(part.package, package, sizeof(part.package) - 1);
      part.type = part_type_from_string(type);
      part.value = value->valuedouble;
      part.tolerance_class = TOLERANCE_E96;
      {
        cJSON *vr = cJSON_GetObjectItemCaseSensitive(item, "v_rating");
        cJSON *pr = cJSON_GetObjectItemCaseSensitive(item, "power_rating_w");
        if (cJSON_IsNumber(vr))
          part.v_rating = vr->valuedouble;
        if (cJSON_IsNumber(pr))
          part.power_rating_w = pr->valuedouble;
      }

      result = DB_InsertPartFull(db, &part, &part_id);
      if (result != DB_OK && result != DB_DUPLICATE) {
        cJSON_Delete(root);
        fprintf(stderr, "[SEED] Failed to insert part %s\n", mpn);
        return 1;
      }
    }
  }

  result = DB_InsertTopology(db, name, description, category, &topology_id);
  if (result != DB_OK && result != DB_DUPLICATE) {
    cJSON_Delete(root);
    fprintf(stderr, "[SEED] Failed to insert topology %s\n", name);
    return 1;
  }

  result = DB_GetTopology(db, name, component_rows, 32, &component_count,
                          node_rows, 32, &node_count, connection_rows, 64,
                          &connection_count);
  if (result == DB_OK && component_count >= 2 && connection_count >= 4) {
    cJSON_Delete(root);
    return 0;
  }

  if (topology_id <= 0) {
    /* Duplicate topology without usable id: re-fetch via GetTopology path. */
    cJSON_Delete(root);
    return 0;
  }

  components = cJSON_GetObjectItemCaseSensitive(root, "components");
  if (!cJSON_IsArray(components)) {
    cJSON_Delete(root);
    fprintf(stderr, "[SEED] Missing components array\n");
    return 1;
  }

  cJSON_ArrayForEach(item, components) {
    const char *role =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
    const char *ptype = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(item, "part_type"));
    cJSON *quantity = cJSON_GetObjectItemCaseSensitive(item, "quantity");
    int64_t component_id = 0;

    if (!role || !ptype || !cJSON_IsNumber(quantity)) {
      cJSON_Delete(root);
      return 1;
    }

    result = DB_AddTopologyComponent(db, topology_id, role,
                                     part_type_from_string(ptype),
                                     quantity->valueint, &component_id);
    if (result != DB_OK && result != DB_DUPLICATE) {
      cJSON_Delete(root);
      return 1;
    }
  }

  nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
  if (!cJSON_IsArray(nodes)) {
    cJSON_Delete(root);
    return 1;
  }

  cJSON_ArrayForEach(item, nodes) {
    const char *node_name = cJSON_GetStringValue(item);
    int64_t node_id = 0;

    if (!node_name) {
      cJSON_Delete(root);
      return 1;
    }

    result = DB_AddTopologyNode(db, topology_id, node_name, &node_id);
    if (result != DB_OK && result != DB_DUPLICATE) {
      cJSON_Delete(root);
      return 1;
    }
  }

  result = DB_GetTopology(db, name, component_rows, 32, &component_count,
                          node_rows, 32, &node_count, connection_rows, 64,
                          &connection_count);
  if (result != DB_OK) {
    cJSON_Delete(root);
    return 1;
  }

  connections = cJSON_GetObjectItemCaseSensitive(root, "connections");
  if (!cJSON_IsArray(connections)) {
    cJSON_Delete(root);
    return 1;
  }

  cJSON_ArrayForEach(item, connections) {
    const char *role =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
    const char *pin =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "pin"));
    const char *node =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "node"));
    int64_t component_id;
    int64_t node_id;

    if (!role || !pin || !node) {
      cJSON_Delete(root);
      return 1;
    }

    component_id = find_component_id(component_rows, component_count, role);
    node_id = find_node_id(node_rows, node_count, node);

    if (component_id <= 0 || node_id <= 0) {
      cJSON_Delete(root);
      fprintf(stderr, "[SEED] Unknown role/node in connection\n");
      return 1;
    }

    result = DB_AddTopologyConnection(db, component_id, pin, node_id, NULL);
    if (result != DB_OK && result != DB_DUPLICATE) {
      cJSON_Delete(root);
      return 1;
    }
  }

  cJSON_Delete(root);
  return 0;
}
