#include "cli.h"

#include "nlp.h"
#include "nlp_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  fclose(fp);
  buf[size] = '\0';
  return buf;
}

int cmd_parse(int argc, char **argv) {
  const char *prompt_text = NULL;
  const char *prompt_path = NULL;
  const char *out_path = "parse_schematic.json";
  char *owned = NULL;
  char clarify[256];
  int i;
  int json = 0;
  int want_neural = 0;

  for (i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--prompt-text") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth parse --prompt-text \"...\" [-o out.json]\n");
        return 1;
      }
      prompt_text = argv[++i];
    } else if (strcmp(argv[i], "--prompt") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth parse --prompt prompt.txt [-o out.json]\n");
        return 1;
      }
      prompt_path = argv[++i];
    } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
      if (i + 1 >= argc)
        return 1;
      out_path = argv[++i];
    } else if (strcmp(argv[i], "--json") == 0) {
      json = 1;
    } else if (strcmp(argv[i], "--neural") == 0) {
      want_neural = 1;
    }
  }

  if (!prompt_text && prompt_path) {
    owned = read_file(prompt_path);
    if (!owned) {
      fprintf(stderr, "[PARSE] cannot read %s\n", prompt_path);
      return 1;
    }
    prompt_text = owned;
  }
  if (!prompt_text) {
    fprintf(stderr,
            "Usage: synth parse --prompt-text \"...\" | --prompt file "
            "[-o out.json] [--neural]\n");
    return 1;
  }

  if (want_neural) {
    NlpSemanticCandidate cand;
    if (nlp_runtime_infer(prompt_text, &cand) != 0) {
      if (json)
        printf("{\"ok\":false,\"backend\":\"neural\",\"clarifying_question\":\"%s\"}\n",
               cand.clarifying_question);
      else
        fprintf(stderr, "[PARSE] neural: %s\n", cand.clarifying_question);
      free(owned);
      return 1;
    }
    /* Future: map cand → IR. Never invent physics here. */
    snprintf(clarify, sizeof(clarify),
             "Neural candidate ok but IR emit not wired; use deterministic parse");
    if (json)
      printf("{\"ok\":false,\"backend\":\"neural\",\"clarifying_question\":\"%s\"}\n",
             clarify);
    else
      fprintf(stderr, "[PARSE] %s\n", clarify);
    free(owned);
    return 1;
  }

  if (nlp_text_to_schematic_ir(prompt_text, out_path, clarify, sizeof(clarify)) !=
      0) {
    if (json)
      printf("{\"ok\":false,\"clarifying_question\":\"%s\"}\n", clarify);
    else
      fprintf(stderr, "[PARSE] %s\n", clarify);
    free(owned);
    return 1;
  }

  if (json)
    printf("{\"ok\":true,\"ir\":\"%s\"}\n", out_path);
  else
    printf("[PARSE] wrote %s\n", out_path);
  free(owned);
  return 0;
}
