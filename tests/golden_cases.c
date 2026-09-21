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
