#include "mfg_dfm.h"

#include "cJSON.h"
#include "vendor_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MfgDfmProfile mfg_dfm_profile_standard(void) {
  MfgDfmProfile p;
  memset(&p, 0, sizeof(p));
  strncpy(p.name, "standard", sizeof(p.name) - 1);
  p.layer_count = 2;
  p.copper_weight_oz = 1.0;
  p.min_trace_width_mm = 0.127;
  p.min_clearance_mm = 0.127;
  p.min_via_diameter_mm = 0.6; /* >= drill + 2*annular (0.56) */
  p.min_drill_mm = 0.3;
  p.min_annular_ring_mm = 0.13;
  p.board_edge_clearance_mm = 0.25;
  p.max_component_height_mm = 1.6;
  return p;
}

MfgDfmProfile mfg_dfm_profile_wearable(void) {
  MfgDfmProfile p;
  memset(&p, 0, sizeof(p));
  strncpy(p.name, "wearable", sizeof(p.name) - 1);
  p.layer_count = 2;
  p.copper_weight_oz = 0.5;
  p.min_trace_width_mm = 0.09;
  p.min_clearance_mm = 0.09;
  p.min_via_diameter_mm = 0.35;
  p.min_drill_mm = 0.15;
  p.min_annular_ring_mm = 0.1;
  p.board_edge_clearance_mm = 0.15;
  p.max_component_height_mm = 0.8;
  return p;
}

static char *read_file(const char *path) {
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

int mfg_dfm_profile_load_json(const char *path, MfgDfmProfile *out) {
  char *text;
  cJSON *root;
  cJSON *item;
  MfgDfmProfile p;

  if (!path || !out)
    return 1;

  text = read_file(path);
  if (!text)
    return 1;
  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 1;

  p = mfg_dfm_profile_standard();
  item = cJSON_GetObjectItemCaseSensitive(root, "name");
  if (cJSON_IsString(item) && item->valuestring)
    strncpy(p.name, item->valuestring, sizeof(p.name) - 1);
  item = cJSON_GetObjectItemCaseSensitive(root, "layer_count");
  if (cJSON_IsNumber(item))
    p.layer_count = item->valueint;
  item = cJSON_GetObjectItemCaseSensitive(root, "copper_weight_oz");
  if (cJSON_IsNumber(item))
    p.copper_weight_oz = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "min_trace_width_mm");
  if (cJSON_IsNumber(item))
    p.min_trace_width_mm = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "min_clearance_mm");
  if (cJSON_IsNumber(item))
    p.min_clearance_mm = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "min_via_diameter_mm");
  if (cJSON_IsNumber(item))
    p.min_via_diameter_mm = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "min_drill_mm");
  if (cJSON_IsNumber(item))
    p.min_drill_mm = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "min_annular_ring_mm");
  if (cJSON_IsNumber(item))
    p.min_annular_ring_mm = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "board_edge_clearance_mm");
  if (cJSON_IsNumber(item))
    p.board_edge_clearance_mm = item->valuedouble;
  item = cJSON_GetObjectItemCaseSensitive(root, "max_component_height_mm");
  if (cJSON_IsNumber(item))
    p.max_component_height_mm = item->valuedouble;

  cJSON_Delete(root);
  *out = p;
  return 0;
}

int mfg_dfm_check_schematic(const CompiledSchematic *schematic,
                            const MfgDfmProfile *profile,
                            const char *report_path, MfgDfmResult *out) {
  /* Delegate to teammate electronics_vendor_v2_next DFM engine. */
  return vendor_dfm_check_schematic(schematic, profile, report_path, out);
}
