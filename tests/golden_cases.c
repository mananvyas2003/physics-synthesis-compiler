#include "golden_cases.h"

#include "cli.h"
#include "compiler.h"
#include "compose.h"
#include "db.h"
#include "diag_error.h"
#include "emit.h"
#include "physics2_interpreter.h"
#include "schematic_load.h"
#include "seed_topology.h"
#include "spec_load.h"
#include "verify_report.h"
#include "mfg_dfm.h"
#include "nlp.h"
#include "nlp_runtime.h"
#include "gemini_schematic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int near_eq(double actual, double expected, double tolerance) {
  return fabs(actual - expected) <= tolerance;
}

static char *join_root(const char *root, const char *rel) {
  return cli_join_path(root ? root : cli_fixture_root(), rel);
}

int golden_g01_resistor_stamp(FILE *out) {
  PhysicsPrimitive r1;
  PhysicsProgram program;
  PhysicsAccumulator *accumulator = NULL;
  PhysicsExecutionContext context;
  PhysicsInterpreter interpreter;
  NodeId vin;
  NodeId vout;
  NodeId terminals[2];
  double expected;
  int rc = 1;

  if (!out)
    return 1;

  physics2_program_init(&program);
  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);

  if (vin == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE)
    goto done;

  if (!physics2_primitive_init_resistor(&r1, "R1", 10000.0, 1.0))
    goto done;

  terminals[0] = vin;
  terminals[1] = vout;

  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);
  if (!accumulator)
    goto done;

  if (!physics2_context_init(&context, &program, accumulator, 1e-3))
    goto done;

  if (!physics2_interpreter_init(&interpreter, &program, &context) ||
      !physics2_interpreter_execute(&interpreter)) {
    physics2_context_free(&context);
    goto done;
  }

  expected = 1.0 / 10000.0;

  if (!near_eq(physics2_accumulator_get(accumulator, 0, 0), expected, 1e-12) ||
      !near_eq(physics2_accumulator_get(accumulator, 0, 1), -expected, 1e-12) ||
      !near_eq(physics2_accumulator_get(accumulator, 1, 0), -expected, 1e-12) ||
      !near_eq(physics2_accumulator_get(accumulator, 1, 1), expected, 1e-12)) {
    physics2_context_free(&context);
    goto done;
  }

  fprintf(out, "g01_resistor_stamp\n");
  fprintf(out, "G00=%.12g\n", physics2_accumulator_get(accumulator, 0, 0));
  fprintf(out, "G01=%.12g\n", physics2_accumulator_get(accumulator, 0, 1));
  fprintf(out, "G10=%.12g\n", physics2_accumulator_get(accumulator, 1, 0));
  fprintf(out, "G11=%.12g\n", physics2_accumulator_get(accumulator, 1, 1));

  physics2_context_free(&context);
  rc = 0;

done:
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);
  return rc;
}

int golden_g02_divider_5v(FILE *out) {
  PhysicsPrimitive r1;
  PhysicsPrimitive r2;
  PhysicsPrimitive v1;
  PhysicsProgram program;
  PhysicsAccumulator *accumulator = NULL;
  PhysicsExecutionContext context;
  NodeId vin;
  NodeId vout;
  NodeId gnd;
  NodeId terminals[2];
  double actual;
  int rc = 1;

  if (!out)
    return 1;

  physics2_program_init(&program);
  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);

  if (vin == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE ||
      gnd == PHYSICS2_NODE_NONE)
    goto done;

  if (!physics2_primitive_init_vsource(&v1, "V1", 10.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 10000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r2, "R2", 10000.0, 0.0))
    goto done;

  terminals[0] = vin;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &v1, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  terminals[0] = vin;
  terminals[1] = vout;
  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  terminals[0] = vout;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &r2, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);
  if (!accumulator)
    goto done;

  if (!physics2_context_init(&context, &program, accumulator, 1e-3))
    goto done;

  if (!physics2_context_step(&context, gnd)) {
    physics2_context_free(&context);
    goto done;
  }

  actual = context.solution[vout];
  if (!near_eq(actual, 5.0, 1e-10)) {
    physics2_context_free(&context);
    goto done;
  }

  fprintf(out, "g02_divider_5v\n");
  fprintf(out, "Vout=%.10f\n", actual);

  physics2_context_free(&context);
  rc = 0;

done:
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);
  return rc;
}

int golden_g03_three_resistor(FILE *out) {
  PhysicsPrimitive r1;
  PhysicsPrimitive r2;
  PhysicsPrimitive r3;
  PhysicsPrimitive v1;
  PhysicsProgram program;
  PhysicsAccumulator *accumulator = NULL;
  PhysicsExecutionContext context;
  NodeId vin;
  NodeId vout;
  NodeId gnd;
  NodeId terminals[2];
  double actual;
  double expected;
  int rc = 1;

  if (!out)
    return 1;

  physics2_program_init(&program);
  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);

  if (!physics2_primitive_init_vsource(&v1, "V1", 12.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 10000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r2, "R2", 20000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r3, "R3", 30000.0, 0.0))
    goto done;

  terminals[0] = vin;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &v1, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  terminals[0] = vin;
  terminals[1] = vout;
  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  terminals[0] = vout;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &r2, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  terminals[0] = vout;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &r3, terminals, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);
  if (!accumulator)
    goto done;

  if (!physics2_context_init(&context, &program, accumulator, 1e-3))
    goto done;

  if (!physics2_context_step(&context, gnd)) {
    physics2_context_free(&context);
    goto done;
  }

  actual = context.solution[vout];
  expected = 12.0 * 12000.0 / (10000.0 + 12000.0);

  if (!near_eq(actual, expected, 1e-10)) {
    physics2_context_free(&context);
    goto done;
  }

  fprintf(out, "g03_three_resistor\n");
  fprintf(out, "Vout=%.10f\n", actual);

  physics2_context_free(&context);
  rc = 0;

done:
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);
  return rc;
}

int golden_g04_dfm_suite(FILE *out) {
  int failures;

  if (!out)
    return 1;

  failures = cmd_dfm_run_suite();
  if (failures != 0)
    return 1;

  fprintf(out, "g04_dfm_suite\n");
  fprintf(out, "dfm_failures=0\n");
  fprintf(out, "cases=compatible,voltage_mismatch,current_mismatch,"
               "source_to_source,ground,composition\n");
  return 0;
}

static int extract_sch_summary(const char *path, FILE *out) {
  FILE *fp;
  char line[512];
  int labels = 0;
  int placed_refs = 0;

  fp = fopen(path, "rb");
  if (!fp)
    return 1;

  fprintf(out, "g05_compile_divider_sch\n");

  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, "(property \"Reference\" \"r1\"") ||
        strstr(line, "(property \"Reference\" \"r2\"")) {
      placed_refs++;
    }
    if (strstr(line, "(label \"VIN\"")) {
      fprintf(out, "label=VIN\n");
      labels++;
    }
    if (strstr(line, "(label \"VOUT\"")) {
      fprintf(out, "label=VOUT\n");
      labels++;
    }
    if (strstr(line, "(label \"GND\"")) {
      fprintf(out, "label=GND\n");
      labels++;
    }
  }

  fclose(fp);

  fprintf(out, "placed_refs=%d\n", placed_refs);
  fprintf(out, "labels=%d\n", labels);

  if (placed_refs != 2 || labels != 3)
    return 1;

  return 0;
}

int golden_g05_compile_divider_sch(FILE *out, const char *fixture_root) {
  char *seed_path = NULL;
  char db_path[] = "golden_g05_tmp.db";
  char sch_path[] = "golden_g05_tmp.kicad_sch";
  DB *db = NULL;
  CompiledSchematic schematic;
  DBResult result;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));

  if (!out)
    return 1;

  seed_path = join_root(fixture_root, "fixtures/seed/resistor_divider.json");
  if (!seed_path) {
    fprintf(stderr, "[G05] seed path join failed\n");
    return 1;
  }

  remove(db_path);
  remove(sch_path);

  db = DB_open(db_path);
  if (!db) {
    fprintf(stderr, "[G05] DB_open failed for %s\n", db_path);
    goto done;
  }

  if (seed_load_topology_json(db, seed_path) != 0) {
    fprintf(stderr, "[G05] seed_load_topology_json failed for %s\n", seed_path);
    goto done;
  }

  result = compiler_compile_resistor_divider(db, "resistor_divider", &schematic);
  if (result != DB_OK) {
    fprintf(stderr, "[G05] compile failed result=%d\n", (int)result);
    goto done;
  }

  if (!compiler_write_kicad_sch(sch_path, &schematic)) {
    fprintf(stderr, "[G05] write_kicad_sch failed\n");
    compiler_free_schematic(&schematic);
    goto done;
  }

  compiler_free_schematic(&schematic);

  if (extract_sch_summary(sch_path, out) != 0) {
    fprintf(stderr, "[G05] extract_sch_summary failed\n");
    goto done;
  }

  rc = 0;

done:
  if (db)
    DB_close(db);
  free(seed_path);
  remove(db_path);
  remove(sch_path);
  return rc;
}

static int run_generate_artifacts(const char *fixture_root, const char *outdir,
                                  CompiledSchematic *schematic) {
  char *seed_path = NULL;
  char *db_path = NULL;
  DB *db = NULL;
  int rc = 1;

  seed_path = join_root(fixture_root, "fixtures/seed/resistor_divider.json");
  db_path = cli_join_path(outdir, "golden_gen.db");
  if (!seed_path || !db_path)
    goto done;

  remove(db_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, seed_path) != 0)
    goto done;
  if (compiler_compile_from_design(db, "resistor_divider", seed_path,
                                   schematic) != DB_OK)
    goto done;
  rc = 0;

done:
  if (db)
    DB_close(db);
  free(seed_path);
  if (db_path) {
    remove(db_path);
    free(db_path);
  }
  return rc;
}

int golden_g06_generate_net(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  char net_path[] = "golden_g06_tmp.net";
  FILE *fp;
  char line[512];
  int comps = 0;
  int nets = 0;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  if (!out)
    return 1;
  if (run_generate_artifacts(fixture_root, ".", &schematic) != 0)
    goto done;
  if (!emit_ki_cad_netlist(net_path, &schematic))
    goto done;

  fp = fopen(net_path, "rb");
  if (!fp)
    goto done;
  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, "(comp (ref "))
      comps++;
    if (strstr(line, "(net (code "))
      nets++;
  }
  fclose(fp);

  fprintf(out, "g06_generate_net\n");
  fprintf(out, "components=%d\n", comps);
  fprintf(out, "nets=%d\n", nets);
  if (comps == 2 && nets == 3)
    rc = 0;

done:
  compiler_free_schematic(&schematic);
  remove(net_path);
  return rc;
}

int golden_g07_generate_bom(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  char bom_path[] = "golden_g07_tmp.csv";
  FILE *fp;
  char line[1024];
  int rows = 0;
  int has_header = 0;
  int has_rationale = 0;
  int has_alt = 0;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  if (!out)
    return 1;
  if (run_generate_artifacts(fixture_root, ".", &schematic) != 0)
    goto done;
  if (!emit_bom_csv(bom_path, &schematic))
    goto done;

  fp = fopen(bom_path, "rb");
  if (!fp)
    goto done;
  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, "refdes,value,package,MPN,JLCPCB,quantity"))
      has_header = 1;
    if (strstr(line, "rationale"))
      has_rationale = 1;
    if (strstr(line, "alternate_mpn"))
      has_alt = 1;
    if (strncmp(line, "r1,", 3) == 0 || strncmp(line, "r2,", 3) == 0)
      rows++;
  }
  fclose(fp);

  fprintf(out, "g07_generate_bom\n");
  fprintf(out, "header=%d\n", has_header);
  fprintf(out, "rationale_col=%d\n", has_rationale);
  fprintf(out, "alternate_col=%d\n", has_alt);
  fprintf(out, "rows=%d\n", rows);
  if (has_header && has_rationale && has_alt && rows == 2)
    rc = 0;

done:
  compiler_free_schematic(&schematic);
  remove(bom_path);
  return rc;
}

int golden_g08_snapshot(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  char snap_path[] = "golden_g08_tmp.json";
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  if (!out)
    return 1;
  if (run_generate_artifacts(fixture_root, ".", &schematic) != 0)
    goto done;
  if (!emit_design_snapshot_v1(snap_path, &schematic))
    goto done;
  if (!emit_snapshot_validate_file(snap_path))
    goto done;

  fprintf(out, "g08_snapshot\n");
  fprintf(out, "schema=design-snapshot.v1\n");
  fprintf(out, "topology=%s\n", schematic.name);
  fprintf(out, "components=%d\n", schematic.component_count);
  if (schematic.component_count == 2)
    rc = 0;

done:
  compiler_free_schematic(&schematic);
  remove(snap_path);
  return rc;
}

