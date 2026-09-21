#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <time.h>
#include <unistd.h>
#define GEMINI_CURL "curl"
#else
#include <windows.h>
#define GEMINI_CURL "curl.exe"
#endif

#include "gemini_schematic.h"

#include "cJSON.h"
#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *api_key(void) {
  const char *k = getenv("GEMINI_API_KEY");
  if (k && k[0])
    return k;
  k = getenv("SYNTH_LLM_API_KEY");
  if (k && k[0])
    return k;
  return NULL;
}

static int get_candidate_models(const char *models[8]) {
  int count = 0;
  const char *m = getenv("SYNTH_GEMINI_MODEL");
  const char *fb = getenv("SYNTH_GEMINI_FALLBACK_MODEL");
  static const char *stock[] = {
    "gemini-3.8-flash",
    "gemini-3.7-flash",
    "gemini-3.1-flash-lite",
    "gemini-3.5-flash",
    NULL
  };
  int i, j, already;

  if (m && m[0])
    models[count++] = m;

  if (fb && fb[0]) {
    already = 0;
    for (j = 0; j < count; j++) {
      if (strcmp(models[j], fb) == 0) {
        already = 1;
        break;
      }
    }
    if (!already)
      models[count++] = fb;
  }

  for (i = 0; stock[i] && count < 7; i++) {
    already = 0;
    for (j = 0; j < count; j++) {
      if (strcmp(models[j], stock[i]) == 0) {
        already = 1;
        break;
      }
    }
    if (!already)
      models[count++] = stock[i];
  }
  models[count] = NULL;
  return count;
}

static void sleep_ms(unsigned ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec req;
  req.tv_sec = (time_t)(ms / 1000u);
  req.tv_nsec = (long)((ms % 1000u) * 1000000L);
  (void)nanosleep(&req, NULL);
#endif
}

static int response_is_retriable_model_error(const char *resp, int *is_404) {
  if (!resp)
    return 0;
  if (is_404)
    *is_404 = 0;
  if (strstr(resp, "\"code\": 404") || strstr(resp, "\"code\":404") ||
      strstr(resp, "NOT_FOUND")) {
    if (is_404)
      *is_404 = 1;
    return 1;
  }
  if (strstr(resp, "\"code\": 503") || strstr(resp, "\"code\":503") ||
      strstr(resp, "\"code\": 429") || strstr(resp, "\"code\":429") ||
      strstr(resp, "UNAVAILABLE") || strstr(resp, "high demand") ||
      strstr(resp, "currently experiencing") ||
      strstr(resp, "RESOURCE_EXHAUSTED")) {
    return 1;
  }
  return 0;
}

int gemini_api_key_present(void) { return api_key() != NULL; }

