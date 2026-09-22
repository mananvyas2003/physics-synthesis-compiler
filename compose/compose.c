#include "compose.h"

#include "cJSON.h"
#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *join_block_path(const char *id) {
  char rel[128];
  snprintf(rel, sizeof(rel), "fixtures/blocks/%s.json", id);
  return cli_join_path(cli_fixture_root(), rel);
}

static char *read_all(const char *path) {
  FILE *fp = fopen(path, "rb");
  long size;
  char *buf;
  if (!fp)
    return NULL;
  fseek(fp, 0, SEEK_END);
  size = ftell(fp);
  rewind(fp);
  if (size < 0) {
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

static DFMElectricalLimits limits_from_json(cJSON *obj) {
  DFMElectricalLimits lim;
  cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, "voltage");
  cJSON *c = cJSON_GetObjectItemCaseSensitive(obj, "current");
  cJSON *z = cJSON_GetObjectItemCaseSensitive(obj, "impedance_ohms");
  memset(&lim, 0, sizeof(lim));
  if (cJSON_IsObject(v)) {
    cJSON *mn = cJSON_GetObjectItemCaseSensitive(v, "min");
    cJSON *mx = cJSON_GetObjectItemCaseSensitive(v, "max");
    if (cJSON_IsNumber(mn))
      lim.voltage.min = mn->valuedouble;
    if (cJSON_IsNumber(mx))
      lim.voltage.max = mx->valuedouble;
  }
  if (cJSON_IsObject(c)) {
    cJSON *mn = cJSON_GetObjectItemCaseSensitive(c, "min");
    cJSON *mx = cJSON_GetObjectItemCaseSensitive(c, "max");
    if (cJSON_IsNumber(mn))
      lim.current.min = mn->valuedouble;
    if (cJSON_IsNumber(mx))
      lim.current.max = mx->valuedouble;
  }
  if (cJSON_IsNumber(z))
    lim.impedance_ohms = z->valuedouble;
  return lim;
}

static DFMPortKind kind_from_string(const char *s) {
  if (!s)
    return DFM_PORT_NONE;
  if (strcmp(s, "power_in") == 0)
    return DFM_PORT_POWER_IN;
  if (strcmp(s, "power_out") == 0)
    return DFM_PORT_POWER_OUT;
  if (strcmp(s, "signal_in") == 0)
    return DFM_PORT_SIGNAL_IN;
  if (strcmp(s, "signal_out") == 0)
    return DFM_PORT_SIGNAL_OUT;
  if (strcmp(s, "gnd") == 0)
    return DFM_PORT_GND;
  if (strcmp(s, "i2c") == 0)
    return DFM_PORT_I2C;
  return DFM_PORT_NONE;
}

int compose_from_block_ids(const char *const *block_ids, int count,
                           ComposeResult *out) {
  DFMBlockPool pool;
  DFMComposition composition;
  DFMBlockId block_dfm_ids[16];
  DFMInstanceId instances[16];
  int i;

  if (!block_ids || count <= 0 || !out || count > 16)
    return 1;

  memset(out, 0, sizeof(*out));
  DFM_Init(&pool);
  DFM_CompositionInit(&composition);

  for (i = 0; i < count; i++) {
    char *path = join_block_path(block_ids[i]);
    char *text;
    cJSON *root;
    cJSON *ports;
    cJSON *p;
    DFMBlockId bid;

    if (!path)
      goto fail;
    text = read_all(path);
    if (!text) {
      free(path);
      goto fail;
    }
    root = cJSON_Parse(text);
    free(text);
    if (!root) {
      free(path);
      goto fail;
    }

    strncpy(out->blocks[out->block_count].id, block_ids[i],
            sizeof(out->blocks[0].id) - 1);
    strncpy(out->blocks[out->block_count].path, path,
            sizeof(out->blocks[0].path) - 1);
    out->block_count++;
    free(path);

    bid = DFM_AddBlock(&pool, block_ids[i]);
    block_dfm_ids[i] = bid;
    ports = cJSON_GetObjectItemCaseSensitive(root, "ports");
    if (cJSON_IsArray(ports)) {
      cJSON_ArrayForEach(p, ports) {
        const char *pname =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "name"));
        const char *pk =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p, "kind"));
        cJSON *el = cJSON_GetObjectItemCaseSensitive(p, "electrical");
        if (pname && pk && cJSON_IsObject(el))
          DFM_AddPort(&pool, bid, pname, kind_from_string(pk),
                      limits_from_json(el));
      }
    }
    cJSON_Delete(root);

    instances[i] = DFM_AddInstance(&composition, bid, block_ids[i]);
    if (!instances[i])
      goto fail;
  }

  /* Fan-out: any earlier power_out may feed a later power_in when compatible. */
  for (i = 0; i < count; i++) {
    const DFMBlock *a = DFM_GetBlock(&pool, block_dfm_ids[i]);
    int jblk;
    size_t pa, pb;
    if (!a)
      continue;
    for (pa = 0; pa < a->port_count; pa++) {
      if (a->ports[pa].kind != DFM_PORT_POWER_OUT)
        continue;
      for (jblk = i + 1; jblk < count; jblk++) {
        const DFMBlock *b = DFM_GetBlock(&pool, block_dfm_ids[jblk]);
        if (!b)
          continue;
        for (pb = 0; pb < b->port_count; pb++) {
          if (b->ports[pb].kind != DFM_PORT_POWER_IN)
            continue;
          if (DFM_PortsCompatible(&a->ports[pa], &b->ports[pb])) {
            DFM_Connect(&composition, instances[i], a->ports[pa].id,
                        instances[jblk], b->ports[pb].id);
          }
        }
      }
    }
  }

  /* Bidirectional I2C links between earlier and later blocks. */
  for (i = 0; i < count; i++) {
    const DFMBlock *a = DFM_GetBlock(&pool, block_dfm_ids[i]);
    int jblk;
    size_t pa, pb;
    if (!a)
      continue;
    for (pa = 0; pa < a->port_count; pa++) {
      if (a->ports[pa].kind != DFM_PORT_I2C)
        continue;
      for (jblk = i + 1; jblk < count; jblk++) {
        const DFMBlock *b = DFM_GetBlock(&pool, block_dfm_ids[jblk]);
        if (!b)
          continue;
        for (pb = 0; pb < b->port_count; pb++) {
          if (b->ports[pb].kind != DFM_PORT_I2C)
            continue;
          if (DFM_PortsCompatible(&a->ports[pa], &b->ports[pb])) {
            DFM_Connect(&composition, instances[i], a->ports[pa].id,
                        instances[jblk], b->ports[pb].id);
          }
        }
      }
    }
  }

  out->composition_ok = DFM_ValidateComposition(&pool, &composition) ? 1 : 0;
  DFM_CompositionFree(&composition);
  DFM_Free(&pool);
  return out->composition_ok ? 0 : 1;

