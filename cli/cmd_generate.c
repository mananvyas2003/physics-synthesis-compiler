#include "cli.h"

#include "cJSON.h"
#include "compiler.h"
#include "compose.h"
#include "db.h"
#include "emit.h"
#include "llm_provider.h"
#include "schematic_load.h"
#include "gemini_schematic.h"
#include "seed_topology.h"
#include "spec_load.h"
#include "verify_report.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define synth_mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define synth_mkdir(path) mkdir((path), 0755)
#endif

static int ensure_dir(const char *path) {
  if (!path || path[0] == '\0')
    return 1;
  synth_mkdir(path);
  return 0;
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

static int topology_name_from_design(const char *design_json, char *out,
                                     size_t out_len) {
  char *text;
  cJSON *root;
  const char *name;

  text = read_file(design_json);
  if (!text)
    return 1;
  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 1;
  name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "name"));
  if (!name) {
    cJSON_Delete(root);
    return 1;
  }
  strncpy(out, name, out_len - 1);
  out[out_len - 1] = '\0';
  cJSON_Delete(root);
  return 0;
}

static int check_gate5_cost(const CompiledSchematic *schematic,
                            const char *fixture_root) {
  char *ref_path;
  char *text;
  cJSON *root;
  cJSON *hand;
  cJSON *max_err;
  cJSON *design;
  double total = 0.0;
  double hand_total;
  double max_rel;
  int i;
  int ok = 0;

  /* Only enforce cost gate for the resistor_divider reference design. */
  if (!schematic || strcmp(schematic->name, "resistor_divider") != 0)
    return 0;

  ref_path =
      cli_join_path(fixture_root, "fixtures/reference/gate5_bom_cost.json");
  if (!ref_path)
    return 1;
  text = read_file(ref_path);
  free(ref_path);
  if (!text)
    return 1;
  root = cJSON_Parse(text);
  free(text);
  if (!root)
    return 1;

  hand = cJSON_GetObjectItemCaseSensitive(root, "hand_reference_total_usd");
  max_err = cJSON_GetObjectItemCaseSensitive(root, "max_relative_error");
  design = cJSON_GetObjectItemCaseSensitive(root, "design");
  (void)design;
  if (!cJSON_IsNumber(hand) || !cJSON_IsNumber(max_err)) {
    cJSON_Delete(root);
    return 1;
  }
  hand_total = hand->valuedouble;
  max_rel = max_err->valuedouble;

  for (i = 0; i < schematic->component_count; i++)
    total += schematic->components[i].unit_cost;

  if (hand_total > 0.0 &&
      fabs(total - hand_total) / hand_total <= max_rel + 1e-12)
    ok = 1;

  if (!ok)
    fprintf(stderr,
            "[GENERATE] Gate5 cost gate failed total=%.6g hand=%.6g max_rel=%.3g\n",
            total, hand_total, max_rel);

  cJSON_Delete(root);
  return ok ? 0 : 1;
}

int cmd_generate_design(const char *design_json, const char *out_dir) {
  char *db_path = NULL;
  char *net_path = NULL;
  char *bom_path = NULL;
  char *sch_path = NULL;
  char *snap_path = NULL;
  char *verify_path = NULL;
  char topology_name[64];
  DB *db = NULL;
  CompiledSchematic schematic;
  VerifyResult verify;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  topology_name[0] = '\0';

  if (!design_json || !out_dir)
    return 1;

  if (ensure_dir(out_dir) != 0) {
    fprintf(stderr, "[GENERATE] cannot create %s\n", out_dir);
    return 1;
  }

  if (schematic_ir_validate_file(design_json) != 0) {
    fprintf(stderr, "[GENERATE] schematic IR invalid: %s\n", design_json);
    return 1;
  }

  if (topology_name_from_design(design_json, topology_name,
                                sizeof(topology_name)) != 0) {
    fprintf(stderr, "[GENERATE] missing topology name in %s\n", design_json);
    return 1;
  }

  db_path = cli_join_path(out_dir, "generate_work.db");
  net_path = cli_join_path(out_dir, "design.net");
  bom_path = cli_join_path(out_dir, "bom.csv");
  sch_path = cli_join_path(out_dir, "design.kicad_sch");
  snap_path = cli_join_path(out_dir, "design-snapshot.v1.json");
  verify_path = cli_join_path(out_dir, "verification.v1.json");

  if (!db_path || !net_path || !bom_path || !sch_path || !snap_path ||
      !verify_path)
    goto done;

  remove(db_path);

  db = DB_open(db_path);
  if (!db) {
    fprintf(stderr, "[GENERATE] DB_open failed\n");
    goto done;
  }

  if (seed_load_topology_json(db, design_json) != 0) {
    fprintf(stderr, "[GENERATE] seed failed for %s\n", design_json);
    goto done;
  }

  if (compiler_compile_from_design(db, topology_name, design_json, &schematic) !=
      DB_OK) {
    fprintf(stderr, "[GENERATE] compile failed for %s\n", topology_name);
    goto done;
  }

  if (verify_bound_schematic(&schematic, verify_path, &verify) != 0) {
    fprintf(stderr, "[GENERATE] verification failed: %s\n", verify.summary);
    printf("[GENERATE] wrote %s (failed)\n", verify_path);
    rc = 2;
    goto done;
  }

  if (check_gate5_cost(&schematic, cli_fixture_root()) != 0) {
    rc = 3;
    goto done;
  }

  if (!emit_ki_cad_netlist(net_path, &schematic) ||
      !emit_bom_csv(bom_path, &schematic) ||
      !compiler_write_kicad_sch(sch_path, &schematic) ||
      !emit_design_snapshot_v1(snap_path, &schematic)) {
    fprintf(stderr, "[GENERATE] emit failed\n");
    goto done;
  }

  if (!emit_snapshot_validate_file(snap_path)) {
    fprintf(stderr, "[GENERATE] snapshot schema validate failed\n");
    goto done;
  }

  printf("[GENERATE] wrote %s\n", net_path);
  printf("[GENERATE] wrote %s\n", bom_path);
  printf("[GENERATE] wrote %s\n", sch_path);
  printf("[GENERATE] wrote %s\n", snap_path);
  printf("[GENERATE] wrote %s\n", verify_path);
  rc = 0;

done:
  if (db)
    DB_close(db);
  compiler_free_schematic(&schematic);
  free(db_path);
  free(net_path);
  free(bom_path);
  free(sch_path);
  free(snap_path);
  free(verify_path);
  return rc;
}

