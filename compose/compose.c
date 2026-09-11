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