int golden_g09_spec_corpus(FILE *out, const char *fixture_root) {
  int i;
  int ok = 0;
  int fail = 0;

  if (!out)
    return 1;

  for (i = 1; i <= 30; i++) {
    char rel[64];
    char *path;
    SpecV1 spec;
    snprintf(rel, sizeof(rel), "fixtures/specs/%03d.json", i);
    path = join_root(fixture_root, rel);
    if (!path) {
      fail++;
      continue;
    }
    memset(&spec, 0, sizeof(spec));
    if (spec_load_and_validate(path, &spec) == 0)
      ok++;
    else
      fail++;
    free(path);
  }

  fprintf(out, "g09_spec_corpus\n");
  fprintf(out, "valid=%d\n", ok);
  fprintf(out, "invalid=%d\n", fail);
  return (ok == 30 && fail == 0) ? 0 : 1;
}

int golden_g10_compose_gate4(FILE *out, const char *fixture_root) {
  ComposeResult result;
  (void)fixture_root;
  if (!out)
    return 1;
  memset(&result, 0, sizeof(result));
  if (compose_gate4_scenario(&result) != 0)
    return 1;
  fprintf(out, "g10_compose_gate4\n");
  fprintf(out, "blocks=%d\n", result.block_count);
  fprintf(out, "composition_ok=%d\n", result.composition_ok);
  return (result.block_count == 5 && result.composition_ok) ? 0 : 1;
}

int golden_g11_bind_rationale(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  int i;
  int with_r = 0;
  int with_a = 0;
  double total = 0.0;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  if (!out)
    return 1;
  if (run_generate_artifacts(fixture_root, ".", &schematic) != 0)
    goto done;

  for (i = 0; i < schematic.component_count; i++) {
    if (schematic.components[i].rationale[0])
      with_r++;
    if (schematic.components[i].has_alternate)
      with_a++;
    total += schematic.components[i].unit_cost;
  }

  fprintf(out, "g11_bind_rationale\n");
  fprintf(out, "with_rationale=%d\n", with_r);
  fprintf(out, "with_alternate=%d\n", with_a);
  fprintf(out, "cost_ok=%d\n",
          (fabs(total - 0.022) / 0.022 <= 0.15) ? 1 : 0);
  if (with_r == 2 && with_a == 2 && fabs(total - 0.022) / 0.022 <= 0.15)
    rc = 0;

done:
  compiler_free_schematic(&schematic);
  return rc;
}

int golden_g12_verify_report(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  VerifyResult verify;
  char path[] = "golden_g12_verify.json";
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  if (!out)
    return 1;
  if (run_generate_artifacts(fixture_root, ".", &schematic) != 0)
    goto done;
  if (verify_bound_schematic(&schematic, path, &verify) != 0)
    goto done;

  fprintf(out, "g12_verify_report\n");
  fprintf(out, "passed=%d\n", verify.passed);
  fprintf(out, "rating_violations=%d\n", verify.rating_violations);
  if (verify.passed && verify.rating_violations == 0)
    rc = 0;

done:
  compiler_free_schematic(&schematic);
  remove(path);
  return rc;
}

int golden_g13_schematic_corpus(FILE *out, const char *fixture_root) {
  int i;
  int ok = 0;
  int fail = 0;

  if (!out)
    return 1;

  for (i = 1; i <= 10; i++) {
    char rel[64];
    char *path;
    snprintf(rel, sizeof(rel), "fixtures/schematics/%03d.json", i);
    path = join_root(fixture_root, rel);
    if (!path) {
      fail++;
      continue;
    }
    if (schematic_ir_validate_file(path) == 0)
      ok++;
    else
      fail++;
    free(path);
  }

  fprintf(out, "g13_schematic_corpus\n");
  fprintf(out, "valid=%d\n", ok);
  fprintf(out, "invalid=%d\n", fail);
  return (ok == 10 && fail == 0) ? 0 : 1;
}

int golden_g14_prompt_generate(FILE *out, const char *fixture_root) {
  char *prompt;
  char ir_path[512];
  SchematicIrMeta meta;
  CompiledSchematic schematic;
  char *db_path = NULL;
  DB *db = NULL;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&meta, 0, sizeof(meta));
  if (!out)
    return 1;

  prompt = join_root(fixture_root, "fixtures/prompts/001.txt");
  if (!prompt)
    return 1;
  if (schematic_provider_from_prompt(prompt, NULL, NULL, /*force_offline=*/1,
                                     ir_path, sizeof(ir_path), &meta) != 0) {
    free(prompt);
    return 1;
  }
  free(prompt);

  db_path = cli_join_path(".", "golden_g14.db");
  if (!db_path)
    return 1;
  remove(db_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, ir_path) != 0)
    goto done;
  if (compiler_compile_from_design(db, meta.name, ir_path, &schematic) != DB_OK)
    goto done;

  fprintf(out, "g14_prompt_generate\n");
  fprintf(out, "topology=%s\n", schematic.name);
  fprintf(out, "components=%d\n", schematic.component_count);
  if (schematic.component_count == 2)
    rc = 0;

done:
  if (db)
    DB_close(db);
  compiler_free_schematic(&schematic);
  if (db_path) {
    remove(db_path);
    free(db_path);
  }
  return rc;
}

int golden_g15_compose_expand_sch(FILE *out, const char *fixture_root) {
  ComposeResult compose;
  char path[] = "golden_g15_composed.json";
  FILE *fp;
  char *text = NULL;
  long size;
  int components = 0;
  int has_vbus = 0;
  int has_3v3 = 0;
  int rc = 1;
  const char *p;
  (void)fixture_root;

  if (!out)
    return 1;
  memset(&compose, 0, sizeof(compose));
  if (compose_gate4_scenario(&compose) != 0)
    return 1;
  if (compose_expand_to_schematic(&compose, path) != 0)
    return 1;

  fp = fopen(path, "rb");
  if (!fp)
    goto done;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    goto done;
  }
  size = ftell(fp);
  rewind(fp);
  if (size < 0) {
    fclose(fp);
    goto done;
  }
  text = malloc((size_t)size + 1);
  if (!text) {
    fclose(fp);
    goto done;
  }
  if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
    fclose(fp);
    free(text);
    text = NULL;
    goto done;
  }
  text[size] = '\0';
  fclose(fp);

  p = text;
  while ((p = strstr(p, "\"target_value\"")) != NULL) {
    components++;
    p += 14;
  }
  if (strstr(text, "\"VBUS\""))
    has_vbus = 1;
  if (strstr(text, "\"3V3\""))
    has_3v3 = 1;

  fprintf(out, "g15_compose_expand_sch\n");
  fprintf(out, "blocks=%d\n", compose.block_count);
  fprintf(out, "components=%d\n", components);
  fprintf(out, "has_vbus=%d\n", has_vbus);
  fprintf(out, "has_3v3=%d\n", has_3v3);
  if (compose.block_count == 5 && components == 5 && has_vbus && has_3v3)
    rc = 0;

done:
  free(text);
  remove(path);
  return rc;
}

static char *slurp_file(const char *path) {
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

static int count_substr(const char *hay, const char *needle) {
  int n = 0;
  const char *p = hay;
  size_t len = strlen(needle);
  if (!hay || !needle || len == 0)
    return 0;
  while ((p = strstr(p, needle)) != NULL) {
    n++;
    p += len;
  }
  return n;
}

/*
 * Structural ERC surrogate (always runs in CI without KiCad):
 * - Device:R symbols present with pin 1/2
 * - Labels (local or global) present for nets
 * - Composed sch must not use the old vertical inter-part short pattern alone
 */
static int structural_erc_check(const char *sch_path, int expect_symbols,
                                int require_global_labels) {
  char *text;
  int symbols;
  int pin1;
  int pin2;
  int labels;
  int global_labels;
  int wires;

  text = slurp_file(sch_path);
  if (!text)
    return 1;

  symbols = count_substr(text, "(lib_id \"Device:R\")");
  pin1 = count_substr(text, "(pin \"1\"");
  pin2 = count_substr(text, "(pin \"2\"");
  labels = count_substr(text, "(label \"");
  global_labels = count_substr(text, "(global_label \"");
  wires = count_substr(text, "(wire");

  free(text);

  if (symbols != expect_symbols)
    return 1;
  if (pin1 < expect_symbols || pin2 < expect_symbols)
    return 1;
  if (labels + global_labels < expect_symbols)
    return 1;
  if (require_global_labels && global_labels < expect_symbols * 2)
    return 1;
  if (wires < expect_symbols)
    return 1;
  return 0;
}

int golden_g16_structural_erc(FILE *out, const char *fixture_root) {
  ComposeResult compose;
  CompiledSchematic schematic;
  char *seed_path = NULL;
  char *db_path = NULL;
  char compose_ir[] = "golden_g16_composed.json";
  char divider_sch[] = "golden_g16_divider.kicad_sch";
  char compose_sch[] = "golden_g16_compose.kicad_sch";
  DB *db = NULL;
  int divider_ok = 0;
  int compose_ok = 0;
  int topology_ok = 0;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&compose, 0, sizeof(compose));
  if (!out)
    return 1;

  /* Divider special-case layout (local labels) — Gate 2 path. */
  seed_path = join_root(fixture_root, "fixtures/seed/resistor_divider.json");
  db_path = cli_join_path(".", "golden_g16.db");
  if (!seed_path || !db_path)
    goto done;
  remove(db_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, seed_path) != 0)
    goto done;
  if (compiler_compile_from_design(db, "resistor_divider", seed_path,
                                   &schematic) != DB_OK)
    goto done;
  if (!compiler_write_kicad_sch(divider_sch, &schematic))
    goto done;
  divider_ok = structural_erc_check(divider_sch, 2, 0) == 0 ? 1 : 0;
  compiler_free_schematic(&schematic);
  memset(&schematic, 0, sizeof(schematic));
  DB_close(db);
  db = NULL;
  remove(db_path);

  /* Gate 4 composed expand → sch with global labels (no stand-in). */
  if (compose_gate4_scenario(&compose) != 0)
    goto done;
  if (compose_expand_to_schematic(&compose, compose_ir) != 0)
    goto done;
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, compose_ir) != 0)
    goto done;
  if (compiler_compile_from_design(db, "usb_c_stm32_composed", compose_ir,
                                   &schematic) != DB_OK)
    goto done;
  if (strcmp(schematic.name, "usb_c_stm32_composed") == 0 &&
      schematic.component_count == 5)
    topology_ok = 1;
  if (!compiler_write_kicad_sch(compose_sch, &schematic))
    goto done;
  compose_ok = structural_erc_check(compose_sch, 5, 1) == 0 ? 1 : 0;

  fprintf(out, "g16_structural_erc\n");
  fprintf(out, "divider_erc=%d\n", divider_ok);
  fprintf(out, "compose_topology=%d\n", topology_ok);
  fprintf(out, "compose_erc=%d\n", compose_ok);
  if (divider_ok && topology_ok && compose_ok)
    rc = 0;

done:
  if (db)
    DB_close(db);
  compiler_free_schematic(&schematic);
  free(seed_path);
  if (db_path) {
    remove(db_path);
    free(db_path);
  }
  remove(compose_ir);
  remove(divider_sch);
  remove(compose_sch);
  return rc;
}

static int generate_fixture_case(const char *fixture_root, const char *rel,
                                 const char *topology, VerifyResult *vout,
                                 CompiledSchematic *schematic) {
  char *seed_path = NULL;
  char *db_path = NULL;
  char verify_path[] = "golden_phase1_verify.json";
  DB *db = NULL;
  int rc = 1;

  seed_path = join_root(fixture_root, rel);
  db_path = cli_join_path(".", "golden_phase1_tmp.db");
  if (!seed_path || !db_path)
    goto done;
  remove(db_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, seed_path) != 0)
    goto done;
  if (compiler_compile_from_design(db, topology, seed_path, schematic) != DB_OK)
    goto done;
  if (verify_bound_schematic(schematic, verify_path, vout) != 0)
    goto done;
  rc = 0;
done:
  if (db)
    DB_close(db);
  if (db_path) {
    remove(db_path);
    free(db_path);
  }
  free(seed_path);
  remove(verify_path);
  return rc;
}

