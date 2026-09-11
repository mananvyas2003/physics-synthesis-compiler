#include "emit.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int emit_design_snapshot_v1(const char *path,
                            const CompiledSchematic *schematic) {
  cJSON *root;
  cJSON *components;
  cJSON *nets;
  char *printed;
  FILE *fp;
  int i;
  char net_names[64][64];
  int net_count = 0;

  if (!path || !schematic)
    return 0;

  root = cJSON_CreateObject();
  if (!root)
    return 0;

  cJSON_AddStringToObject(root, "schema", "design-snapshot.v1");
  cJSON_AddStringToObject(root, "generator", "physics-synthesis-compiler");
  cJSON_AddStringToObject(root, "generator_version", "0.1.0");
  cJSON_AddStringToObject(root, "topology", schematic->name);

  components = cJSON_AddArrayToObject(root, "components");
  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "role", c->role);
    cJSON_AddStringToObject(item, "refdes", c->role);
    cJSON_AddStringToObject(item, "mpn", c->part.mpn);
    cJSON_AddNumberToObject(item, "value", c->part.value);
    cJSON_AddStringToObject(item, "package", c->part.package);
    cJSON_AddNumberToObject(item, "qty", 1);
    cJSON_AddItemToArray(components, item);
  }

  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    int n;
    int found1 = 0;
    int found2 = 0;
    for (n = 0; n < net_count; n++) {
      if (strcmp(net_names[n], c->node1) == 0)
        found1 = 1;
      if (strcmp(net_names[n], c->node2) == 0)
        found2 = 1;
    }
    if (!found1 && net_count < 64) {
      strncpy(net_names[net_count], c->node1, sizeof(net_names[0]) - 1);
      net_count++;
    }
    if (!found2 && net_count < 64) {
      strncpy(net_names[net_count], c->node2, sizeof(net_names[0]) - 1);
      net_count++;
    }
  }

  nets = cJSON_AddArrayToObject(root, "nets");
  for (i = 0; i < net_count; i++) {
    cJSON *net = cJSON_CreateObject();
    cJSON *pins = cJSON_CreateArray();
    int cidx;
    cJSON_AddStringToObject(net, "name", net_names[i]);
    for (cidx = 0; cidx < schematic->component_count; cidx++) {
      const CompiledComponent *c = &schematic->components[cidx];
      char pinbuf[96];
      if (strcmp(c->node1, net_names[i]) == 0) {
        snprintf(pinbuf, sizeof(pinbuf), "%s.%s", c->role, c->pin1);
        cJSON_AddItemToArray(pins, cJSON_CreateString(pinbuf));
      }
      if (strcmp(c->node2, net_names[i]) == 0) {
        snprintf(pinbuf, sizeof(pinbuf), "%s.%s", c->role, c->pin2);
        cJSON_AddItemToArray(pins, cJSON_CreateString(pinbuf));
      }
    }
    cJSON_AddItemToObject(net, "pins", pins);
    cJSON_AddItemToArray(nets, net);
  }

  printed = cJSON_Print(root);
  cJSON_Delete(root);
  if (!printed)
    return 0;

  fp = fopen(path, "wb");
  if (!fp) {
    free(printed);
    return 0;
  }

  fputs(printed, fp);
  fputc('\n', fp);
  fclose(fp);
  free(printed);
  return 1;
}

static char *read_all(const char *path) {
  FILE *fp;
  long size;
  char *buf;

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

int emit_snapshot_validate_file(const char *path) {
  char *text;
  cJSON *root;
  cJSON *schema;
  cJSON *components;
  cJSON *nets;
  int ok = 0;

  text = read_all(path);
  if (!text)
    return 0;

  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 0;

  schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  components = cJSON_GetObjectItemCaseSensitive(root, "components");
  nets = cJSON_GetObjectItemCaseSensitive(root, "nets");

  if (cJSON_IsString(schema) &&
      strcmp(schema->valuestring, "design-snapshot.v1") == 0 &&
      cJSON_IsArray(components) && cJSON_IsArray(nets) &&
      cJSON_GetObjectItemCaseSensitive(root, "topology") &&
      cJSON_GetObjectItemCaseSensitive(root, "generator"))
    ok = 1;

  cJSON_Delete(root);
  return ok;
}
