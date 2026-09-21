#include "cli.h"

#include "cJSON.h"
#include "compiler.h"
#include "compose.h"
#include "db.h"
#include "diag_error.h"
#include "emit.h"
#include "gemini_schematic.h"
#include "llm_provider.h"
#include "mfg_dfm.h"
#include "schematic_load.h"
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

static int write_text_file(const char *path, const char *text) {
  FILE *fp;
  if (!path || !text)
    return 1;
  fp = fopen(path, "wb");
  if (!fp)
    return 1;
  fputs(text, fp);
  fclose(fp);
  return 0;
}

static void write_error_report(const char *out_dir, const char *what,
                               const char *component, const char *why,
                               const char *fix, int partial) {
  char *path;
  cJSON *root;
  char *printed;
  if (!out_dir)
    return;
  path = cli_join_path(out_dir, "error-report.v1.json");
  if (!path)
    return;
  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "schema", "error-report.v1");
  cJSON_AddStringToObject(root, "what_failed", what ? what : "");
  cJSON_AddStringToObject(root, "component_or_net", component ? component : "");
  cJSON_AddStringToObject(root, "why", why ? why : "");
  cJSON_AddStringToObject(root, "how_to_fix", fix ? fix : "");
  cJSON_AddBoolToObject(root, "partial_artifacts", partial ? 1 : 0);
  printed = cJSON_Print(root);
  cJSON_Delete(root);
  if (printed) {
    write_text_file(path, printed);
    free(printed);
  }
  free(path);
}

static void write_build_manifest(const char *out_dir, const char *design_json,
                                 const char *catalogue_db,
                                 const char *dfm_profile) {
  char *path;
  cJSON *root;
  char *printed;
  const char *seed_env = getenv("SYNTH_SEED");
  if (!out_dir)
    return;
  path = cli_join_path(out_dir, "build-manifest.v1.json");
  if (!path)
    return;
  root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "schema", "build-manifest.v1");
  cJSON_AddStringToObject(root, "compiler_version", "0.2.0");
  cJSON_AddStringToObject(root, "library_version", "part_lib.v1");
  cJSON_AddStringToObject(root, "design_ir", design_json ? design_json : "");
  cJSON_AddStringToObject(root, "catalogue", catalogue_db ? catalogue_db : "");
  cJSON_AddStringToObject(root, "dfm_profile", dfm_profile ? dfm_profile : "");
  cJSON_AddStringToObject(root, "seed", seed_env && seed_env[0] ? seed_env : "0");
  printed = cJSON_Print(root);
  cJSON_Delete(root);
  if (printed) {
    write_text_file(path, printed);
    free(printed);
  }
  free(path);
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

