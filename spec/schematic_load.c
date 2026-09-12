#include "schematic_load.h"

#include "cJSON.h"
#include "cli.h"
#include "gemini_schematic.h"

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

static int validate_root(cJSON *root) {
  cJSON *name;
  cJSON *desc;
  cJSON *cat;
  cJSON *parts;
  cJSON *components;
  cJSON *nodes;
  cJSON *connections;
  cJSON *item;

  if (!root)
    return 1;

  name = cJSON_GetObjectItemCaseSensitive(root, "name");
  desc = cJSON_GetObjectItemCaseSensitive(root, "description");
  cat = cJSON_GetObjectItemCaseSensitive(root, "category");
  parts = cJSON_GetObjectItemCaseSensitive(root, "parts");
  components = cJSON_GetObjectItemCaseSensitive(root, "components");
  nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
  connections = cJSON_GetObjectItemCaseSensitive(root, "connections");

  if (!cJSON_IsString(name) || name->valuestring[0] == '\0')
    return 1;
  if (!cJSON_IsString(desc) || !cJSON_IsString(cat))
    return 1;
  if (!cJSON_IsArray(parts) || cJSON_GetArraySize(parts) < 1)
    return 1;
  if (!cJSON_IsArray(components) || cJSON_GetArraySize(components) < 1)
    return 1;
  if (!cJSON_IsArray(nodes) || cJSON_GetArraySize(nodes) < 1)
    return 1;
  if (!cJSON_IsArray(connections) || cJSON_GetArraySize(connections) < 1)
    return 1;

  cJSON_ArrayForEach(item, parts) {
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "mpn")) ||
        !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "type")) ||
        !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "package")) ||
        !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "value")))
      return 1;
  }

  cJSON_ArrayForEach(item, components) {
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "role")) ||
        !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "part_type")) ||
        !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "quantity")))
      return 1;
  }

  cJSON_ArrayForEach(item, nodes) {
    if (!cJSON_IsString(item))
      return 1;
  }

  cJSON_ArrayForEach(item, connections) {
    if (!cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "role")) ||
        !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "pin")) ||
        !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "node")))
      return 1;
  }

  return 0;
}

int schematic_ir_validate_file(const char *path) {
  char *text;
  cJSON *root;
  int rc;

  text = read_all(path);
  if (!text)
    return 1;
  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 1;
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
  if (!text)
    return 1;
  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 1;

  if (validate_root(root) != 0) {
    cJSON_Delete(root);
    snprintf(out->clarifying_question, sizeof(out->clarifying_question),
             "Schematic IR invalid; clarify name/parts/components/nodes/connections.");
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
           "No valid schematic IR for prompt after 3 attempts; clarify topology.");
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