static char *read_all_file(const char *path) {
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

static int write_all_file(const char *path, const char *text) {
  FILE *fp = fopen(path, "wb");
  if (!fp)
    return 1;
  fputs(text, fp);
  fputc('\n', fp);
  fclose(fp);
  return 0;
}

static char *json_escape(const char *s) {
  size_t n = 0;
  size_t i;
  char *out;
  size_t o = 0;

  if (!s)
    s = "";
  for (i = 0; s[i]; i++) {
    char c = s[i];
    if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t')
      n += 2;
    else
      n += 1;
  }
  out = malloc(n + 1);
  if (!out)
    return NULL;
  for (i = 0; s[i]; i++) {
    char c = s[i];
    if (c == '"') {
      out[o++] = '\\';
      out[o++] = '"';
    } else if (c == '\\') {
      out[o++] = '\\';
      out[o++] = '\\';
    } else if (c == '\n') {
      out[o++] = '\\';
      out[o++] = 'n';
    } else if (c == '\r') {
      out[o++] = '\\';
      out[o++] = 'r';
    } else if (c == '\t') {
      out[o++] = '\\';
      out[o++] = 't';
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
  return out;
}

static const char *SYSTEM_RULES =
    "You are a schematic IR generator for physics-synthesis-compiler. "
    "Reply with ONLY one JSON object matching schematic-ir.v1. No markdown. "
    "Required fields: name, description, category, parts, components, nodes, "
    "connections. "
    "Supported part_type values: resistor, capacitor, inductor, diode, led "
    "(prefer these; avoid transistor/regulator until DC models exist). "
    "CRITICAL: set parts to [] (empty). Do NOT invent MPNs or catalogue rows. "
    "Part binding is owned by the deterministic compiler/catalogue DB. "
    "components need role, part_type, quantity, target_value, package only. "
    "CRITICAL FORMAT RULES: "
    "1. 'target_value' MUST be numeric SI (ohms, farads, or volts for LED Vf). "
    "Prefer numbers (10000, 1e-7, 2.0). Engineering strings like \"1k\" ok. "
    "2. 'quantity' MUST be a numeric integer (e.g. 1). "
    "3. connections are {role,pin,node}. Pins are \"1\" and \"2\" for passives; "
    "for LED/diode you may use \"A\"/\"K\" (anode/cathode) or \"1\"/\"2\". "
    "4. Always include GND and a power net (VIN, VBUS, VCC, 3V3, or 5V). "
    "Circuit recipes (topology only — no MPNs): "
    "- LED indicator: series resistor + led (Vf~2.0), nets e.g. 5V/VIN, LED_A, "
    "GND. "
    "- RC low-pass: resistor + capacitor, nets VIN, VOUT, GND. "
    "- Divider: two resistors, nets VIN, VOUT, GND. "
    "Keep designs small (2-6 components).";

static char *extract_json_object(const char *text) {
  const char *start;
  const char *p;
  int depth = 0;
  int in_str = 0;
  int esc = 0;
  size_t len;
  char *out;

  if (!text)
    return NULL;
  start = strchr(text, '{');
  if (!start)
    return NULL;
  for (p = start; *p; p++) {
    char c = *p;
    if (in_str) {
      if (esc)
        esc = 0;
      else if (c == '\\')
        esc = 1;
      else if (c == '"')
        in_str = 0;
      continue;
    }
    if (c == '"') {
      in_str = 1;
      continue;
    }
    if (c == '{')
      depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        len = (size_t)(p - start + 1);
        out = malloc(len + 1);
        if (!out)
          return NULL;
        memcpy(out, start, len);
        out[len] = '\0';
        return out;
      }
    }
  }
  return NULL;
}

static char *gemini_response_text(const char *http_body) {
  cJSON *root;
  cJSON *cands;
  cJSON *cand0;
  cJSON *content;
  cJSON *parts;
  cJSON *part;
  cJSON *text;
  cJSON *thought;
  char *accum = NULL;
  size_t accum_len = 0;

  root = cJSON_Parse(http_body);
  if (!root)
    return NULL;
  cands = cJSON_GetObjectItemCaseSensitive(root, "candidates");
  if (!cJSON_IsArray(cands) || cJSON_GetArraySize(cands) < 1) {
    cJSON_Delete(root);
    return NULL;
  }
  cand0 = cJSON_GetArrayItem(cands, 0);
  content = cJSON_GetObjectItemCaseSensitive(cand0, "content");
  parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
  if (!cJSON_IsArray(parts) || cJSON_GetArraySize(parts) < 1) {
    cJSON_Delete(root);
    return NULL;
  }

  /* Concatenate non-thought text parts */
  cJSON_ArrayForEach(part, parts) {
    thought = cJSON_GetObjectItemCaseSensitive(part, "thought");
    if (thought && cJSON_IsTrue(thought))
      continue;
    text = cJSON_GetObjectItemCaseSensitive(part, "text");
    if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) {
      size_t tlen = strlen(text->valuestring);
      char *new_accum = realloc(accum, accum_len + tlen + 1);
      if (!new_accum) {
        free(accum);
        cJSON_Delete(root);
        return NULL;
      }
      accum = new_accum;
      memcpy(accum + accum_len, text->valuestring, tlen);
      accum_len += tlen;
      accum[accum_len] = '\0';
    }
  }

  /* Fallback: if all parts had thought flag or empty, take first text */
  if (!accum) {
    cJSON_ArrayForEach(part, parts) {
      text = cJSON_GetObjectItemCaseSensitive(part, "text");
      if (cJSON_IsString(text) && text->valuestring && text->valuestring[0]) {
        accum = malloc(strlen(text->valuestring) + 1);
        if (accum)
          strcpy(accum, text->valuestring);
        break;
      }
    }
  }

  cJSON_Delete(root);
  return accum;
}