fail:
  DFM_CompositionFree(&composition);
  DFM_Free(&pool);
  return 1;
}

int compose_gate4_scenario(ComposeResult *out) {
  const char *ids[] = {"usb_c_sink", "ldo_5v", "mcu_min_system",
                       "i2c_temp_sensor", "status_led"};
  return compose_from_block_ids(ids, 5, out);
}

int compose_industrial_sensor(ComposeResult *out) {
  const char *ids[] = {"battery_input", "bat_ldo_3v3", "mcu_min_system",
                       "i2c_temp_sensor", "status_led", "decouple_100n"};
  return compose_from_block_ids(ids, 6, out);
}

int compose_power_tree_12v(ComposeResult *out) {
  const char *ids[] = {"vin_12v", "buck_5v_stub", "ldo_5v", "mcu_min_system",
                       "status_led"};
  return compose_from_block_ids(ids, 5, out);
}

int compose_scenario(const char *name, ComposeResult *out) {
  if (!name || !out)
    return 1;
  if (strcmp(name, "gate4") == 0 || strcmp(name, "usb_iot") == 0)
    return compose_gate4_scenario(out);
  if (strcmp(name, "industrial_sensor") == 0)
    return compose_industrial_sensor(out);
  if (strcmp(name, "power_tree_12v") == 0)
    return compose_power_tree_12v(out);
  return 1;
}

static int node_in_array(cJSON *nodes, const char *name) {
  cJSON *item;
  cJSON_ArrayForEach(item, nodes) {
    if (cJSON_IsString(item) && strcmp(item->valuestring, name) == 0)
      return 1;
  }
  return 0;
}

static int part_mpn_in_array(cJSON *parts, const char *mpn) {
  cJSON *item;
  cJSON_ArrayForEach(item, parts) {
    const char *m =
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "mpn"));
    if (m && strcmp(m, mpn) == 0)
      return 1;
  }
  return 0;
}