int cmd_generate(int argc, char **argv) {
  const char *design_json = NULL;
  const char *spec_path = NULL;
  const char *prompt_path = NULL;
  const char *prompt_text = NULL;
  const char *out_dir = NULL;
  int compose_gate4 = 0;
  int force_offline_prompt = 0;
  int i;
  char resolved_design[512];
  SpecV1 spec;
  ComposeResult compose;
  SchematicIrMeta sch_meta;

  for (i = 2; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--out") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth generate <design.json> -o out/\n");
        return 1;
      }
      out_dir = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--spec") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth generate --spec <spec.json> -o out/\n");
        return 1;
      }
      spec_path = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--prompt") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth generate --prompt <prompt.txt> -o out/\n");
        return 1;
      }
      prompt_path = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--prompt-text") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr,
                "Usage: synth generate --prompt-text \"...\" -o out/\n");
        return 1;
      }
      prompt_text = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--offline-prompt") == 0) {
      force_offline_prompt = 1;
      continue;
    }
    if (strcmp(argv[i], "--compose-gate4") == 0) {
      compose_gate4 = 1;
      continue;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "[GENERATE] unknown flag %s\n", argv[i]);
      return 1;
    }
    if (!design_json)
      design_json = argv[i];
  }

  if (!out_dir) {
    fprintf(stderr, "Usage: synth generate <design.json> -o out/\n");
    return 1;
  }

  if (compose_gate4) {
    char *expanded;
    memset(&compose, 0, sizeof(compose));
    if (compose_gate4_scenario(&compose) != 0) {
      fprintf(stderr, "[GENERATE] Gate4 composition failed\n");
      return 1;
    }
    printf("[GENERATE] Gate4 composition ok blocks=%d\n", compose.block_count);
    if (ensure_dir(out_dir) != 0)
      return 1;
    expanded = cli_join_path(out_dir, "composed_schematic.json");
    if (!expanded)
      return 1;
    if (compose_expand_to_schematic(&compose, expanded) != 0) {
      fprintf(stderr, "[GENERATE] compose expand failed\n");
      free(expanded);
      return 1;
    }
    printf("[GENERATE] expanded schematic %s\n", expanded);
    strncpy(resolved_design, expanded, sizeof(resolved_design) - 1);
    resolved_design[sizeof(resolved_design) - 1] = '\0';
    free(expanded);
    design_json = resolved_design;
  } else if (prompt_path || prompt_text) {
    char *ir_out = NULL;
    memset(&sch_meta, 0, sizeof(sch_meta));
    if (ensure_dir(out_dir) != 0)
      return 1;
    ir_out = cli_join_path(out_dir, "prompt_schematic.json");
    if (!ir_out)
      return 1;
    if (schematic_provider_from_prompt(prompt_path, prompt_text, ir_out,
                                       force_offline_prompt, resolved_design,
                                       sizeof(resolved_design),
                                       &sch_meta) != 0) {
      fprintf(stderr, "[GENERATE] prompt→IR failed: %s\n",
              sch_meta.clarifying_question);
      free(ir_out);
      return 1;
    }
    printf("[GENERATE] prompt IR %s (live=%s)\n", resolved_design,
           (!force_offline_prompt && gemini_api_key_present()) ? "gemini"
                                                              : "offline");
    free(ir_out);
    design_json = resolved_design;
  } else if (spec_path) {
    memset(&spec, 0, sizeof(spec));
    if (spec_provider_validate_retry(spec_file_provider(), NULL, spec_path,
                                     &spec) != 0) {
      fprintf(stderr, "[GENERATE] spec invalid: %s\n",
              spec.clarifying_question);
      return 1;
    }
    if (spec_resolve_design_path(&spec, resolved_design,
                                 sizeof(resolved_design)) != 0) {
      fprintf(stderr, "[GENERATE] cannot resolve design from spec\n");
      return 1;
    }
    design_json = resolved_design;
  }

  if (!design_json) {
    fprintf(stderr, "Usage: synth generate <design.json> -o out/\n");
    return 1;
  }

  return cmd_generate_design(design_json, out_dir);
}