static int curl_post_json(const char *url, const char *api_key_hdr,
                          const char *body_path, const char *resp_path) {
  char cmd[2048];
  int rc;

  snprintf(cmd, sizeof(cmd),
           "%s -sS --connect-timeout 10 --max-time 45 -X POST \"%s\" "
           "-H \"Content-Type: application/json\" "
           "-H \"x-goog-api-key: %s\" "
           "--data-binary \"@%s\" -o \"%s\"",
           GEMINI_CURL, url, api_key_hdr, body_path, resp_path);
  rc = system(cmd);
  return rc == 0 ? 0 : 1;
}

static int one_gemini_attempt(const char *prompt_text, const char *feedback,
                              const char *model, const char *out_ir_path,
                              char *errbuf, size_t errlen, int *retriable_out,
                              int *is_404_out) {
  const char *key = api_key();
  char url[512];
  char body_path[1024];
  char resp_path[1024];
  char *esc_sys;
  char *esc_user;
  char *body = NULL;
  char *resp = NULL;
  char *model_text = NULL;
  char *json_obj = NULL;
  size_t body_cap;
  int rc = 1;

  if (retriable_out)
    *retriable_out = 0;
  if (is_404_out)
    *is_404_out = 0;

  if (out_ir_path && out_ir_path[0]) {
    snprintf(body_path, sizeof(body_path), "%s.req.json", out_ir_path);
    snprintf(resp_path, sizeof(resp_path), "%s.resp.json", out_ir_path);
  } else {
    strncpy(body_path, "gemini_req.json", sizeof(body_path) - 1);
    body_path[sizeof(body_path) - 1] = '\0';
    strncpy(resp_path, "gemini_resp.json", sizeof(resp_path) - 1);
    resp_path[sizeof(resp_path) - 1] = '\0';
  }

  if (!key) {
    snprintf(errbuf, errlen, "GEMINI_API_KEY not set");
    return 1;
  }
  if (!model || !model[0]) {
    snprintf(errbuf, errlen, "Gemini model name empty");
    return 1;
  }

  esc_sys = json_escape(SYSTEM_RULES);
  {
    char user_buf[8192];
    if (feedback && feedback[0])
      snprintf(user_buf, sizeof(user_buf),
               "User prompt:\n%s\n\nPrevious IR was invalid:\n%s\n"
               "Return corrected schematic-ir.v1 JSON only. Supported types: "
               "resistor, capacitor, diode, led. Values are SI numbers "
               "(ohms/farads/Vf). Pins 1/2 or A/K. Include GND and a power net.",
               prompt_text, feedback);
    else
      snprintf(user_buf, sizeof(user_buf),
               "User prompt:\n%s\n\nReturn schematic-ir.v1 JSON only. Supported "
               "types: resistor, capacitor, diode, led. Values are SI numbers. "
               "Pins 1/2 or A/K. Include GND and a power net.",
               prompt_text);
    esc_user = json_escape(user_buf);
  }
  if (!esc_sys || !esc_user)
    goto done;

  body_cap = strlen(esc_sys) + strlen(esc_user) + 512;
  body = malloc(body_cap);
  if (!body)
    goto done;
  snprintf(body, body_cap,
           "{"
           "\"systemInstruction\":{\"parts\":[{\"text\":\"%s\"}]},"
           "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}],"
           "\"generationConfig\":{\"temperature\":0.2,\"responseMimeType\":"
           "\"application/json\"}"
           "}",
           esc_sys, esc_user);

  if (write_all_file(body_path, body) != 0) {
    snprintf(errbuf, errlen, "cannot write Gemini request body");
    goto done;
  }

  snprintf(url, sizeof(url),
           "https://generativelanguage.googleapis.com/v1beta/models/%s:"
           "generateContent",
           model);

  if (curl_post_json(url, key, body_path, resp_path) != 0) {
    snprintf(errbuf, errlen, "curl Gemini request failed (network or timeout)");
    if (retriable_out)
      *retriable_out = 1;
    goto done;
  }

  resp = read_all_file(resp_path);
  if (!resp) {
    snprintf(errbuf, errlen, "empty Gemini response");
    if (retriable_out)
      *retriable_out = 1;
    goto done;
  }

  /* Surface API errors. */
  if (strstr(resp, "\"error\"")) {
    int f404 = 0;
    if (response_is_retriable_model_error(resp, &f404)) {
      if (retriable_out)
        *retriable_out = 1;
      if (is_404_out)
        *is_404_out = f404;
      if (f404) {
        snprintf(errbuf, errlen,
                 "Gemini model %s not found (404, trying fallback): %.100s",
                 model, resp);
      } else {
        snprintf(errbuf, errlen,
                 "Gemini model %s overloaded (503/429, trying fallback): %.100s",
                 model, resp);
      }
    } else {
      snprintf(errbuf, errlen, "Gemini API error (check key/model): %.180s",
               resp);
    }
    goto done;
  }

  model_text = gemini_response_text(resp);
  if (!model_text) {
    snprintf(errbuf, errlen, "Gemini response missing candidates[].content");
    goto done;
  }

  json_obj = extract_json_object(model_text);
  if (!json_obj) {
    snprintf(errbuf, errlen, "model did not return a JSON object");
    goto done;
  }

  if (write_all_file(out_ir_path, json_obj) != 0) {
    snprintf(errbuf, errlen, "cannot write IR file");
    goto done;
  }

  rc = 0;

done:
  free(esc_sys);
  free(esc_user);
  free(body);
  free(resp);
  free(model_text);
  free(json_obj);
  remove(body_path);
  remove(resp_path);
  return rc;
}