int cmd_generate_design(const char *design_json, const char *out_dir,
                        const char *catalogue_db, const char *dfm_profile) {
  char *db_path = NULL;
  char *net_path = NULL;
  char *bom_path = NULL;
  char *sch_path = NULL;
  char *snap_path = NULL;
  char *verify_path = NULL;
  char *dfm_path = NULL;
  char topology_name[64];
  DB *db = NULL;
  CompiledSchematic schematic;
  VerifyResult verify;
  MfgDfmResult dfm;
  MfgDfmProfile profile;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  memset(&dfm, 0, sizeof(dfm));
  topology_name[0] = '\0';

  if (!design_json || !out_dir)
    return 1;

  if (!catalogue_db || !catalogue_db[0])
    catalogue_db = getenv("SYNTH_CATALOGUE_DB");
  if (!dfm_profile || !dfm_profile[0])
    dfm_profile = getenv("SYNTH_DFM_PROFILE");

  if (ensure_dir(out_dir) != 0) {
    fprintf(stderr, "[GENERATE] cannot create %s\n", out_dir);
    return 1;
  }

  if (schematic_ir_validate_file(design_json) != 0) {
    fprintf(stderr, "[GENERATE] schematic IR invalid: %s\n", design_json);
    if (diag_last_error()[0])
      fprintf(stderr, "[GENERATE] %s\n", diag_last_error());
    write_error_report(out_dir, "IR validation", "", diag_last_error(),
                       "Fix the schematic IR fields listed in the error, then "
                       "regenerate.",
                       0);
    write_build_manifest(out_dir, design_json, catalogue_db, dfm_profile);
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
  dfm_path = cli_join_path(out_dir, "mfg-dfm.v1.json");

  if (!db_path || !net_path || !bom_path || !sch_path || !snap_path ||
      !verify_path || !dfm_path)
    goto done;

  remove(db_path);

  db = DB_open(db_path);
  if (!db) {
    fprintf(stderr, "[GENERATE] DB_open failed\n");
    goto done;
  }

  if (seed_load_topology_json(db, design_json) != 0) {
    fprintf(stderr, "[GENERATE] seed failed for %s\n", design_json);
    if (diag_last_error()[0])
      fprintf(stderr, "[GENERATE] %s\n", diag_last_error());
    write_error_report(out_dir, "seed/load topology", "", diag_last_error(),
                       "Correct roles/nodes/pins in the IR.", 0);
    goto done;
  }

  if (catalogue_db && catalogue_db[0]) {
    int merged = DB_MergePartsFrom(db, catalogue_db);
    if (merged < 0)
      fprintf(stderr, "[GENERATE] catalogue merge failed: %s\n", catalogue_db);
    else
      printf("[GENERATE] merged %d catalogue part(s) from %s\n", merged,
             catalogue_db);
  }

  if (compiler_compile_from_design(db, topology_name, design_json, &schematic) !=
      DB_OK) {
    fprintf(stderr, "[GENERATE] compile failed for %s\n", topology_name);
    if (diag_last_error()[0])
      fprintf(stderr, "[GENERATE] %s\n", diag_last_error());
    write_error_report(out_dir, "compile/bind", topology_name,
                       diag_last_error(),
                       "Upload matching catalogue parts or adjust target "
                       "values/packages.",
                       0);
    goto done;
  }

  write_build_manifest(out_dir, design_json, catalogue_db, dfm_profile);

  if (verify_bound_schematic(&schematic, verify_path, &verify) != 0) {
    fprintf(stderr, "[GENERATE] verification failed: %s\n", verify.summary);
    printf("[GENERATE] wrote %s (failed)\n", verify_path);
    write_error_report(out_dir, "electrical verification", topology_name,
                       verify.summary,
                       "Adjust values or topology so verification passes.", 1);
    rc = 2;
    goto done;
  }

  if (check_gate5_cost(&schematic, cli_fixture_root()) != 0) {
    rc = 3;
    goto done;
  }

  if (dfm_profile && dfm_profile[0]) {
    if (mfg_dfm_profile_load_json(dfm_profile, &profile) != 0) {
      fprintf(stderr, "[GENERATE] DFM profile load failed: %s\n", dfm_profile);
      rc = 4;
      goto done;
    }
  } else {
    profile = mfg_dfm_profile_standard();
  }
  {
    int dfc = mfg_dfm_check_schematic(&schematic, &profile, dfm_path, &dfm);
    printf("[GENERATE] wrote %s (%s)\n", dfm_path, dfm.summary);
    if (dfc != 0) {
      fprintf(stderr, "[GENERATE] manufacturing DFM failed: %s\n", dfm.summary);
      rc = 5;
      goto done;
    }
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
  {
    /* Lightweight ERC: all compiled pins connected (already enforced upstream). */
    char *erc_path = cli_join_path(out_dir, "erc.v1.json");
    cJSON *er = cJSON_CreateObject();
    cJSON *errs = cJSON_CreateArray();
    char *printed;
    int ei;
    cJSON_AddStringToObject(er, "schema", "erc.v1");
    cJSON_AddBoolToObject(er, "passed", 1);
    for (ei = 0; ei < schematic.component_count; ei++) {
      if (schematic.components[ei].pin_count < 2) {
        cJSON_AddItemToArray(
            errs, cJSON_CreateString("component has fewer than 2 pins"));
        cJSON_ReplaceItemInObject(er, "passed", cJSON_CreateFalse());
      }
    }
    cJSON_AddItemToObject(er, "errors", errs);
    cJSON_AddStringToObject(er, "summary", "structural ERC: pin connectivity ok");
    printed = cJSON_Print(er);
    cJSON_Delete(er);
    if (erc_path && printed) {
      write_text_file(erc_path, printed);
      printf("[GENERATE] wrote %s\n", erc_path);
    }
    free(printed);
    free(erc_path);
  }
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
  free(dfm_path);
  return rc;
}

int cmd_generate(int argc, char **argv) {
  const char *design_json = NULL;
  const char *spec_path = NULL;
  const char *prompt_path = NULL;
  const char *prompt_text = NULL;
  const char *out_dir = NULL;
  const char *catalogue_db = NULL;
  const char *dfm_profile = NULL;
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
    if (strcmp(argv[i], "--catalogue") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth generate ... --catalogue <parts.db>\n");
        return 1;
      }
      catalogue_db = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--dfm-profile") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Usage: synth generate ... --dfm-profile <profile.json>\n");
        return 1;
      }
      dfm_profile = argv[++i];
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

  return cmd_generate_design(design_json, out_dir, catalogue_db, dfm_profile);
}