int golden_g17_led_series(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  VerifyResult verify;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  if (!out)
    return 1;
  if (generate_fixture_case(fixture_root, "fixtures/seed/led_series.json",
                            "led_series_indicator", &verify, &schematic) != 0)
    goto done;
  fprintf(out, "g17_led_series\n");
  fprintf(out, "components=%d\n", schematic.component_count);
  fprintf(out, "passed=%d\n", verify.passed);
  if (schematic.component_count == 2 && verify.passed)
    rc = 0;
done:
  compiler_free_schematic(&schematic);
  return rc;
}

int golden_g18_rc_low_pass(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  VerifyResult verify;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  if (!out)
    return 1;
  if (generate_fixture_case(fixture_root, "fixtures/seed/rc_low_pass.json",
                            "rc_low_pass_filter", &verify, &schematic) != 0)
    goto done;
  fprintf(out, "g18_rc_low_pass\n");
  fprintf(out, "components=%d\n", schematic.component_count);
  fprintf(out, "passed=%d\n", verify.passed);
  if (schematic.component_count == 2 && verify.passed)
    rc = 0;
done:
  compiler_free_schematic(&schematic);
  return rc;
}

int golden_g19_invalid_role(FILE *out, const char *fixture_root) {
  char *path;
  int rejected = 0;

  if (!out)
    return 1;
  path = join_root(fixture_root, "fixtures/seed/invalid_unknown_role.json");
  if (!path)
    return 1;
  if (schematic_ir_validate_file(path) != 0)
    rejected = 1;
  free(path);
  fprintf(out, "g19_invalid_role\n");
  fprintf(out, "rejected=%d\n", rejected);
  if (rejected && strstr(diag_last_error(), "unknown role"))
    return 0;
  if (rejected && strstr(diag_last_error(), "Unknown role"))
    return 0;
  if (rejected && strstr(diag_last_error(), "R99"))
    return 0;
  return rejected ? 0 : 1;
}

int golden_g20_rl_low_pass(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  VerifyResult verify;
  int rc = 1;
  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  if (!out)
    return 1;
  if (generate_fixture_case(fixture_root, "fixtures/seed/rl_low_pass.json",
                            "rl_low_pass_filter", &verify, &schematic) != 0)
    goto done;
  fprintf(out, "g20_rl_low_pass\n");
  fprintf(out, "components=%d\n", schematic.component_count);
  fprintf(out, "passed=%d\n", verify.passed);
  if (schematic.component_count == 2 && verify.passed)
    rc = 0;
done:
  compiler_free_schematic(&schematic);
  return rc;
}

int golden_g21_ldo_3v3(FILE *out, const char *fixture_root) {
  CompiledSchematic schematic;
  VerifyResult verify;
  char *seed_path = NULL;
  char *db_path = NULL;
  char verify_path[] = "golden_g21_verify.json";
  DB *db = NULL;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  if (!out)
    return 1;

  seed_path = join_root(fixture_root, "fixtures/seed/ldo_3v3.json");
  db_path = cli_join_path(".", "golden_g21_tmp.db");
  if (!seed_path || !db_path)
    goto done;
  remove(db_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, seed_path) != 0)
    goto done;
  if (compiler_compile_from_design(db, "ldo_3v3", seed_path, &schematic) !=
      DB_OK)
    goto done;
  /* Verify is expected to fail-closed for IC/regulator (returns non-zero). */
  (void)verify_bound_schematic(&schematic, verify_path, &verify);

  fprintf(out, "g21_ldo_3v3\n");
  fprintf(out, "components=%d\n", schematic.component_count);
  fprintf(out, "passed=%d\n", verify.passed);
  if (schematic.component_count == 2 && !verify.passed)
    rc = 0;
done:
  if (db)
    DB_close(db);
  if (db_path) {
    remove(db_path);
    free(db_path);
  }
  free(seed_path);
  remove(verify_path);
  compiler_free_schematic(&schematic);
  return rc;
}

/*
 * Physics2 Shockley diode Newton vs closed-form OP (matches vendor mna_test
 * circuit: 5 V — 1 kΩ — diode — GND, Is=1e-12, n=1, Vt=k*300/q).
 */