int gemini_schematic_from_prompt(const char *prompt_text, const char *out_ir_path,
                                 SchematicIrMeta *meta) {
  const char *candidate_models[8];
  int total_candidates;
  int current_model_idx = 0;
  int attempt;
  int is_retriable;
  int is_404;
  char err[512];
  char feedback[1024];

  if (!prompt_text || !out_ir_path || !meta)
    return 1;
  memset(meta, 0, sizeof(*meta));
  feedback[0] = '\0';

  if (!gemini_api_key_present()) {
    snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
             "Set GEMINI_API_KEY in the environment for live prompt→schematic.");
    return 1;
  }

  total_candidates = get_candidate_models(candidate_models);
  if (total_candidates == 0) {
    snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
             "No Gemini models configured.");
    return 1;
  }

  for (attempt = 0; attempt < 6; attempt++) {
    const char *model = candidate_models[current_model_idx];
    err[0] = '\0';
    is_retriable = 0;
    is_404 = 0;

    if (one_gemini_attempt(prompt_text, feedback[0] ? feedback : NULL, model,
                           out_ir_path, err, sizeof(err), &is_retriable,
                           &is_404) != 0) {
      snprintf(feedback, sizeof(feedback), "%s", err);
      if (is_retriable) {
        if (!is_404)
          sleep_ms(1000u * (unsigned)((attempt % 3) + 1));
        if (current_model_idx + 1 < total_candidates)
          current_model_idx++;
      }
      continue;
    }

    if (schematic_ir_load_and_validate(out_ir_path, meta) == 0)
      return 0;

    snprintf(feedback, sizeof(feedback),
             "schema validation failed: %s",
             meta->clarifying_question[0] ? meta->clarifying_question
                                          : "invalid schematic-ir.v1");
  }

  if (strstr(feedback, "overloaded") || strstr(feedback, "503") ||
      strstr(feedback, "currently experiencing")) {
    snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
             "Gemini models temporarily busy (503). Retry in a moment or set "
             "SYNTH_GEMINI_MODEL in GEMINI_API_KEY.local. Last: %.120s",
             feedback);
  } else {
    snprintf(meta->clarifying_question, sizeof(meta->clarifying_question),
             "Gemini IR invalid after retries; clarify rails/topology. Last: "
             "%.120s",
             feedback);
  }
  return 1;
}
