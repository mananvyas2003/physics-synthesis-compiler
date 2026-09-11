#include "spec_load.h"

#include "cJSON.h"
#include "cli.h"

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

int spec_load_file(const char *path, SpecV1 *out) {
  char *text;
  cJSON *root;
  cJSON *rails;
  cJSON *ifaces;
  cJSON *item;
  cJSON *tmp;

  if (!path || !out)
    return 1;

  memset(out, 0, sizeof(*out));
  text = read_all(path);
  if (!text)
    return 1;
  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 1;

  rails = cJSON_GetObjectItemCaseSensitive(root, "rails");
  if (cJSON_IsArray(rails)) {
    cJSON_ArrayForEach(item, rails) {
      if (out->rail_count >= 8)
        break;
      if (cJSON_IsString(item)) {
        strncpy(out->rails[out->rail_count], item->valuestring,
                sizeof(out->rails[0]) - 1);
        out->rail_count++;
      }
    }
  }

  tmp = cJSON_GetObjectItemCaseSensitive(root, "current_a");
  if (cJSON_IsNumber(tmp))
    out->current_a = tmp->valuedouble;
  tmp = cJSON_GetObjectItemCaseSensitive(root, "ripple_v");
  if (cJSON_IsNumber(tmp))
    out->ripple_v = tmp->valuedouble;
  tmp = cJSON_GetObjectItemCaseSensitive(root, "efficiency_target");
  if (cJSON_IsNumber(tmp))
    out->efficiency_target = tmp->valuedouble;

  tmp = cJSON_GetObjectItemCaseSensitive(root, "form_factor");
  if (cJSON_IsString(tmp))
    strncpy(out->form_factor, tmp->valuestring, sizeof(out->form_factor) - 1);
  tmp = cJSON_GetObjectItemCaseSensitive(root, "part_preference");
  if (cJSON_IsString(tmp))
    strncpy(out->part_preference, tmp->valuestring,
            sizeof(out->part_preference) - 1);

  ifaces = cJSON_GetObjectItemCaseSensitive(root, "required_interfaces");
  if (cJSON_IsArray(ifaces)) {
    cJSON_ArrayForEach(item, ifaces) {
      if (out->interface_count >= 16)
        break;
      if (cJSON_IsString(item)) {
        strncpy(out->required_interfaces[out->interface_count],
                item->valuestring, sizeof(out->required_interfaces[0]) - 1);
        out->interface_count++;
      }
    }
  }

  tmp = cJSON_GetObjectItemCaseSensitive(root, "inferred");
  if (cJSON_IsObject(tmp)) {
    cJSON *f = cJSON_GetObjectItemCaseSensitive(tmp, "current_a");
    if (cJSON_IsTrue(f))
      out->inferred_current = true;
    f = cJSON_GetObjectItemCaseSensitive(tmp, "efficiency_target");
    if (cJSON_IsTrue(f))
      out->inferred_efficiency = true;
  }

  cJSON_Delete(root);
  return 0;
}

int spec_validate(const SpecV1 *spec) {
  if (!spec)
    return 1;
  if (spec->rail_count <= 0)
    return 1;
  if (spec->current_a < 0.0)
    return 1;
  if (spec->efficiency_target < 0.0 || spec->efficiency_target > 1.0)
    return 1;
  if (spec->form_factor[0] == '\0')
    return 1;
  return 0;
}

int spec_load_and_validate(const char *path, SpecV1 *out) {
  if (spec_load_file(path, out) != 0)
    return 1;
  return spec_validate(out);
}

int spec_resolve_design_path(const SpecV1 *spec, char *out_path, size_t out_len) {
  char *joined;
  (void)spec;
  if (!out_path || out_len == 0)
    return 1;
  /* Offline Gate 3: all specs resolve to the resistor_divider seed design. */
  joined = cli_join_path(cli_fixture_root(), "fixtures/seed/resistor_divider.json");
  if (!joined)
    return 1;
  strncpy(out_path, joined, out_len - 1);
  out_path[out_len - 1] = '\0';
  free(joined);
  return 0;
}