int compose_expand_to_schematic(const ComposeResult *composition,
                                const char *out_path) {
  cJSON *root;
  cJSON *parts;
  cJSON *components;
  cJSON *nodes;
  cJSON *connections;
  char *printed;
  FILE *fp;
  int i;

  if (!composition || !out_path || !composition->composition_ok ||
      composition->block_count <= 0)
    return 1;

  root = cJSON_CreateObject();
  if (!root)
    return 1;

  cJSON_AddStringToObject(root, "schema", "schematic-ir.v1");
  cJSON_AddStringToObject(root, "name", "usb_c_stm32_composed");
  cJSON_AddStringToObject(root, "description",
                          "Gate4/7 composed expand from catalog blocks");
  cJSON_AddStringToObject(root, "category", "Gate7");
  parts = cJSON_AddArrayToObject(root, "parts");
  components = cJSON_AddArrayToObject(root, "components");
  nodes = cJSON_AddArrayToObject(root, "nodes");
  connections = cJSON_AddArrayToObject(root, "connections");

  for (i = 0; i < composition->block_count; i++) {
    char *text = read_all(composition->blocks[i].path);
    cJSON *block;
    cJSON *expand;
    cJSON *arr;
    cJSON *item;
    const char *prefix = composition->blocks[i].id;

    if (!text)
      goto fail;
    block = cJSON_Parse(text);
    free(text);
    if (!block)
      goto fail;

    expand = cJSON_GetObjectItemCaseSensitive(block, "expand");
    if (!cJSON_IsObject(expand)) {
      cJSON_Delete(block);
      goto fail;
    }

    arr = cJSON_GetObjectItemCaseSensitive(expand, "parts");
    if (cJSON_IsArray(arr)) {
      cJSON_ArrayForEach(item, arr) {
        const char *mpn =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "mpn"));
        if (mpn && !part_mpn_in_array(parts, mpn))
          cJSON_AddItemToArray(parts, cJSON_Duplicate(item, 1));
      }
    }

    arr = cJSON_GetObjectItemCaseSensitive(expand, "nodes");
    if (cJSON_IsArray(arr)) {
      cJSON_ArrayForEach(item, arr) {
        if (cJSON_IsString(item) && !node_in_array(nodes, item->valuestring))
          cJSON_AddItemToArray(nodes, cJSON_CreateString(item->valuestring));
      }
    }

    arr = cJSON_GetObjectItemCaseSensitive(expand, "components");
    if (cJSON_IsArray(arr)) {
      cJSON_ArrayForEach(item, arr) {
        cJSON *copy = cJSON_Duplicate(item, 1);
        char rolebuf[96];
        const char *role =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
        if (!copy || !role) {
          cJSON_Delete(copy);
          cJSON_Delete(block);
          goto fail;
        }
        snprintf(rolebuf, sizeof(rolebuf), "%s_%s", prefix, role);
        cJSON_ReplaceItemInObjectCaseSensitive(copy, "role",
                                               cJSON_CreateString(rolebuf));
        cJSON_AddItemToArray(components, copy);
      }
    }

    arr = cJSON_GetObjectItemCaseSensitive(expand, "connections");
    if (cJSON_IsArray(arr)) {
      cJSON_ArrayForEach(item, arr) {
        cJSON *copy = cJSON_Duplicate(item, 1);
        char rolebuf[96];
        const char *role =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "role"));
        if (!copy || !role) {
          cJSON_Delete(copy);
          cJSON_Delete(block);
          goto fail;
        }
        snprintf(rolebuf, sizeof(rolebuf), "%s_%s", prefix, role);
        cJSON_ReplaceItemInObjectCaseSensitive(copy, "role",
                                               cJSON_CreateString(rolebuf));
        cJSON_AddItemToArray(connections, copy);
      }
    }

    cJSON_Delete(block);
  }

  if (cJSON_GetArraySize(components) < 1 || cJSON_GetArraySize(nodes) < 1) {
    cJSON_Delete(root);
    return 1;
  }

  printed = cJSON_Print(root);
  cJSON_Delete(root);
  if (!printed)
    return 1;

  fp = fopen(out_path, "wb");
  if (!fp) {
    free(printed);
    return 1;
  }
  fputs(printed, fp);
  fputc('\n', fp);
  fclose(fp);
  free(printed);
  return 0;

fail:
  cJSON_Delete(root);
  return 1;
}