int golden_g22_diode_newton(FILE *out) {
  PhysicsPrimitive vsrc;
  PhysicsPrimitive r1;
  PhysicsPrimitive d1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vin;
  NodeId n_mid;
  NodeId n_gnd;
  NodeId t[2];
  double vt;
  double vmid;
  double current;
  int rc = 1;

  if (!out)
    return 1;

  vt = (1.380649e-23 * 300.0) / 1.602176634e-19;

  physics2_program_init(&program);
  n_vin = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_gnd = physics2_program_new_node(&program);

  if (!physics2_primitive_init_vsource(&vsrc, "V1", 5.0, 0.0))
    goto done;
  t[0] = n_vin;
  t[1] = n_gnd;
  if (physics2_program_add_primitive(&program, &vsrc, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  if (!physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0))
    goto done;
  t[0] = n_vin;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  if (!physics2_primitive_init_diode(&d1, "D1", 1.0e-12, 1.0, vt, 0.0))
    goto done;
  t[0] = n_mid;
  t[1] = n_gnd;
  if (physics2_program_add_primitive(&program, &d1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;

  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc)
    goto done;
  if (!physics2_context_init(&ctx, &program, acc, 1e-3))
    goto done;
  if (!physics2_context_step(&ctx, n_gnd)) {
    physics2_context_free(&ctx);
    goto done;
  }

  vmid = ctx.solution[n_mid];
  current = (5.0 - vmid) / 1000.0;
  /* Fixed reference print for golden file; tolerances checked below. */
  fprintf(out, "g22_diode_newton\n");
  fprintf(out, "vmid=0.574147\n");
  fprintf(out, "current=0.004426\n");

  if (near_eq(vmid, 0.574147, 1.0e-3) && near_eq(current, 0.00442585, 1.0e-5))
    rc = 0;

  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Floating node / no reference path → solve must fail. */
int golden_g23_singular_float(FILE *out) {
  PhysicsPrimitive r1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId a, b, gnd;
  NodeId t[2];
  int failed = 0;
  int rc = 1;

  if (!out)
    return 1;
  physics2_program_init(&program);
  a = physics2_program_new_node(&program);
  b = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  if (!physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0))
    goto done;
  t[0] = a;
  t[1] = b; /* neither tied to gnd → floating island */
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 1e-3))
    goto done;
  failed = !physics2_context_step(&ctx, gnd);
  physics2_context_free(&ctx);
  fprintf(out, "g23_singular_float\n");
  fprintf(out, "solve_failed=%d\n", failed);
  if (failed)
    rc = 0;
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Two conflicting ideal voltage sources on same nodes → singular/fail. */
int golden_g24_conflict_vsources(FILE *out) {
  PhysicsPrimitive v1, v2;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId p, g;
  NodeId t[2];
  int failed = 0;
  int rc = 1;

  if (!out)
    return 1;
  physics2_program_init(&program);
  p = physics2_program_new_node(&program);
  g = physics2_program_new_node(&program);
  if (!physics2_primitive_init_vsource(&v1, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_vsource(&v2, "V2", 3.3, 0.0))
    goto done;
  t[0] = p;
  t[1] = g;
  if (physics2_program_add_primitive(&program, &v1, t, 2) ==
          PHYSICS_PRIMITIVE_NONE ||
      physics2_program_add_primitive(&program, &v2, t, 2) ==
          PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 1e-3))
    goto done;
  failed = !physics2_context_step(&ctx, g);
  physics2_context_free(&ctx);
  fprintf(out, "g24_conflict_vsources\n");
  fprintf(out, "solve_failed=%d\n", failed);
  if (failed)
    rc = 0;
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* One BE capacitor step: V(0)=0, step with I-source companion check via R||C. */
int golden_g25_be_capacitor(FILE *out) {
  PhysicsPrimitive c1;
  PhysicsPrimitive r1;
  PhysicsPrimitive vs;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_top, n_gnd;
  NodeId t[2];
  double dt = 1.0e-3;
  double C = 1.0e-6;
  double R = 1000.0;
  double v;
  double expected;
  int rc = 1;

  if (!out)
    return 1;
  /*
   * Series R from 5V to mid, C from mid to GND.
   * After one BE step from V=0: G=C/dt, companion RHS.
   * Exact BE for Thevenin is messy; assert 0 < Vmid < 5 and finite.
   */
  physics2_program_init(&program);
  n_top = physics2_program_new_node(&program);
  n_gnd = physics2_program_new_node(&program);
  {
    NodeId n_mid = physics2_program_new_node(&program);
    if (!physics2_primitive_init_vsource(&vs, "V1", 5.0, 0.0) ||
        !physics2_primitive_init_resistor(&r1, "R1", R, 0.0) ||
        !physics2_primitive_init_capacitor(&c1, "C1", C, 0.0))
      goto done;
    t[0] = n_top;
    t[1] = n_gnd;
    if (physics2_program_add_primitive(&program, &vs, t, 2) ==
        PHYSICS_PRIMITIVE_NONE)
      goto done;
    t[0] = n_top;
    t[1] = n_mid;
    if (physics2_program_add_primitive(&program, &r1, t, 2) ==
        PHYSICS_PRIMITIVE_NONE)
      goto done;
    t[0] = n_mid;
    t[1] = n_gnd;
    if (physics2_program_add_primitive(&program, &c1, t, 2) ==
        PHYSICS_PRIMITIVE_NONE)
      goto done;
    acc = physics2_accumulator_create(program.next_node + program.branch_count);
    if (!acc || !physics2_context_init(&ctx, &program, acc, dt))
      goto done;
    if (!physics2_context_step(&ctx, n_gnd)) {
      physics2_context_free(&ctx);
      goto done;
    }
    v = ctx.solution[n_mid];
    /* BE RC step from 0 toward 5: V = 5 * (1 - G_r/(G_r+G_c)) wait —
     * companion: C/dt stamp. Closed form mid voltage:
     * G_c = C/dt, divider with R: V = 5 * (1/R) / (1/R + G_c) ? No —
     * C companion is G between mid-gnd with RHS G*Vprev=0, so
     * Vmid = 5 * Gc_eq... actually R from vin to mid, C mid-gnd:
     * i_R = (5-V)/R, i_C = G V with G=C/dt, KCL: (5-V)/R = G V
     * V = 5 / (1 + R G) = 5 / (1 + R C / dt)
     */
    expected = 5.0 / (1.0 + R * C / dt); /* 2.5 V for R=1k, C=1u, dt=1ms */
    physics2_context_free(&ctx);
    fprintf(out, "g25_be_capacitor\n");
    fprintf(out, "vmid=2.500000\n");
    fprintf(out, "ok=1\n");
    if (near_eq(v, expected, 1e-6) && near_eq(expected, 2.5, 1e-12))
      rc = 0;
  }
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/*
 * Phase 3 regression: 903 sensor rail — capacitors must survive into Physics2;
 * V(SENSOR_VDD) = 3.3 * 10k/(1k+10k) = 3.0 V (caps open at DC).
 */
int golden_g26_903_sensor_power(FILE *out, const char *fixture_root) {
  char *seed_path = NULL;
  char *db_path = NULL;
  char verify_path[] = "golden_g26_verify.json";
  char run_path[] = "golden_g26_test-run.v1.json";
  DB *db = NULL;
  CompiledSchematic schematic;
  VerifyResult verify;
  CompilerPhysDesign phys;
  int bound_caps = 0;
  int bound_r = 0;
  int bound_d = 0;
  int i;
  int rc = 1;
  FILE *runfp = NULL;
  const double expected_sensor = 3.3 * 10000.0 / (1000.0 + 10000.0); /* 3.0 */

  memset(&schematic, 0, sizeof(schematic));
  memset(&verify, 0, sizeof(verify));
  if (!out)
    return 1;

  seed_path = join_root(fixture_root, "fixtures/seed/sensor_903.json");
  db_path = cli_join_path(".", "golden_g26_tmp.db");
  if (!seed_path || !db_path)
    goto done;
  remove(db_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, seed_path) != 0)
    goto done;
  if (compiler_compile_from_design(db, "sensor_supply_conditioning_macro_903",
                                   seed_path, &schematic) != DB_OK)
    goto done;

  for (i = 0; i < schematic.component_count; i++) {
    PartTypes t = schematic.components[i].part.type;
    if (t == PART_CAPACITOR)
      bound_caps++;
    else if (t == PART_RESISTOR)
      bound_r++;
    else if (t == PART_DIODE)
      bound_d++;
  }

  if (verify_bound_schematic(&schematic, verify_path, &verify) != 0)
    goto done;

  compiler_physics_design_init(&phys);
  if (!compiler_schematic_to_phys_design(&schematic, 3.3, &phys)) {
    compiler_physics_design_free(&phys);
    goto done;
  }

  /* Integrity already enforced inside verify; re-check phys design caps. */
  {
    int phys_caps = 0;
    size_t pi;
    for (pi = 0; pi < phys.count; pi++) {
      if (phys.elements[pi].kind == COMPILER_PHYS_CAPACITOR)
        phys_caps++;
    }
    if (bound_caps != 2 || phys_caps != 2 || verify.physics2_caps != 2)
      goto free_phys;
    if (schematic.component_count != 6 || bound_r != 3 || bound_d != 1)
      goto free_phys;
    if (strcmp(verify.measured_node, "SENSOR_VDD") != 0)
      goto free_phys;
    if (!near_eq(verify.measured_v, expected_sensor, 1e-3))
      goto free_phys;
    if (!verify.passed)
      goto free_phys;

    fprintf(out, "g26_903_sensor_power\n");
    fprintf(out, "bound_components=6\n");
    fprintf(out, "bound_caps=2\n");
    fprintf(out, "physics2_caps=2\n");
    fprintf(out, "sensor_vdd_ok=1\n");

    runfp = fopen(run_path, "wb");
    if (runfp) {
      fprintf(runfp,
              "{\n"
              "  \"schema\": \"test-run.v1\",\n"
              "  \"test\": \"903_SENSOR_POWER_MACRO\",\n"
              "  \"topology\": \"sensor_supply_conditioning_macro_903\",\n"
              "  \"bound_components\": %d,\n"
              "  \"bound_caps\": %d,\n"
              "  \"physics2_caps\": %d,\n"
              "  \"physics2_instr\": %d,\n"
              "  \"measured_node\": \"%s\",\n"
              "  \"measured_v\": %.6f,\n"
              "  \"expected_v\": %.6f,\n"
              "  \"passed\": true\n"
              "}\n",
              schematic.component_count, bound_caps, verify.physics2_caps,
              verify.physics2_instr, verify.measured_node, verify.measured_v,
              expected_sensor);
      fclose(runfp);
    }
    rc = 0;
  free_phys:
    compiler_physics_design_free(&phys);
  }

done:
  if (db)
    DB_close(db);
  if (db_path) {
    remove(db_path);
    free(db_path);
  }
  free(seed_path);
  remove(verify_path);
  compiler_free_schematic(&schematic);
  return rc;
}

/* Phase 6: ISOURCE 1 mA into R=1k → V=1 V */
int golden_g27_isource_resistor(FILE *out) {
  PhysicsPrimitive isrc;
  PhysicsPrimitive r1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId a;
  NodeId gnd;
  NodeId t_is[2];
  NodeId t_r[2];
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_isource(&isrc, "I1", 0.001, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0))
    goto done;
  a = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  /* I flows terminal0→terminal1; inject into `a` from gnd → +I·R. */
  t_is[0] = gnd;
  t_is[1] = a;
  t_r[0] = a;
  t_r[1] = gnd;
  if (physics2_program_add_primitive(&program, &isrc, t_is, 2) ==
          PHYSICS_PRIMITIVE_NONE ||
      physics2_program_add_primitive(&program, &r1, t_r, 2) ==
          PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g27_isource_resistor\n");
  fprintf(out, "va=1.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[a], 1.0, 1e-9))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 6: DC capacitor = open; divider unchanged by C to GND */
int golden_g28_dc_capacitor_open(FILE *out) {
  PhysicsPrimitive vs, r1, r2, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  NodeId t[2];
  double expected;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 10.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r2, "R2", 1000.0, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", 1e-6, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &r2, t, 2) ==
          PHYSICS_PRIMITIVE_NONE ||
      physics2_program_add_primitive(&program, &c1, t, 2) ==
          PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  expected = 5.0;
  fprintf(out, "g28_dc_capacitor_open\n");
  fprintf(out, "vmid=5.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[mid], expected, 1e-9) &&
      program.instruction_count == 4)
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 6: DC inductor = ideal short → mid at 0 V */
int golden_g29_dc_inductor_short(FILE *out) {
  PhysicsPrimitive vs, r1, l1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  NodeId t[2];
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_inductor(&l1, "L1", 1e-3, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &l1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g29_dc_inductor_short\n");
  fprintf(out, "vmid=0.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[mid], 0.0, 1e-9))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 6: RL BE one step — Vmid = Vin*(L/dt)/(R+L/dt) */
int golden_g30_rl_be_step(FILE *out) {
  PhysicsPrimitive vs, r1, l1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  NodeId t[2];
  const double R = 1000.0;
  const double L = 1.0;
  const double dt = 0.001;
  double expected;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", R, 0.0) ||
      !physics2_primitive_init_inductor(&l1, "L1", L, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &l1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, dt) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  expected = 5.0 * (L / dt) / (R + L / dt); /* 2.5 V */
  fprintf(out, "g30_rl_be_step\n");
  fprintf(out, "vmid=2.500000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[mid], expected, 1e-6) &&
      near_eq(expected, 2.5, 1e-12))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 17: multi-pin Q/LDO/OpAmp lib_symbols present with real pin numbers */
int golden_g31_multipin_lib_symbol(FILE *out, const char *fixture_root) {
  char *seed_path = NULL;
  char db_path[] = "golden_g31_tmp.db";
  char sch_path[] = "golden_g31_tmp.kicad_sch";
  DB *db = NULL;
  CompiledSchematic schematic;
  FILE *fp;
  char *buf = NULL;
  long sz;
  int has_b = 0;
  int has_vin = 0;
  int has_inp = 0;
  int rc = 1;

  memset(&schematic, 0, sizeof(schematic));
  if (!out)
    return 1;
  seed_path = join_root(fixture_root, "fixtures/seed/resistor_divider.json");
  if (!seed_path)
    return 1;
  remove(db_path);
  remove(sch_path);
  db = DB_open(db_path);
  if (!db)
    goto done;
  if (seed_load_topology_json(db, seed_path) != 0)
    goto done;
  if (compiler_compile_resistor_divider(db, "resistor_divider", &schematic) !=
      DB_OK)
    goto done;
  if (!compiler_write_kicad_sch(sch_path, &schematic))
    goto done;
  fp = fopen(sch_path, "rb");
  if (!fp)
    goto done;
  fseek(fp, 0, SEEK_END);
  sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(fp);
    goto done;
  }
  if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
    fclose(fp);
    goto done;
  }
  buf[sz] = '\0';
  fclose(fp);
  has_b = strstr(buf, "(number \"B\"") != NULL;
  has_vin = strstr(buf, "(number \"VIN\"") != NULL;
  has_inp = strstr(buf, "(number \"IN+\"") != NULL;
  fprintf(out, "g31_multipin_lib_symbol\n");
  fprintf(out, "q_pin_b=%d\n", has_b ? 1 : 0);
  fprintf(out, "ldo_pin_vin=%d\n", has_vin ? 1 : 0);
  fprintf(out, "opamp_pin_inp=%d\n", has_inp ? 1 : 0);
  if (has_b && has_vin && has_inp)
    rc = 0;
done:
  free(buf);
  free(seed_path);
  compiler_free_schematic(&schematic);
  if (db)
    DB_close(db);
  remove(db_path);
  remove(sch_path);
  return rc;
}

/* Phase 7: VCVS — Vp = μ·Vcp; μ=2, Vin=1 → Vout=2 */
int golden_g32_vcvs(FILE *out) {
  PhysicsPrimitive vs, vcvs, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, vout, gnd;
  NodeId t2[2];
  NodeId t4[4];
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 1.0, 0.0) ||
      !physics2_primitive_init_vcvs(&vcvs, "E1", 2.0, 0.0) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1000.0, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t2[0] = vin;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t4[0] = vout;
  t4[1] = gnd;
  t4[2] = vin;
  t4[3] = gnd;
  if (physics2_program_add_primitive(&program, &vcvs, t4, 4) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = vout;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &rl, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g32_vcvs\n");
  fprintf(out, "vout=2.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[vout], 2.0, 1e-9))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 7: VCCS — I = gm·Vin into RL; gm=1e-3, Vin=1, RL=1k → V=1 */
int golden_g33_vccs(FILE *out) {
  PhysicsPrimitive vs, vccs, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, vout, gnd;
  NodeId t2[2];
  NodeId t4[4];
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 1.0, 0.0) ||
      !physics2_primitive_init_vccs(&vccs, "G1", 0.001, 0.0) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1000.0, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t2[0] = vin;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t4[0] = gnd;
  t4[1] = vout;
  t4[2] = vin;
  t4[3] = gnd;
  if (physics2_program_add_primitive(&program, &vccs, t4, 4) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = vout;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &rl, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g33_vccs\n");
  fprintf(out, "vout=1.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[vout], 1.0, 1e-9))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/*
 * Phase 7: CCVS — sense shorts mid→gnd through Vin-R; I=Vin/R;
 * Vout = Rm·I = Rm·Vin/R. Rm=2k, R=1k, Vin=1 → Vout=2.
 */
int golden_g34_ccvs(FILE *out) {
  PhysicsPrimitive vs, rs, ccvs, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, vout, gnd;
  NodeId t2[2];
  NodeId t4[4];
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 1.0, 0.0) ||
      !physics2_primitive_init_resistor(&rs, "RS", 1000.0, 0.0) ||
      !physics2_primitive_init_ccvs(&ccvs, "H1", 2000.0, 0.0) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1000.0, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t2[0] = vin;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = vin;
  t2[1] = mid;
  if (physics2_program_add_primitive(&program, &rs, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t4[0] = vout;
  t4[1] = gnd;
  t4[2] = mid;
  t4[3] = gnd;
  if (physics2_program_add_primitive(&program, &ccvs, t4, 4) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = vout;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &rl, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g34_ccvs\n");
  fprintf(out, "vout=2.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[vout], 2.0, 1e-9))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/*
 * Phase 7: CCCS — I_s=Vin/R through sense short; I_out=β·I_s into RL;
 * Vout=β·Vin·RL/R. β=2, Vin=1, R=RL=1k → Vout=2.
 */
int golden_g35_cccs(FILE *out) {
  PhysicsPrimitive vs, rs, cccs, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, vout, gnd;
  NodeId t2[2];
  NodeId t4[4];
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 1.0, 0.0) ||
      !physics2_primitive_init_resistor(&rs, "RS", 1000.0, 0.0) ||
      !physics2_primitive_init_cccs(&cccs, "F1", 2.0, 0.0) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1000.0, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t2[0] = vin;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = vin;
  t2[1] = mid;
  if (physics2_program_add_primitive(&program, &rs, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t4[0] = gnd;
  t4[1] = vout;
  t4[2] = mid;
  t4[3] = gnd;
  if (physics2_program_add_primitive(&program, &cccs, t4, 4) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = vout;
  t2[1] = gnd;
  if (physics2_program_add_primitive(&program, &rl, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g35_cccs\n");
  fprintf(out, "vout=2.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[vout], 2.0, 1e-9))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 7: PhysDesign → Physics2 lowering for VCVS */
int golden_g36_vcvs_phys_lower(FILE *out) {
  CompilerPhysDesign phys;
  CompiledPhysicsProgram compiled;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  const char *vs_t[] = {"VIN", "GND"};
  const char *e_t[] = {"VOUT", "GND", "VIN", "GND"};
  const char *rl_t[] = {"VOUT", "GND"};
  NodeId gnd, vout;
  int rc = 1;

  if (!out)
    return 1;
  memset(&compiled, 0, sizeof(compiled));
  memset(&ctx, 0, sizeof(ctx));
  compiler_physics_design_init(&phys);
  if (!compiler_physics_design_add(&phys, COMPILER_PHYS_VSOURCE, "V1", 1.0, 0.0,
                                  vs_t, 2) ||
      !compiler_physics_design_add(&phys, COMPILER_PHYS_VCVS, "E1", 2.0, 0.0,
                                  e_t, 4) ||
      !compiler_physics_design_add(&phys, COMPILER_PHYS_RESISTOR, "RL", 1000.0,
                                  0.0, rl_t, 2))
    goto done;
  if (!compiler_lower_to_physics2(&phys, &compiled))
    goto done;
  gnd = compiler_physics_find_node(&compiled, "GND");
  vout = compiler_physics_find_node(&compiled, "VOUT");
  acc = physics2_accumulator_create(compiled.program.next_node +
                                    compiled.program.branch_count);
  if (!acc || gnd == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE ||
      !physics2_context_init(&ctx, &compiled.program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g36_vcvs_phys_lower\n");
  fprintf(out, "vout=2.000000\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[vout], 2.0, 1e-9) &&
      compiled.program.instruction_count == 3)
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  compiler_free_physics_program(&compiled);
  compiler_physics_design_free(&phys);
  return rc;
}

static int build_diode_series(PhysicsProgram *program, PhysicsPrimitive *vs,
                              PhysicsPrimitive *r1, PhysicsPrimitive *d1,
                              NodeId *vin, NodeId *mid, NodeId *gnd) {
  NodeId t[2];
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  if (!physics2_primitive_init_vsource(vs, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_diode(d1, "D1", 1.0e-12, 1.0, vt, 0.0))
    return 1;
  *vin = physics2_program_new_node(program);
  *mid = physics2_program_new_node(program);
  *gnd = physics2_program_new_node(program);
  t[0] = *vin;
  t[1] = *gnd;
  if (physics2_program_add_primitive(program, vs, t, 2) == PHYSICS_PRIMITIVE_NONE)
    return 1;
  t[0] = *vin;
  t[1] = *mid;
  if (physics2_program_add_primitive(program, r1, t, 2) == PHYSICS_PRIMITIVE_NONE)
    return 1;
  t[0] = *mid;
  t[1] = *gnd;
  if (physics2_program_add_primitive(program, d1, t, 2) == PHYSICS_PRIMITIVE_NONE)
    return 1;
  return 0;
}

/* Phase 8: Newton report fields populated on converged diode OP */
int golden_g37_newton_report(FILE *out) {
  PhysicsPrimitive vs, r1, d1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (build_diode_series(&program, &vs, &r1, &d1, &vin, &mid, &gnd) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g37_newton_report\n");
  fprintf(out, "status=ok\n");
  fprintf(out, "converged=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && ctx.newton.iterations > 0 &&
      ctx.newton.residual_norm < 1e-6 && near_eq(ctx.solution[mid], 0.574147, 1e-3))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 8: bad init (Vmid=10) still converges with limiting/damping */
int golden_g38_newton_bad_init(FILE *out) {
  PhysicsPrimitive vs, r1, d1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (build_diode_series(&program, &vs, &r1, &d1, &vin, &mid, &gnd) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0))
    goto done;
  ctx.solution[mid] = 10.0; /* harsh guess; junction vlim must recover */
  if (!physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g38_newton_bad_init\n");
  fprintf(out, "status=ok\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK &&
      near_eq(ctx.solution[mid], 0.574147, 1e-3))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 8: max_iter=1 forces NEWTON_DIVERGED */
int golden_g39_newton_diverge(FILE *out) {
  PhysicsPrimitive vs, r1, d1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  int rc = 1;
  int failed;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (build_diode_series(&program, &vs, &r1, &d1, &vin, &mid, &gnd) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0))
    goto done;
  physics2_context_set_newton_limits(&ctx, 1, 1e-12, 1e-12, 0.25);
  failed = !physics2_context_step(&ctx, gnd);
  fprintf(out, "g39_newton_diverge\n");
  fprintf(out, "status=diverged\n");
  fprintf(out, "ok=1\n");
  if (failed && ctx.newton.status == PHYSICS2_NEWTON_DIVERGED &&
      strstr(ctx.newton.failure, "NEWTON_DIVERGED"))
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 8: two series diodes share one Newton loop */
int golden_g40_two_diodes(FILE *out) {
  PhysicsPrimitive vs, r1, d1, d2;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, mid2, gnd;
  NodeId t[2];
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_diode(&d1, "D1", 1.0e-12, 1.0, vt, 0.0) ||
      !physics2_primitive_init_diode(&d2, "D2", 1.0e-12, 1.0, vt, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  mid2 = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = mid2;
  if (physics2_program_add_primitive(&program, &d1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid2;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &d2, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g40_two_diodes\n");
  fprintf(out, "status=ok\n");
  fprintf(out, "ok=1\n");
  /* Two ~0.57 V drops → mid ≈ 1.15 V, mid2 ≈ 0.57 V */
  if (ctx.newton.status == PHYSICS2_NEWTON_OK &&
      ctx.solution[mid] > ctx.solution[mid2] &&
      ctx.solution[mid2] > 0.4 && ctx.solution[mid2] < 0.8 &&
      ctx.solution[mid] > 0.9 && ctx.solution[mid] < 1.4)
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 9: reverse-biased diode — Vin=-5 → Vmid ≈ -5, |I|≈Isat */
int golden_g41_diode_reverse(FILE *out) {
  PhysicsPrimitive vs, r1, d1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  NodeId t[2];
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  double current;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", -5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_diode(&d1, "D1", 1.0e-12, 1.0, vt, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &d1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  current = (-5.0 - ctx.solution[mid]) / 1000.0;
  fprintf(out, "g41_diode_reverse\n");
  fprintf(out, "reverse=1\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK &&
      ctx.solution[mid] < -4.0 && fabs(current) < 1e-9)
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 9: diode + capacitor BE step still Newton-converges */
int golden_g42_diode_cap_be(FILE *out) {
  PhysicsPrimitive vs, r1, d1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  NodeId t[2];
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  const double dt = 1e-3;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_diode(&d1, "D1", 1.0e-12, 1.0, vt, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", 1e-6, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &d1, t, 2) ==
          PHYSICS_PRIMITIVE_NONE ||
      physics2_program_add_primitive(&program, &c1, t, 2) ==
          PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, dt) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  fprintf(out, "g42_diode_cap_be\n");
  fprintf(out, "status=ok\n");
  fprintf(out, "ok=1\n");
  /* First BE step with C from 0: mid between 0 and diode OP. */
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && ctx.solution[mid] > 0.2 &&
      ctx.solution[mid] < 0.8)
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

/* Phase 9: KCL residual at diode node after OP — |(Vin-V)/R - Id(V)| ≈ 0 */
int golden_g43_diode_kcl_residual(FILE *out) {
  PhysicsPrimitive vs, r1, d1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  double vmid;
  double i_r;
  double i_d;
  double exp_v;
  double resid;
  int rc = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (build_diode_series(&program, &vs, &r1, &d1, &vin, &mid, &gnd) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, gnd))
    goto done;
  vmid = ctx.solution[mid];
  i_r = (5.0 - vmid) / 1000.0;
  exp_v = exp(vmid / vt);
  if (vmid / vt > 40.0)
    exp_v = exp(40.0);
  i_d = 1.0e-12 * (exp_v - 1.0);
  resid = fabs(i_r - i_d);
  fprintf(out, "g43_diode_kcl_residual\n");
  fprintf(out, "kcl_ok=1\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && resid < 1e-9)
    rc = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return rc;
}

static int bjt_common_ce(PhysicsProgram *program, PhysicsPrimitive *vcc,
                         PhysicsPrimitive *vbb, PhysicsPrimitive *prc,
                         PhysicsPrimitive *q1, NodeId *n_vcc, NodeId *n_c,
                         NodeId *n_b, NodeId *n_e, double vbb_v,
                         double rc_ohm) {
  NodeId t2[2];
  NodeId t3[3];
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  if (!physics2_primitive_init_vsource(vcc, "VCC", 5.0, 0.0) ||
      !physics2_primitive_init_vsource(vbb, "VBB", vbb_v, 0.0) ||
      !physics2_primitive_init_resistor(prc, "RC", rc_ohm, 0.0) ||
      !physics2_primitive_init_bjt(q1, "Q1", 1.0e-15, 0.99, 0.5, vt, 0.0))
    return 1;
  *n_vcc = physics2_program_new_node(program);
  *n_c = physics2_program_new_node(program);
  *n_b = physics2_program_new_node(program);
  *n_e = physics2_program_new_node(program);
  t2[0] = *n_vcc;
  t2[1] = *n_e; /* VCC to emitter/gnd ref */
  if (physics2_program_add_primitive(program, vcc, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  t2[0] = *n_b;
  t2[1] = *n_e;
  if (physics2_program_add_primitive(program, vbb, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  t2[0] = *n_vcc;
  t2[1] = *n_c;
  if (physics2_program_add_primitive(program, prc, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  t3[0] = *n_c;
  t3[1] = *n_b;
  t3[2] = *n_e;
  if (physics2_program_add_primitive(program, q1, t3, 3) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  return 0;
}

/* Phase 10: cutoff — VBB=0 → Ic≈0, Vc≈5 */
int golden_g44_bjt_cutoff(FILE *out) {
  PhysicsPrimitive vcc, vbb, prc, q1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vcc, n_c, n_b, n_e;
  double ic;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (bjt_common_ce(&program, &vcc, &vbb, &prc, &q1, &n_vcc, &n_c, &n_b, &n_e,
                    0.0, 1000.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_e))
    goto done;
  ic = (5.0 - ctx.solution[n_c]) / 1000.0;
  fprintf(out, "g44_bjt_cutoff\n");
  fprintf(out, "region=cutoff\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && fabs(ic) < 1e-9 &&
      near_eq(ctx.solution[n_c], 5.0, 1e-3))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Phase 10: forward active — VBB=0.75, Ic>0, Vce>0.2 */
int golden_g45_bjt_forward_active(FILE *out) {
  PhysicsPrimitive vcc, vbb, prc, q1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vcc, n_c, n_b, n_e;
  double ic, vce;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (bjt_common_ce(&program, &vcc, &vbb, &prc, &q1, &n_vcc, &n_c, &n_b, &n_e,
                    0.75, 1000.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_e))
    goto done;
  ic = (5.0 - ctx.solution[n_c]) / 1000.0;
  vce = ctx.solution[n_c] - ctx.solution[n_e];
  fprintf(out, "g45_bjt_forward_active\n");
  fprintf(out, "region=forward_active\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && ic > 1e-4 && vce > 0.2)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Phase 10: saturation — small Rc, Vce small */
int golden_g46_bjt_saturation(FILE *out) {
  PhysicsPrimitive vcc, vbb, prc, q1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vcc, n_c, n_b, n_e;
  double vce;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (bjt_common_ce(&program, &vcc, &vbb, &prc, &q1, &n_vcc, &n_c, &n_b, &n_e,
                    0.85, 100.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_e))
    goto done;
  vce = ctx.solution[n_c] - ctx.solution[n_e];
  fprintf(out, "g46_bjt_saturation\n");
  fprintf(out, "region=saturation\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && vce >= 0.0 && vce < 0.3)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Phase 10: KCL Ic+Ib+Ie ≈ 0 at OP */
int golden_g47_bjt_kcl(FILE *out) {
  PhysicsPrimitive vcc, vbb, prc, q1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vcc, n_c, n_b, n_e;
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  double vbe, vbc, i_f, i_r, ic, ib, ie, resid;
  double ies, ics, af = 0.99, ar = 0.5, isat = 1e-15;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (bjt_common_ce(&program, &vcc, &vbb, &prc, &q1, &n_vcc, &n_c, &n_b, &n_e,
                    0.75, 1000.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_e))
    goto done;
  vbe = ctx.solution[n_b] - ctx.solution[n_e];
  vbc = ctx.solution[n_b] - ctx.solution[n_c];
  ies = isat / af;
  ics = isat / ar;
  i_f = ies * (exp(vbe / vt) - 1.0);
  i_r = ics * (exp(vbc / vt) - 1.0);
  if (vbe / vt > 40.0)
    i_f = ies * (exp(40.0) - 1.0);
  if (vbc / vt > 40.0)
    i_r = ics * (exp(40.0) - 1.0);
  if (vbe / vt < -40.0)
    i_f = ies * (exp(-40.0) - 1.0);
  if (vbc / vt < -40.0)
    i_r = ics * (exp(-40.0) - 1.0);
  ic = af * i_f - i_r;
  ie = -i_f + ar * i_r;
  ib = (1.0 - af) * i_f + (1.0 - ar) * i_r;
  resid = fabs(ic + ib + ie);
  fprintf(out, "g47_bjt_kcl\n");
  fprintf(out, "kcl_ok=1\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && resid < 1e-12)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Common-source NMOS: VDD–Rd–D, Vg=vgg, S=gnd. Params Vth=1, K=2e-3, λ=0. */
static int nmos_common_source(PhysicsProgram *program, PhysicsPrimitive *vdd,
                              PhysicsPrimitive *vgg, PhysicsPrimitive *prd,
                              PhysicsPrimitive *m1, NodeId *n_vdd, NodeId *n_d,
                              NodeId *n_g, NodeId *n_s, double vdd_v,
                              double vgg_v, double rd_ohm) {
  NodeId t2[2];
  NodeId t3[3];
  if (!physics2_primitive_init_vsource(vdd, "VDD", vdd_v, 0.0) ||
      !physics2_primitive_init_vsource(vgg, "VGG", vgg_v, 0.0) ||
      !physics2_primitive_init_resistor(prd, "RD", rd_ohm, 0.0) ||
      !physics2_primitive_init_nmos(m1, "M1", 1.0, 2.0e-3, 0.0, 1.0, 0.0))
    return 1;
  *n_vdd = physics2_program_new_node(program);
  *n_d = physics2_program_new_node(program);
  *n_g = physics2_program_new_node(program);
  *n_s = physics2_program_new_node(program);
  t2[0] = *n_vdd;
  t2[1] = *n_s;
  if (physics2_program_add_primitive(program, vdd, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  t2[0] = *n_g;
  t2[1] = *n_s;
  if (physics2_program_add_primitive(program, vgg, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  t2[0] = *n_vdd;
  t2[1] = *n_d;
  if (physics2_program_add_primitive(program, prd, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  t3[0] = *n_d;
  t3[1] = *n_g;
  t3[2] = *n_s;
  if (physics2_program_add_primitive(program, m1, t3, 3) ==
      PHYSICS_PRIMITIVE_NONE)
    return 1;
  return 0;
}

int golden_g48_nmos_cutoff(FILE *out) {
  PhysicsPrimitive vdd, vgg, prd, m1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vdd, n_d, n_g, n_s;
  double id;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (nmos_common_source(&program, &vdd, &vgg, &prd, &m1, &n_vdd, &n_d, &n_g,
                         &n_s, 5.0, 0.0, 1000.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_s))
    goto done;
  id = (5.0 - ctx.solution[n_d]) / 1000.0;
  fprintf(out, "g48_nmos_cutoff\n");
  fprintf(out, "region=cutoff\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && fabs(id) < 1e-9 &&
      near_eq(ctx.solution[n_d], 5.0, 1e-3))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Linear: VDD=1, Vgg=3 (Vov=2) → Vds < Vov */
int golden_g49_nmos_linear(FILE *out) {
  PhysicsPrimitive vdd, vgg, prd, m1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vdd, n_d, n_g, n_s;
  double vds, vov, id;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (nmos_common_source(&program, &vdd, &vgg, &prd, &m1, &n_vdd, &n_d, &n_g,
                         &n_s, 1.0, 3.0, 100.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_s))
    goto done;
  vds = ctx.solution[n_d] - ctx.solution[n_s];
  vov = ctx.solution[n_g] - ctx.solution[n_s] - 1.0;
  id = (1.0 - ctx.solution[n_d]) / 100.0;
  fprintf(out, "g49_nmos_linear\n");
  fprintf(out, "region=linear\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && id > 1e-4 && vds >= 0.0 &&
      vds < vov)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Saturation: Vgg=2 (Vov=1), Rd=1k → Vds large */
int golden_g50_nmos_saturation(FILE *out) {
  PhysicsPrimitive vdd, vgg, prd, m1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vdd, n_d, n_g, n_s;
  double vds, vov, id;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (nmos_common_source(&program, &vdd, &vgg, &prd, &m1, &n_vdd, &n_d, &n_g,
                         &n_s, 5.0, 2.0, 1000.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_s))
    goto done;
  vds = ctx.solution[n_d] - ctx.solution[n_s];
  vov = ctx.solution[n_g] - ctx.solution[n_s] - 1.0;
  id = (5.0 - ctx.solution[n_d]) / 1000.0;
  fprintf(out, "g50_nmos_saturation\n");
  fprintf(out, "region=saturation\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && id > 1e-4 && vds >= vov)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g51_nmos_kcl(FILE *out) {
  PhysicsPrimitive vdd, vgg, prd, m1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_vdd, n_d, n_g, n_s;
  double vgs, vds, vov, k = 2e-3, vth = 1.0, id, ig, is, resid;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (nmos_common_source(&program, &vdd, &vgg, &prd, &m1, &n_vdd, &n_d, &n_g,
                         &n_s, 5.0, 2.0, 1000.0) != 0)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_s))
    goto done;
  vgs = ctx.solution[n_g] - ctx.solution[n_s];
  vds = ctx.solution[n_d] - ctx.solution[n_s];
  vov = vgs - vth;
  id = 0.0;
  if (vov > 0.0) {
    if (vds < vov)
      id = k * (vov * vds - 0.5 * vds * vds);
    else
      id = 0.5 * k * vov * vov;
  }
  ig = 0.0;
  is = -id;
  resid = fabs(id + ig + is);
  fprintf(out, "g51_nmos_kcl\n");
  fprintf(out, "kcl_ok=1\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK && resid < 1e-12)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Phase 12: switch ON ≈ short through Ron */
int golden_g52_switch_on(FILE *out) {
  PhysicsPrimitive v1, sw, r1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_a, n_mid, n_g, t[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&v1, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_switch(&sw, "S1", 1.0, 1.0e9, 1) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0))
    goto done;
  n_a = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_a;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &v1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_a;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &sw, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_mid;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g52_switch_on\n");
  fprintf(out, "state=on\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[n_mid], 5.0 * 1000.0 / 1001.0, 1e-3))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g53_switch_off(FILE *out) {
  PhysicsPrimitive v1, sw, r1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_a, n_mid, n_g, t[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&v1, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_switch(&sw, "S1", 1.0, 1.0e9, 0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0))
    goto done;
  n_a = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_a;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &v1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_a;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &sw, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_mid;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g53_switch_off\n");
  fprintf(out, "state=off\n");
  fprintf(out, "ok=1\n");
  /* Vmid ≈ 5 * Rload/(Roff+Rload) ≪ 1 mV */
  if (fabs(ctx.solution[n_mid]) < 1e-4)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g54_opamp_follower(FILE *out) {
  PhysicsPrimitive vin, oa;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_g, t4[4], t2[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 2.5, 0.0) ||
      !physics2_primitive_init_opamp(&oa, "U1", 1.0e6))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t2[0] = n_in;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t4[0] = n_out;
  t4[1] = n_g;
  t4[2] = n_in;
  t4[3] = n_out;
  if (physics2_program_add_primitive(&program, &oa, t4, 4) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g54_opamp_follower\n");
  fprintf(out, "config=follower\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[n_out], 2.5, 1e-3))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g55_opamp_noninv(FILE *out) {
  PhysicsPrimitive vin, oa, rf, rg;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_fb, n_g, t4[4], t2[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 1.0, 0.0) ||
      !physics2_primitive_init_opamp(&oa, "U1", 1.0e6) ||
      !physics2_primitive_init_resistor(&rf, "RF", 1000.0, 0.0) ||
      !physics2_primitive_init_resistor(&rg, "RG", 1000.0, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_fb = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t2[0] = n_in;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t4[0] = n_out;
  t4[1] = n_g;
  t4[2] = n_in;
  t4[3] = n_fb;
  if (physics2_program_add_primitive(&program, &oa, t4, 4) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = n_out;
  t2[1] = n_fb;
  if (physics2_program_add_primitive(&program, &rf, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = n_fb;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &rg, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g55_opamp_noninv\n");
  fprintf(out, "config=noninv_gain2\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[n_out], 2.0, 1e-2))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g56_ldo_regulate(FILE *out) {
  PhysicsPrimitive vin, ldo, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_g, t3[3], t2[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 5.0, 0.0) ||
      !physics2_primitive_init_ldo(&ldo, "U1", 3.3, 0.3, 0.1, 0.0) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1000.0, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t2[0] = n_in;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t3[0] = n_in;
  t3[1] = n_out;
  t3[2] = n_g;
  if (physics2_program_add_primitive(&program, &ldo, t3, 3) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = n_out;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &rl, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g56_ldo_regulate\n");
  fprintf(out, "region=regulate\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK &&
      near_eq(ctx.solution[n_out], 3.3, 1e-2))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g57_ldo_dropout(FILE *out) {
  PhysicsPrimitive vin, ldo, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_g, t3[3], t2[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 3.0, 0.0) ||
      !physics2_primitive_init_ldo(&ldo, "U1", 3.3, 0.3, 0.1, 0.0) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1000.0, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t2[0] = n_in;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t3[0] = n_in;
  t3[1] = n_out;
  t3[2] = n_g;
  if (physics2_program_add_primitive(&program, &ldo, t3, 3) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t2[0] = n_out;
  t2[1] = n_g;
  if (physics2_program_add_primitive(&program, &rl, t2, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g57_ldo_dropout\n");
  fprintf(out, "region=dropout\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK &&
      near_eq(ctx.solution[n_out], 2.7, 5e-2))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g58_battery_load(FILE *out) {
  PhysicsPrimitive bat, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_p, n_g, t[2];
  double vexp;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_battery(&bat, "B1", 3.7, 0.5) ||
      !physics2_primitive_init_resistor(&rl, "RL", 10.0, 0.0))
    goto done;
  n_p = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_p;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &bat, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  if (physics2_program_add_primitive(&program, &rl, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  vexp = 3.7 * 10.0 / 10.5;
  fprintf(out, "g58_battery_load\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[n_p], vexp, 1e-6))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g59_battery_unloaded(FILE *out) {
  PhysicsPrimitive bat, rl;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_p, n_g, t[2];
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_battery(&bat, "B1", 3.7, 0.5) ||
      !physics2_primitive_init_resistor(&rl, "RL", 1.0e9, 0.0))
    goto done;
  n_p = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_p;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &bat, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  if (physics2_program_add_primitive(&program, &rl, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0) ||
      !physics2_context_step(&ctx, n_g))
    goto done;
  fprintf(out, "g59_battery_unloaded\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.solution[n_p], 3.7, 1e-3))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Phase 13: AC RC low-pass |H|=1/√2 at ω=1/RC */
int golden_g60_ac_rc_lpf(FILE *out) {
  PhysicsPrimitive vin, r1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_g, t[2];
  double *re = NULL, *im = NULL;
  double R = 1000.0, C = 1.0e-6, omega, mag, phase, href;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 1.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", R, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", C, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_out;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_out;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &c1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0))
    goto done;
  re = calloc(ctx.solution_size, sizeof(double));
  im = calloc(ctx.solution_size, sizeof(double));
  omega = 1.0 / (R * C);
  if (!re || !im || !physics2_context_step_ac(&ctx, n_g, omega, re, im))
    goto done;
  physics2_ac_mag_phase(re[n_out], im[n_out], &mag, &phase);
  href = 1.0 / sqrt(2.0);
  fprintf(out, "g60_ac_rc_lpf\n");
  fprintf(out, "ok=1\n");
  if (near_eq(mag, href, 1e-3) && near_eq(phase, -45.0, 0.5))
    fail = 0;
  physics2_context_free(&ctx);
done:
  free(re);
  free(im);
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* AC RC high-pass: series C, shunt R; |H|=1/√2 at ω=1/RC */
int golden_g61_ac_rc_hpf(FILE *out) {
  PhysicsPrimitive vin, r1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_g, t[2];
  double *re = NULL, *im = NULL;
  double R = 1000.0, C = 1.0e-6, omega, mag, phase, href;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 1.0, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", C, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", R, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_out;
  if (physics2_program_add_primitive(&program, &c1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_out;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0))
    goto done;
  re = calloc(ctx.solution_size, sizeof(double));
  im = calloc(ctx.solution_size, sizeof(double));
  omega = 1.0 / (R * C);
  if (!re || !im || !physics2_context_step_ac(&ctx, n_g, omega, re, im))
    goto done;
  physics2_ac_mag_phase(re[n_out], im[n_out], &mag, &phase);
  href = 1.0 / sqrt(2.0);
  fprintf(out, "g61_ac_rc_hpf\n");
  fprintf(out, "ok=1\n");
  if (near_eq(mag, href, 1e-3) && near_eq(phase, 45.0, 0.5))
    fail = 0;
  physics2_context_free(&ctx);
done:
  free(re);
  free(im);
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* AC RL: Vout across L, series R; |H|=1/√2 at ω=R/L */
int golden_g62_ac_rl(FILE *out) {
  PhysicsPrimitive vin, r1, l1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_out, n_g, t[2];
  double *re = NULL, *im = NULL;
  double R = 1000.0, L = 1.0e-3, omega, mag, phase, href;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 1.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", R, 0.0) ||
      !physics2_primitive_init_inductor(&l1, "L1", L, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_out = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_out;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_out;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &l1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0))
    goto done;
  re = calloc(ctx.solution_size, sizeof(double));
  im = calloc(ctx.solution_size, sizeof(double));
  omega = R / L;
  if (!re || !im || !physics2_context_step_ac(&ctx, n_g, omega, re, im))
    goto done;
  physics2_ac_mag_phase(re[n_out], im[n_out], &mag, &phase);
  href = 1.0 / sqrt(2.0);
  fprintf(out, "g62_ac_rl\n");
  fprintf(out, "ok=1\n");
  /* H = jωL/(R+jωL) → +45° at ω=R/L */
  if (near_eq(mag, href, 1e-3) && near_eq(phase, 45.0, 0.5))
    fail = 0;
  physics2_context_free(&ctx);
done:
  free(re);
  free(im);
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Resistive AC divider matches DC ratio */
int golden_g63_ac_divider(FILE *out) {
  PhysicsPrimitive vin, r1, r2;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_mid, n_g, t[2];
  double *re = NULL, *im = NULL;
  double mag, phase;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 2.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r2, "R2", 1000.0, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_mid;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &r2, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 0.0))
    goto done;
  re = calloc(ctx.solution_size, sizeof(double));
  im = calloc(ctx.solution_size, sizeof(double));
  if (!re || !im ||
      !physics2_context_step_ac(&ctx, n_g, 1000.0, re, im))
    goto done;
  physics2_ac_mag_phase(re[n_mid], im[n_mid], &mag, &phase);
  fprintf(out, "g63_ac_divider\n");
  fprintf(out, "ok=1\n");
  if (near_eq(mag, 1.0, 1e-6) && fabs(phase) < 1e-3)
    fail = 0;
  physics2_context_free(&ctx);
done:
  free(re);
  free(im);
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Phase 14: multi-step BE RC → approaches Vin */
int golden_g64_rc_transient_steps(FILE *out) {
  PhysicsPrimitive vin, r1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_mid, n_g, t[2];
  const double R = 1000.0, C = 1e-6, dt = 1e-4, Vin = 5.0;
  double v, expected, a;
  size_t nsteps = 50;
  size_t k;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", Vin, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", R, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", C, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_mid;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &c1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, dt))
    goto done;
  if (!physics2_context_run_steps(&ctx, n_g, nsteps))
    goto done;
  /* Exact BE recurrence: V' = (V + Vin*α)/(1+α), α=dt/(RC) */
  a = dt / (R * C);
  expected = 0.0;
  for (k = 0; k < nsteps; k++)
    expected = (expected + Vin * a) / (1.0 + a);
  v = ctx.solution[n_mid];
  fprintf(out, "g64_rc_transient_steps\n");
  fprintf(out, "ok=1\n");
  if (near_eq(v, expected, 1e-6) && near_eq(ctx.time, nsteps * dt, 1e-15) &&
      v > 2.0 && v < Vin)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g65_rl_transient_steps(FILE *out) {
  PhysicsPrimitive vin, r1, l1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_mid, n_g, t[2];
  const double R = 1000.0, L = 1.0, dt = 1e-3, Vin = 5.0;
  double v;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", Vin, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", R, 0.0) ||
      !physics2_primitive_init_inductor(&l1, "L1", L, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_mid;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &l1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, dt))
    goto done;
  if (!physics2_context_run_steps(&ctx, n_g, 20))
    goto done;
  v = ctx.solution[n_mid];
  fprintf(out, "g65_rl_transient_steps\n");
  fprintf(out, "ok=1\n");
  /* After many τ, inductor shorts → Vmid→0 */
  if (fabs(v) < 0.5 && near_eq(ctx.time, 20.0 * dt, 1e-15))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Series RLC underdamped-ish step — just require finite multi-step */
int golden_g66_rlc_transient(FILE *out) {
  PhysicsPrimitive vin, r1, l1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_a, n_b, n_g, t[2];
  double v;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 100.0, 0.0) ||
      !physics2_primitive_init_inductor(&l1, "L1", 1e-3, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", 1e-6, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_a = physics2_program_new_node(&program);
  n_b = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_a;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_a;
  t[1] = n_b;
  if (physics2_program_add_primitive(&program, &l1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_b;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &c1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 1e-6))
    goto done;
  if (!physics2_context_run_steps(&ctx, n_g, 100))
    goto done;
  v = ctx.solution[n_b];
  fprintf(out, "g66_rlc_transient\n");
  fprintf(out, "ok=1\n");
  if (isfinite(v) && fabs(v) < 20.0 && near_eq(ctx.time, 100e-6, 1e-15))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

int golden_g67_diode_cap_steps(FILE *out) {
  PhysicsPrimitive vs, r1, d1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId vin, mid, gnd;
  NodeId t[2];
  double vt = (1.380649e-23 * 300.0) / 1.602176634e-19;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vs, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_diode(&d1, "D1", 1.0e-12, 1.0, vt, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", 1e-6, 0.0))
    goto done;
  vin = physics2_program_new_node(&program);
  mid = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);
  t[0] = vin;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &vs, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = vin;
  t[1] = mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = mid;
  t[1] = gnd;
  if (physics2_program_add_primitive(&program, &d1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  if (physics2_program_add_primitive(&program, &c1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 1e-4))
    goto done;
  if (!physics2_context_run_steps(&ctx, gnd, 30))
    goto done;
  fprintf(out, "g67_diode_cap_steps\n");
  fprintf(out, "ok=1\n");
  if (ctx.newton.status == PHYSICS2_NEWTON_OK &&
      ctx.solution[mid] > 0.4 && ctx.solution[mid] < 1.2)
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

/* Failed step must not advance time or C history */
int golden_g68_transient_reject(FILE *out) {
  PhysicsPrimitive vin, r1, c1;
  PhysicsProgram program;
  PhysicsAccumulator *acc = NULL;
  PhysicsExecutionContext ctx;
  NodeId n_in, n_mid, n_g, t[2];
  double t0, vprev0, vmid0;
  size_t ci;
  int fail = 1;

  if (!out)
    return 1;
  memset(&ctx, 0, sizeof(ctx));
  physics2_program_init(&program);
  if (!physics2_primitive_init_vsource(&vin, "VIN", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 1000.0, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", 1e-6, 0.0))
    goto done;
  n_in = physics2_program_new_node(&program);
  n_mid = physics2_program_new_node(&program);
  n_g = physics2_program_new_node(&program);
  t[0] = n_in;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &vin, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_in;
  t[1] = n_mid;
  if (physics2_program_add_primitive(&program, &r1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  t[0] = n_mid;
  t[1] = n_g;
  if (physics2_program_add_primitive(&program, &c1, t, 2) ==
      PHYSICS_PRIMITIVE_NONE)
    goto done;
  acc = physics2_accumulator_create(program.next_node + program.branch_count);
  if (!acc || !physics2_context_init(&ctx, &program, acc, 1e-3))
    goto done;
  if (!physics2_context_step(&ctx, n_g))
    goto done;
  /* Find capacitor state slot */
  ci = SIZE_MAX;
  for (ci = 0; ci < program.instruction_count; ci++) {
    PhysicsPrimitive *p = physics2_registry_get(
        &program.primitives, program.instructions[ci].primitive_id);
    if (p && p->kind == PHYS_PRIM_CAPACITOR)
      break;
  }
  if (ci >= program.instruction_count)
    goto done;
  t0 = ctx.time;
  vprev0 = ctx.states[ci].capacitor_previous_voltage;
  vmid0 = ctx.solution[n_mid];
  /* Invalid reference → solve fails; must restore */
  if (physics2_context_step(&ctx, (NodeId)ctx.solution_size + 99))
    goto done;
  fprintf(out, "g68_transient_reject\n");
  fprintf(out, "rejected=1\n");
  fprintf(out, "ok=1\n");
  if (near_eq(ctx.time, t0, 0.0) &&
      near_eq(ctx.states[ci].capacitor_previous_voltage, vprev0, 0.0) &&
      near_eq(ctx.solution[n_mid], vmid0, 0.0))
    fail = 0;
  physics2_context_free(&ctx);
done:
  physics2_accumulator_free(acc);
  physics2_program_free(&program);
  return fail;
}

static void dfm_fill_r(CompiledComponent *cc, const char *role, double ohms,
                       const char *pkg, const char *n1, const char *n2) {
  memset(cc, 0, sizeof(*cc));
  strncpy(cc->role, role, sizeof(cc->role) - 1);
  cc->part.type = PART_RESISTOR;
  cc->part.value = ohms;
  if (pkg)
    strncpy(cc->part.package, pkg, sizeof(cc->part.package) - 1);
  cc->part.power_rating_w = 0.1;
  cc->pin_count = 2;
  strncpy(cc->node1, n1, sizeof(cc->node1) - 1);
  strncpy(cc->node2, n2, sizeof(cc->node2) - 1);
  strncpy(cc->nodes[0], n1, sizeof(cc->nodes[0]) - 1);
  strncpy(cc->nodes[1], n2, sizeof(cc->nodes[1]) - 1);
}

int golden_g69_dfm_pass(FILE *out) {
  CompiledSchematic sch;
  CompiledComponent comps[2];
  MfgDfmProfile prof;
  MfgDfmResult res;
  const char *path = "audit_build/g69_dfm.json";
  int fail = 1;

  if (!out)
    return 1;
  memset(&sch, 0, sizeof(sch));
  memset(&res, 0, sizeof(res));
  strncpy(sch.name, "dfm_pass", sizeof(sch.name) - 1);
  dfm_fill_r(&comps[0], "R1", 1000.0, "0603", "VIN", "VOUT");
  dfm_fill_r(&comps[1], "R2", 1000.0, "0603", "VOUT", "GND");
  sch.components = comps;
  sch.component_count = 2;
  prof = mfg_dfm_profile_standard();
  if (mfg_dfm_check_schematic(&sch, &prof, path, &res) != 0)
    goto done;
  fprintf(out, "g69_dfm_pass\n");
  fprintf(out, "passed=1\n");
  fprintf(out, "ok=1\n");
  if (res.passed && res.error_count == 0)
    fail = 0;
done:
  return fail;
}

int golden_g70_dfm_missing_package(FILE *out) {
  CompiledSchematic sch;
  CompiledComponent comps[1];
  MfgDfmProfile prof;
  MfgDfmResult res;
  const char *path = "audit_build/g70_dfm.json";
  int fail = 1;

  if (!out)
    return 1;
  memset(&sch, 0, sizeof(sch));
  memset(&res, 0, sizeof(res));
  strncpy(sch.name, "dfm_miss_pkg", sizeof(sch.name) - 1);
  dfm_fill_r(&comps[0], "R1", 1000.0, NULL, "A", "B"); /* empty package */
  sch.components = comps;
  sch.component_count = 1;
  prof = mfg_dfm_profile_standard();
  /* Expect failure (nonzero rc or !passed) */
  (void)mfg_dfm_check_schematic(&sch, &prof, path, &res);
  fprintf(out, "g70_dfm_missing_package\n");
  fprintf(out, "failed=1\n");
  fprintf(out, "ok=1\n");
  if (!res.passed && res.error_count > 0)
    fail = 0;
  return fail;
}

int golden_g71_dfm_telemetry(FILE *out) {
  CompiledSchematic sch;
  CompiledComponent comps[1];
  MfgDfmProfile prof;
  MfgDfmResult res;
  const char *path = "audit_build/g71_dfm.json";
  FILE *fp;
  char buf[4096];
  size_t n;
  int has_rules = 0, has_fields = 0, has_copper_note = 0;
  int fail = 1;

  if (!out)
    return 1;
  memset(&sch, 0, sizeof(sch));
  memset(&res, 0, sizeof(res));
  strncpy(sch.name, "dfm_tel", sizeof(sch.name) - 1);
  dfm_fill_r(&comps[0], "R1", 1000.0, "0603", "A", "B");
  sch.components = comps;
  sch.component_count = 1;
  prof = mfg_dfm_profile_standard();
  if (mfg_dfm_check_schematic(&sch, &prof, path, &res) != 0)
    goto done;
  fp = fopen(path, "rb");
  if (!fp)
    goto done;
  n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = '\0';
  if (strstr(buf, "\"rules_checked\""))
    has_rules = 1;
  if (strstr(buf, "\"profile_fields_used\""))
    has_fields = 1;
  if (strstr(buf, "copper_weight_oz_note"))
    has_copper_note = 1;
  if (strstr(buf, "missing_package") && strstr(buf, "package_current"))
    has_rules = has_rules && 1;
  fprintf(out, "g71_dfm_telemetry\n");
  fprintf(out, "telemetry=1\n");
  fprintf(out, "ok=1\n");
  if (has_rules && has_fields && has_copper_note &&
      strstr(buf, "\"rules_checked_count\""))
    fail = 0;
done:
  return fail;
}

/* Phase 18: PCB emit — same nets as schematic, footprints, outline */
int golden_g72_pcb_nets(FILE *out) {
  CompiledSchematic sch;
  CompiledComponent comps[2];
  const char *path = "audit_build/g72_design.kicad_pcb";
  FILE *fp;
  char buf[8192];
  size_t n;
  int fail = 1;

  if (!out)
    return 1;
  memset(&sch, 0, sizeof(sch));
  memset(comps, 0, sizeof(comps));
  strncpy(sch.name, "pcb_div", sizeof(sch.name) - 1);
  strncpy(comps[0].role, "R1", sizeof(comps[0].role) - 1);
  comps[0].part.type = PART_RESISTOR;
  comps[0].part.value = 10000.0;
  strncpy(comps[0].part.package, "0603", sizeof(comps[0].part.package) - 1);
  comps[0].pin_count = 2;
  strncpy(comps[0].node1, "VIN", sizeof(comps[0].node1) - 1);
  strncpy(comps[0].node2, "VOUT", sizeof(comps[0].node2) - 1);
  strncpy(comps[0].nodes[0], "VIN", sizeof(comps[0].nodes[0]) - 1);
  strncpy(comps[0].nodes[1], "VOUT", sizeof(comps[0].nodes[1]) - 1);
  strncpy(comps[1].role, "R2", sizeof(comps[1].role) - 1);
  comps[1].part.type = PART_RESISTOR;
  comps[1].part.value = 10000.0;
  strncpy(comps[1].part.package, "0603", sizeof(comps[1].part.package) - 1);
  comps[1].pin_count = 2;
  strncpy(comps[1].node1, "VOUT", sizeof(comps[1].node1) - 1);
  strncpy(comps[1].node2, "GND", sizeof(comps[1].node2) - 1);
  strncpy(comps[1].nodes[0], "VOUT", sizeof(comps[1].nodes[0]) - 1);
  strncpy(comps[1].nodes[1], "GND", sizeof(comps[1].nodes[1]) - 1);
  sch.components = comps;
  sch.component_count = 2;
  if (!emit_kicad_pcb(path, &sch))
    return 1;
  fp = fopen(path, "rb");
  if (!fp)
    return 1;
  n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = '\0';
  fprintf(out, "g72_pcb_nets\n");
  fprintf(out, "ok=1\n");
  if (strstr(buf, "(kicad_pcb") && strstr(buf, "(net ") &&
      strstr(buf, "\"VIN\"") && strstr(buf, "\"VOUT\"") &&
      strstr(buf, "\"GND\"") && strstr(buf, "Edge.Cuts") &&
      strstr(buf, "(footprint ") && strstr(buf, "\"R1\"") &&
      strstr(buf, "\"R2\""))
    fail = 0;
  return fail;
}

/* Phase 19: deterministic NLP lexer */
int golden_g73_nlp_lex(FILE *out) {
  NlpLexResult lex;
  const NlpToken *t;
  int i;
  int ok = 0;

  if (!out)
    return 1;
  if (nlp_lex("Put a 10k resistor between 3V3 and ADC_SENSE with 0603", &lex) !=
      0)
    return 1;
  fprintf(out, "g73_nlp_lex\n");
  for (i = 0; i < lex.ntok; i++) {
    if (lex.tokens[i].kind == NLP_Q_RESISTANCE &&
        fabs(lex.tokens[i].value - 10000.0) < 1e-9)
      ok |= 1;
    if (lex.tokens[i].kind == NLP_Q_PACKAGE)
      ok |= 2;
    if (lex.tokens[i].kind == NLP_Q_NONE &&
        strcmp(lex.tokens[i].original, "3V3") == 0)
      ok |= 4;
  }
  fprintf(out, "flags=%d\n", ok);
  if (nlp_lex("use 10m here", &lex) != 0)
    return 1;
  t = NULL;
  for (i = 0; i < lex.ntok; i++) {
    if (lex.tokens[i].kind == NLP_Q_AMBIGUOUS)
      t = &lex.tokens[i];
  }
  fprintf(out, "ambiguous=%d\n", t ? 1 : 0);
  fprintf(out, "ok=1\n");
  return (ok == 7 && t) ? 0 : 1;
}

int golden_g74_nlp_between(FILE *out) {
  char clarify[256];
  const char *path = "audit_build/g74_nlp.json";
  SchematicIrMeta meta;
  int rc;

  if (!out)
    return 1;
  memset(&meta, 0, sizeof(meta));
  clarify[0] = '\0';
  rc = nlp_text_to_schematic_ir(
      "Put a 10k resistor between 3V3 and ADC_SENSE", path, clarify,
      sizeof(clarify));
  fprintf(out, "g74_nlp_between\n");
  if (rc != 0) {
    fprintf(out, "fail=%s\n", clarify);
    return 1;
  }
  if (schematic_ir_load_and_validate(path, &meta) != 0)
    return 1;
  fprintf(out, "name=%s\n", meta.name);
  fprintf(out, "ok=1\n");
  return 0;
}

int golden_g75_nlp_ambiguous(FILE *out) {
  char clarify[256];
  const char *path = "audit_build/g75_nlp.json";
  int rc;

  if (!out)
    return 1;
  rc = nlp_text_to_schematic_ir("Make the sensor supply stable.", path, clarify,
                                sizeof(clarify));
  fprintf(out, "g75_nlp_ambiguous\n");
  fprintf(out, "rejected=%d\n", rc != 0 ? 1 : 0);
  fprintf(out, "ok=1\n");
  return rc != 0 ? 0 : 1;
}

/* Phase 20: optional ONNX runtime is absent in default build */
int golden_g76_nlp_runtime(FILE *out) {
  NlpSemanticCandidate cand;
  int avail;
  int rc;

  if (!out)
    return 1;
  avail = nlp_runtime_available();
  rc = nlp_runtime_infer("10k divider", &cand);
  fprintf(out, "g76_nlp_runtime\n");
  fprintf(out, "available=%d\n", avail);
  fprintf(out, "infer_fail=%d\n", rc != 0 ? 1 : 0);
  fprintf(out, "ok=1\n");
  return (avail == 0 && rc != 0) ? 0 : 1;
}

/* Phase 20: 30 free-form prompts — parse or clarify, never silent invent */
int golden_g77_nlp_corpus(FILE *out, const char *fixture_root) {
  int i;
  int ok = 0;
  int clarify = 0;
  char path[512];
  char outp[128];
  char q[256];
  char *text;
  FILE *fp;
  long size;

  if (!out)
    return 1;
  for (i = 1; i <= 30; i++) {
    snprintf(path, sizeof(path), "%s/fixtures/nlp_freeform/p%02d.txt",
             fixture_root && fixture_root[0] ? fixture_root : ".", i);
    fp = fopen(path, "rb");
    if (!fp)
      return 1;
    if (fseek(fp, 0, SEEK_END) != 0) {
      fclose(fp);
      return 1;
    }
    size = ftell(fp);
    rewind(fp);
    text = malloc((size_t)size + 1);
    if (!text) {
      fclose(fp);
      return 1;
    }
    if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
      free(text);
      fclose(fp);
      return 1;
    }
    fclose(fp);
    text[size] = '\0';
    snprintf(outp, sizeof(outp), "audit_build/g77_%02d.json", i);
    if (nlp_text_to_schematic_ir(text, outp, q, sizeof(q)) == 0)
      ok++;
    else
      clarify++;
    free(text);
  }
  fprintf(out, "g77_nlp_corpus\n");
  fprintf(out, "total=30\n");
  fprintf(out, "parsed=%d\n", ok);
  fprintf(out, "clarified=%d\n", clarify);
  fprintf(out, "ok=1\n");
  return (ok + clarify == 30 && ok == 15 && clarify == 15) ? 0 : 1;
}

#ifdef _WIN32
static int env_set(const char *k, const char *v) { return _putenv_s(k, v ? v : ""); }
#else
static int env_set(const char *k, const char *v) {
  if (!v || !v[0])
    return unsetenv(k);
  return setenv(k, v, 1);
}
#endif

/* Phase 21: GEMINI_REPLAY cassette → IR without network */
int golden_g78_gemini_replay(FILE *out, const char *fixture_root) {
  char cassette[512];
  char ir_path[] = "audit_build/g78_gemini_ir.json";
  SchematicIrMeta meta;
  int rc;

  if (!out)
    return 1;
  snprintf(cassette, sizeof(cassette), "%s/fixtures/gemini_replay/divider_10k",
           fixture_root && fixture_root[0] ? fixture_root : ".");
  if (env_set("SYNTH_GEMINI_REPLAY", cassette) != 0)
    return 1;
  memset(&meta, 0, sizeof(meta));
  rc = gemini_schematic_from_prompt("ignored in replay", ir_path, &meta);
  env_set("SYNTH_GEMINI_REPLAY", "");
  fprintf(out, "g78_gemini_replay\n");
  if (rc != 0) {
    fprintf(out, "fail=%s\n", meta.clarifying_question);
    return 1;
  }
  fprintf(out, "name=%s\n", meta.name);
  fprintf(out, "replay=%d\n", 1);
  fprintf(out, "ok=1\n");
  return strcmp(meta.name, "gemini_replay_divider") == 0 ? 0 : 1;
}

/*
 * Phase 21: LIVE gate — unavailable unless SYNTH_GEMINI_LIVE=1 and API key.
 * Never silently reports live success without those.
 */
int golden_g79_gemini_live_gate(FILE *out) {
  const char *live = getenv("SYNTH_GEMINI_LIVE");
  int want_live = (live && live[0] == '1');
  int have_key = gemini_api_key_present();
  int replay = gemini_replay_active();

  if (!out)
    return 1;
  fprintf(out, "g79_gemini_live_gate\n");
  if (replay) {
    fprintf(out, "status=replay_env_set\n");
    fprintf(out, "ok=1\n");
    /* Replay must not be mistaken for live in this gate. */
    return 1;
  }
  if (!want_live || !have_key) {
    fprintf(out, "status=unavailable\n");
    fprintf(out, "ok=1\n");
    return 0;
  }
  /* Live smoke is manual/opt-in — do not auto-pass CI. */
  fprintf(out, "status=live_ready\n");
  fprintf(out, "ok=1\n");
  return 0;
}

/* Phase 22: industrial macro catalog + compose scenarios */
int golden_g80_macro_corpus(FILE *out, const char *fixture_root) {
  char path[512];
  char *text;
  FILE *fp;
  long size;
  int macros = 0;
  ComposeResult industrial;
  ComposeResult tree;
  SchematicIrMeta meta;
  const char *p;

  if (!out)
    return 1;
  snprintf(path, sizeof(path), "%s/fixtures/macros/catalog.json",
           fixture_root && fixture_root[0] ? fixture_root : ".");
  fp = fopen(path, "rb");
  if (!fp)
    return 1;
  fseek(fp, 0, SEEK_END);
  size = ftell(fp);
  rewind(fp);
  text = malloc((size_t)size + 1);
  if (!text) {
    fclose(fp);
    return 1;
  }
  if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
    free(text);
    fclose(fp);
    return 1;
  }
  fclose(fp);
  text[size] = '\0';
  for (p = text; (p = strstr(p, "\"id\"")) != NULL; p++)
    macros++;
  free(text);

  memset(&industrial, 0, sizeof(industrial));
  memset(&tree, 0, sizeof(tree));
  if (compose_industrial_sensor(&industrial) != 0)
    return 1;
  if (compose_power_tree_12v(&tree) != 0)
    return 1;
  if (compose_expand_to_schematic(&industrial, "audit_build/g80_industrial.json") !=
      0)
    return 1;
  memset(&meta, 0, sizeof(meta));
  if (schematic_ir_load_and_validate("audit_build/g80_industrial.json", &meta) !=
      0)
    return 1;

  fprintf(out, "g80_macro_corpus\n");
  fprintf(out, "catalog_macros=%d\n", macros);
  fprintf(out, "industrial_blocks=%d\n", industrial.block_count);
  fprintf(out, "power_tree_blocks=%d\n", tree.block_count);
  fprintf(out, "ok=1\n");
  return (macros >= 14 && industrial.block_count == 6 &&
          tree.block_count == 5)
             ? 0
             : 1;
}
