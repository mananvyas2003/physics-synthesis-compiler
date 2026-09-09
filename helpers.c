#include "helpers.h"

#include "DFM.h"
#include "compiler.h"
#include "db.h"
#include "jlcparts_import.h"
#include "physics2_interpreter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Configuration
 * ============================================================ */

#define DEFAULT_DB_PATH "test_board.db"

/* ============================================================
 * DB helpers
 * ============================================================ */

static int Physics2_Near(double actual, double expected, double tolerance) {
  return fabs(actual - expected) <= tolerance;
}

static DB *OpenDB(const char *path) {
  DB *db = DB_open(path);

  if (!db)
    fprintf(stderr, "[DB] Failed to open %s\n", path);
  else
    printf("[DB] Opened %s\n", path);

  return db;
}

static void PrintCandidates(const char *label, const DBPart *parts, int count) {
  printf("\n%s -> %d candidate(s)\n", label, count);

  for (int i = 0; i < count; ++i) {
    printf("  %d. %-28s value=%-12g package=%-10s "
           "V=%-8.3g I=%-8.3g P=%-8.3g\n",
           i + 1, parts[i].mpn, parts[i].value, parts[i].package,
           parts[i].v_rating, parts[i].i_rating, parts[i].power_rating_w);
  }

  if (count == 0)
    printf("  (none)\n");
}

/* ============================================================
 * Controlled DB regression fixture
 * ============================================================ */

static int InsertDemoResistor(DB *db, const char *mpn, double value,
                              const char *package) {
  int64_t id = 0;

  DBResult r = DB_InsertPart(db, mpn, PART_RESISTOR, value, package, &id);

  if (r == DB_OK || r == DB_DUPLICATE)
    return 0;

  fprintf(stderr, "[DB] Failed to insert %s (result=%d)\n", mpn, r);

  return 1;
}

static int EnsureDemoTopology(DB *db) {
  int64_t top_id = 0;
  int64_t r1_id = 0;
  int64_t r2_id = 0;
  int64_t vin_id = 0;
  int64_t vout_id = 0;
  int64_t gnd_id = 0;

  DBResult r = DB_InsertTopology(db, "resistor_divider",
                                 "Standalone DFM/compiler regression divider",
                                 "Test", &top_id);

  if (r != DB_OK && r != DB_DUPLICATE) {
    fprintf(stderr, "[DB] Could not create topology (result=%d)\n", r);
    return 1;
  }

  TopologyComponentRow components[16];
  TopologyNodeRow nodes[16];
  TopologyConnectionRow connections[32];

  int component_count = 0;
  int node_count = 0;
  int connection_count = 0;

  r = DB_GetTopology(db, "resistor_divider", components, 16, &component_count,
                     nodes, 16, &node_count, connections, 32,
                     &connection_count);

  if (r == DB_OK && component_count >= 2)
    return 0;

  r = DB_AddTopologyComponent(db, top_id, "r1", PART_RESISTOR, 1, &r1_id);

  if (r != DB_OK && r != DB_DUPLICATE)
    return 1;

  r = DB_AddTopologyComponent(db, top_id, "r2", PART_RESISTOR, 1, &r2_id);

  if (r != DB_OK && r != DB_DUPLICATE)
    return 1;

  r = DB_AddTopologyNode(db, top_id, "VIN", &vin_id);

  if (r != DB_OK && r != DB_DUPLICATE)
    return 1;

  r = DB_AddTopologyNode(db, top_id, "VOUT", &vout_id);

  if (r != DB_OK && r != DB_DUPLICATE)
    return 1;

  r = DB_AddTopologyNode(db, top_id, "GND", &gnd_id);

  if (r != DB_OK && r != DB_DUPLICATE)
    return 1;

  /*
   * If duplicate insertion did not return valid IDs,
   * let DB_GetTopology provide the existing topology.
   */
  if (r1_id <= 0 || r2_id <= 0 || vin_id <= 0 || vout_id <= 0 || gnd_id <= 0) {

    printf("[DB] Topology already exists; using stored topology.\n");
    return 0;
  }

  if (DB_AddTopologyConnection(db, r1_id, "1", vin_id, NULL) != DB_OK)
    return 1;

  if (DB_AddTopologyConnection(db, r1_id, "2", vout_id, NULL) != DB_OK)
    return 1;

  if (DB_AddTopologyConnection(db, r2_id, "1", vout_id, NULL) != DB_OK)
    return 1;

  if (DB_AddTopologyConnection(db, r2_id, "2", gnd_id, NULL) != DB_OK)
    return 1;

  return 0;
}

/* ============================================================
 * DB commands
 * ============================================================ */

static int cmd_db_reset(const char *db_path) {
  if (remove(db_path) == 0)
    printf("[PASS] Removed %s\n", db_path);
  else
    printf("[DB] %s did not exist\n", db_path);

  return 0;
}

static int cmd_db_import(const char *csv_path, const char *db_path) {
  printf("\n=========================================\n");
  printf(" CSV -> SQLITE IMPORT\n");
  printf("=========================================\n");
  printf("[DB] CSV    : %s\n", csv_path);
  printf("[DB] Output : %s\n\n", db_path);

  /*
   * Import tests always start with a clean DB.
   */
  remove(db_path);

  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  printf("[IMPORT] Starting JLC PCB CSV import...\n");

  int rows = ImportJLCPCBCsv(db, csv_path);

  printf("[IMPORT] Rows inserted: %d\n", rows);

  DB_close(db);

  if (rows <= 0) {
    fprintf(stderr, "[FAIL] CSV import produced no rows.\n");
    return 1;
  }

  printf("[PASS] CSV -> SQLite import completed.\n");

  return 0;
}

static int cmd_db_inspect(const char *db_path) {
  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  DB_PrintAllParts(db);

  DB_close(db);

  return 0;
}

static int cmd_db_candidates(const char *db_path) {
  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  DBPart candidates[16];

  int count = DB_FindCandidates(db, PART_RESISTOR, 10000.0, "0603",
                                TOLERANCE_E24, 0.0, 0.0, 0.0, candidates, 16);

  PrintCandidates("10k / 0603 resistor search", candidates, count);

  DB_close(db);

  return count > 0 ? 0 : 1;
}

static int cmd_db_topology(const char *db_path) {
  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  TopologyComponentRow components[32];
  TopologyNodeRow nodes[32];
  TopologyConnectionRow connections[64];

  int component_count = 0;
  int node_count = 0;
  int connection_count = 0;

  DBResult r = DB_GetTopology(db, "resistor_divider", components, 32,
                              &component_count, nodes, 32, &node_count,
                              connections, 64, &connection_count);

  printf("\n=========================================\n");
  printf(" TOPOLOGY INSPECTION\n");
  printf("=========================================\n");

  printf("[DB] result      = %d\n", r);
  printf("[DB] components  = %d\n", component_count);
  printf("[DB] nodes       = %d\n", node_count);
  printf("[DB] connections = %d\n\n", connection_count);

  for (int i = 0; i < component_count; ++i) {
    printf("  COMPONENT %-10s type=%d quantity=%d\n", components[i].role_name,
           components[i].part_type, components[i].quantity);
  }

  printf("\n");

  for (int i = 0; i < node_count; ++i) {
    printf("  NODE      %s\n", nodes[i].node_name);
  }

  printf("\n");

  for (int i = 0; i < connection_count; ++i) {
    printf("  CONNECT   %-10s pin=%-4s node=%s\n",
           connections[i].component_role, connections[i].pin_name,
           connections[i].node_name);
  }

  DB_close(db);

  return r == DB_OK ? 0 : 1;
}

/* ============================================================
 * DFM helpers
 * ============================================================ */

static DFMElectricalLimits MakeLimits(double vmin, double vmax, double imin,
                                      double imax, double impedance) {
  DFMElectricalLimits limits;

  limits.voltage.min = vmin;
  limits.voltage.max = vmax;

  limits.current.min = imin;
  limits.current.max = imax;

  limits.impedance_ohms = impedance;

  return limits;
}

static int DFM_TestCompatiblePorts(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "test_source");

  DFMBlockId sink_id = DFM_AddBlock(pool, "test_sink");

  if (!source_id || !sink_id)
    return 1;

  DFMPortId source_port =
      DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                  MakeLimits(4.75, 5.25, 0.0, 1.0, 0.0));

  DFMPortId sink_port =
      DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                  MakeLimits(4.5, 5.5, 0.0, 0.5, 0.0));

  if (!source_port || !sink_port)
    return 1;

  const DFMBlock *source = DFM_GetBlock(pool, source_id);

  const DFMBlock *sink = DFM_GetBlock(pool, sink_id);

  if (!source || !sink)
    return 1;

  const DFMPort *source_p = &source->ports[source_port - 1];

  const DFMPort *sink_p = &sink->ports[sink_port - 1];

  if (!DFM_PortsCompatible(source_p, sink_p)) {
    printf("[FAIL] Valid 5V power connection was rejected.\n");
    return 1;
  }

  printf("[PASS] Compatible power ports accepted.\n");

  return 0;
}

static int DFM_TestVoltageMismatch(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "bad_voltage_source");

  DFMBlockId sink_id = DFM_AddBlock(pool, "voltage_sink");

  DFMPortId source_port =
      DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                  MakeLimits(3.0, 3.6, 0.0, 1.0, 0.0));

  DFMPortId sink_port =
      DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                  MakeLimits(4.5, 5.5, 0.0, 0.5, 0.0));

  const DFMBlock *source = DFM_GetBlock(pool, source_id);

  const DFMBlock *sink = DFM_GetBlock(pool, sink_id);

  if (!source || !sink || !source_port || !sink_port)
    return 1;

  const DFMPort *source_p = &source->ports[source_port - 1];

  const DFMPort *sink_p = &sink->ports[sink_port - 1];

  if (DFM_PortsCompatible(source_p, sink_p)) {
    printf("[FAIL] Voltage mismatch was accepted.\n");
    return 1;
  }

  printf("[PASS] Voltage mismatch rejected.\n");

  return 0;
}

static int DFM_TestCurrentMismatch(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "weak_source");

  DFMBlockId sink_id = DFM_AddBlock(pool, "high_current_sink");

  DFMPortId source_port =
      DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                  MakeLimits(4.75, 5.25, 0.0, 0.5, 0.0));

  DFMPortId sink_port =
      DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                  MakeLimits(4.5, 5.5, 2.0, 5.0, 0.0));

  const DFMBlock *source = DFM_GetBlock(pool, source_id);

  const DFMBlock *sink = DFM_GetBlock(pool, sink_id);

  if (!source || !sink || !source_port || !sink_port)
    return 1;

  const DFMPort *source_p = &source->ports[source_port - 1];

  const DFMPort *sink_p = &sink->ports[sink_port - 1];

  if (DFM_PortsCompatible(source_p, sink_p)) {
    printf("[FAIL] Current mismatch was accepted.\n");
    return 1;
  }

  printf("[PASS] Current-capability mismatch rejected.\n");

  return 0;
}

static int DFM_TestSourceToSource(DFMBlockPool *pool) {
  DFMBlockId a = DFM_AddBlock(pool, "source_a");

  DFMBlockId b = DFM_AddBlock(pool, "source_b");

  DFMPortId a_port = DFM_AddPort(pool, a, "out", DFM_PORT_POWER_OUT,
                                 MakeLimits(0.0, 12.0, 0.0, 1.0, 0.0));

  DFMPortId b_port = DFM_AddPort(pool, b, "out", DFM_PORT_POWER_OUT,
                                 MakeLimits(0.0, 12.0, 0.0, 1.0, 0.0));

  const DFMBlock *ab = DFM_GetBlock(pool, a);

  const DFMBlock *bb = DFM_GetBlock(pool, b);

  if (!ab || !bb || !a_port || !b_port)
    return 1;

  const DFMPort *ap = &ab->ports[a_port - 1];

  const DFMPort *bp = &bb->ports[b_port - 1];

  if (DFM_PortsCompatible(ap, bp)) {
    printf("[FAIL] Source-to-source connection accepted.\n");
    return 1;
  }

  printf("[PASS] Source-to-source connection rejected.\n");

  return 0;
}

static int DFM_TestGround(DFMBlockPool *pool) {
  DFMBlockId a = DFM_AddBlock(pool, "ground_a");

  DFMBlockId b = DFM_AddBlock(pool, "ground_b");

  DFMPortId a_port = DFM_AddPort(pool, a, "gnd", DFM_PORT_GND,
                                 MakeLimits(0.0, 0.0, 0.0, 0.0, 0.0));

  DFMPortId b_port = DFM_AddPort(pool, b, "gnd", DFM_PORT_GND,
                                 MakeLimits(0.0, 0.0, 0.0, 0.0, 0.0));

  const DFMBlock *ab = DFM_GetBlock(pool, a);

  const DFMBlock *bb = DFM_GetBlock(pool, b);

  if (!ab || !bb || !a_port || !b_port)
    return 1;

  const DFMPort *ap = &ab->ports[a_port - 1];

  const DFMPort *bp = &bb->ports[b_port - 1];

  if (!DFM_PortsCompatible(ap, bp)) {
    printf("[FAIL] GND-to-GND connection rejected.\n");
    return 1;
  }

  printf("[PASS] GND-to-GND connection accepted.\n");

  return 0;
}

static int DFM_TestComposition(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "composition_source");

  DFMBlockId sink_id = DFM_AddBlock(pool, "composition_sink");

  DFMPortId source_port =
      DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                  MakeLimits(4.75, 5.25, 0.0, 1.0, 0.0));

  DFMPortId sink_port =
      DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                  MakeLimits(4.5, 5.5, 0.0, 0.5, 0.0));

  if (!source_id || !sink_id || !source_port || !sink_port)
    return 1;

  DFMComposition composition;

  DFM_CompositionInit(&composition);

  DFMInstanceId source_instance =
      DFM_AddInstance(&composition, source_id, "PWR1");

  DFMInstanceId sink_instance = DFM_AddInstance(&composition, sink_id, "U1");

  if (!source_instance || !sink_instance) {
    DFM_CompositionFree(&composition);
    return 1;
  }

  if (!DFM_Connect(&composition, source_instance, source_port, sink_instance,
                   sink_port)) {

    printf("[FAIL] Failed to create composition connection.\n");

    DFM_CompositionFree(&composition);
    return 1;
  }

  if (!DFM_ValidateComposition(pool, &composition)) {

    printf("[FAIL] Valid composition rejected.\n");

    DFM_CompositionFree(&composition);
    return 1;
  }

  printf("[PASS] Valid block composition accepted.\n");

  DFM_CompositionFree(&composition);

  return 0;
}

/* ============================================================
 * DFM command
 * ============================================================ */

static int cmd_dfm_test(void) {
  printf("\n=========================================\n");
  printf(" DFM STRESS TEST\n");
  printf("=========================================\n\n");

  DFMBlockPool pool;

  DFM_Init(&pool);

  int failures = 0;

  failures += DFM_TestCompatiblePorts(&pool);
  failures += DFM_TestVoltageMismatch(&pool);
  failures += DFM_TestCurrentMismatch(&pool);
  failures += DFM_TestSourceToSource(&pool);
  failures += DFM_TestGround(&pool);
  failures += DFM_TestComposition(&pool);

  DFM_Free(&pool);

  printf("\n-----------------------------------------\n");

  if (failures == 0) {
    printf("[PASS] All DFM stress tests passed.\n");
    return 0;
  }

  printf("[FAIL] %d DFM test(s) failed.\n", failures);

  return 1;
}

/* ============================================================
 * Public DB command
 * ============================================================ */

int cmd_db(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage:\n"
                    "  synth db reset [db]\n"
                    "  synth db import <csv> [db]\n"
                    "  synth db inspect [db]\n"
                    "  synth db candidates [db]\n"
                    "  synth db topology [db]\n");

    return 1;
  }

  const char *command = argv[2];

  if (strcmp(command, "reset") == 0) {

    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;

    return cmd_db_reset(db_path);
  }

  if (strcmp(command, "import") == 0) {

    if (argc < 4) {
      fprintf(stderr, "Usage: synth db import <csv> [db]\n");

      return 1;
    }

    const char *csv_path = argv[3];

    const char *db_path = argc >= 5 ? argv[4] : DEFAULT_DB_PATH;

    return cmd_db_import(csv_path, db_path);
  }

  if (strcmp(command, "inspect") == 0) {

    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;

    return cmd_db_inspect(db_path);
  }

  if (strcmp(command, "candidates") == 0) {

    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;

    return cmd_db_candidates(db_path);
  }

  if (strcmp(command, "topology") == 0) {

    const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;

    return cmd_db_topology(db_path);
  }

  fprintf(stderr, "[DB] Unknown command: %s\n", command);

  return 1;
}

/* ============================================================
 * Public DFM command
 * ============================================================ */

int cmd_dfm(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: synth dfm test\n");

    return 1;
  }

  if (strcmp(argv[2], "test") == 0)
    return cmd_dfm_test();

  fprintf(stderr, "[DFM] Unknown command: %s\n", argv[2]);

  return 1;
}

/* ============================================================
 * Public compiler command
 * ============================================================ */

static int cmd_compile_divider(const char *db_path, const char *output) {
  DB *db = OpenDB(db_path);

  if (!db)
    return 1;

  /*
   * Controlled regression topology.
   */
  if (EnsureDemoTopology(db) != 0) {
    DB_close(db);
    return 1;
  }

  CompiledSchematic schematic;

  memset(&schematic, 0, sizeof(schematic));

  printf("\n=========================================\n");
  printf(" COMPILER -> KICAD TEST\n");
  printf("=========================================\n");

  printf("[1] DB -> compiler\n");

  DBResult r =
      compiler_compile_resistor_divider(db, "resistor_divider", &schematic);

  if (r != DB_OK) {
    fprintf(stderr, "[FAIL] Compilation failed: DBResult=%d\n", r);

    compiler_free_schematic(&schematic);
    DB_close(db);

    return 1;
  }

  printf("[PASS] Topology and parts compiled.\n\n");

  compiler_print_schematic(&schematic);

  printf("[2] Compiler -> KiCad 9 schematic\n");

  if (!compiler_write_kicad_sch(output, &schematic)) {

    fprintf(stderr, "[FAIL] KiCad schematic writer failed.\n");

    compiler_free_schematic(&schematic);
    DB_close(db);

    return 1;
  }

  printf("[PASS] Wrote %s\n", output);

  compiler_free_schematic(&schematic);
  DB_close(db);

  return 0;
}

int cmd_compile(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: synth compile divider "
                    "[db] [output.kicad_sch]\n");

    return 1;
  }

  if (strcmp(argv[2], "divider") != 0) {
    fprintf(stderr, "[COMPILER] Unknown target: %s\n", argv[2]);

    return 1;
  }

  const char *db_path = argc >= 4 ? argv[3] : DEFAULT_DB_PATH;

  const char *output =
      argc >= 5 ? argv[4] : "generated_resistor_divider.kicad_sch";

  return cmd_compile_divider(db_path, output);
}

int cmd_physics2(int argc, char **argv) {
  PhysicsPrimitive r1;
  PhysicsPrimitive r2;
  PhysicsPrimitive r3;
  PhysicsPrimitive c1;
  PhysicsPrimitive v1;

  PhysicsProgram program;

  PhysicsAccumulator *accumulator;

  PhysicsExecutionContext context;

  NodeId vin;
  NodeId vout;
  NodeId gnd;

  NodeId terminals[2];

  double expected;
  double actual;

  (void)argc;
  (void)argv;

  printf("\n");
  printf("=========================================\n");
  printf(" PHYSICS2 PHYSICAL REGRESSION SUITE\n");
  printf("=========================================\n");

  /* =====================================================
   * TEST 1
   * Single resistor
   * ===================================================== */

  printf("\n[TEST 1] Single 10k resistor stamp\n");

  physics2_program_init(&program);

  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);

  if (vin == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE)
    goto fail_program;

  if (!physics2_primitive_init_resistor(&r1, "R1", 10000.0, 1.0))
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = vout;

  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);

  if (!accumulator)
    goto fail_program;

  if (!physics2_context_init(&context, &program, accumulator, 1e-3))
    goto fail_accumulator;

  {
    PhysicsInterpreter interpreter;

    if (!physics2_interpreter_init(&interpreter, &program, &context))
      goto fail_context;

    if (!physics2_interpreter_execute(&interpreter))
      goto fail_context;
  }

  expected = 1.0 / 10000.0;

  if (!Physics2_Near(physics2_accumulator_get(accumulator, 0, 0), expected,
                     1e-12) ||
      !Physics2_Near(physics2_accumulator_get(accumulator, 0, 1), -expected,
                     1e-12) ||
      !Physics2_Near(physics2_accumulator_get(accumulator, 1, 0), -expected,
                     1e-12) ||
      !Physics2_Near(physics2_accumulator_get(accumulator, 1, 1), expected,
                     1e-12))
    goto fail_context;

  printf("[PASS] Resistor stamp verified.\n");

  physics2_context_free(&context);
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);

  /* =====================================================
   * TEST 2
   * 10k / 10k voltage divider
   *
   * 10 V source
   * VIN -- R1 -- VOUT -- R2 -- GND
   * ===================================================== */

  printf("\n[TEST 2] 10k / 10k voltage divider\n");

  physics2_program_init(&program);

  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);

  if (vin == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE ||
      gnd == PHYSICS2_NODE_NONE)
    goto fail_program;

  if (!physics2_primitive_init_vsource(&v1, "V1", 10.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 10000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r2, "R2", 10000.0, 0.0))
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = gnd;

  if (physics2_program_add_primitive(&program, &v1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = vout;

  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vout;
  terminals[1] = gnd;

  if (physics2_program_add_primitive(&program, &r2, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);

  if (!accumulator)
    goto fail_program;

  if (!physics2_context_init(&context, &program, accumulator, 1e-3))
    goto fail_accumulator;

  if (!physics2_context_step(&context, gnd))
    goto fail_context;

  actual = context.solution[vout];

  if (!Physics2_Near(actual, 5.0, 1e-10))
    goto fail_context;

  printf("[PASS] Vout = %.10f V (expected 5.0 V)\n", actual);

  physics2_context_free(&context);
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);

  /* =====================================================
   * TEST 3
   * Three-resistor network
   *
   * VIN = 12 V
   *
   *          R1 10k
   * VIN ----/\\/\\/---- VOUT
   *                    |
   *                 R2 20k
   *                    |
   *                 R3 30k
   *                    |
   *                   GND
   *
   * R2 || R3 = 12k
   * Vout = 12 * 12 / 22 = 6.5454545 V
   * ===================================================== */

  printf("\n[TEST 3] Three-resistor load network\n");

  physics2_program_init(&program);

  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);

  if (vin == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE ||
      gnd == PHYSICS2_NODE_NONE)
    goto fail_program;

  if (!physics2_primitive_init_vsource(&v1, "V1", 12.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 10000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r2, "R2", 20000.0, 0.0) ||
      !physics2_primitive_init_resistor(&r3, "R3", 30000.0, 0.0))
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &v1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = vout;
  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vout;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &r2, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vout;
  terminals[1] = gnd;
  if (physics2_program_add_primitive(&program, &r3, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);

  if (!accumulator)
    goto fail_program;

  if (!physics2_context_init(&context, &program, accumulator, 1e-3))
    goto fail_accumulator;

  if (!physics2_context_step(&context, gnd))
    goto fail_context;

  actual = context.solution[vout];
  expected = 12.0 * 12000.0 / (10000.0 + 12000.0);

  if (!Physics2_Near(actual, expected, 1e-10))
    goto fail_context;

  printf("[PASS] Vout = %.10f V (expected %.10f V)\n", actual, expected);

  physics2_context_free(&context);
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);

  /* =====================================================
   * TEST 4
   * RC charging
   *
   * 5 V source
   * R = 10k
   * C = 100 nF
   *
   * tau = RC = 1 ms
   *
   * dt = 0.01 ms
   * 100 steps = 1 tau
   *
   * Backward Euler:
   *
   * Vc[n] -> Vfinal
   * ===================================================== */

  printf("\n[TEST 4] RC charging transient\n");

  physics2_program_init(&program);

  vin = physics2_program_new_node(&program);
  vout = physics2_program_new_node(&program);
  gnd = physics2_program_new_node(&program);

  if (vin == PHYSICS2_NODE_NONE || vout == PHYSICS2_NODE_NONE ||
      gnd == PHYSICS2_NODE_NONE)
    goto fail_program;

  if (!physics2_primitive_init_vsource(&v1, "V1", 5.0, 0.0) ||
      !physics2_primitive_init_resistor(&r1, "R1", 10000.0, 0.0) ||
      !physics2_primitive_init_capacitor(&c1, "C1", 100e-9, 0.0))
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = gnd;

  if (physics2_program_add_primitive(&program, &v1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vin;
  terminals[1] = vout;

  if (physics2_program_add_primitive(&program, &r1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  terminals[0] = vout;
  terminals[1] = gnd;

  if (physics2_program_add_primitive(&program, &c1, terminals, 2) ==
      PHYSICS2_NODE_NONE)
    goto fail_program;

  accumulator =
      physics2_accumulator_create(program.next_node + program.branch_count);

  if (!accumulator)
    goto fail_program;

  if (!physics2_context_init(&context, &program, accumulator, 1e-5))
    goto fail_accumulator;

  for (int step = 0; step < 100; step++) {
    if (!physics2_context_step(&context, gnd))
      goto fail_context;

    if ((step + 1) % 20 == 0) {
      printf("  t = %.6f s, Vc = %.6f V\n", context.time,
             context.solution[vout]);
    }
  }

  actual = context.solution[vout];

  /*
   * Exact RC value at 1 tau:
   *
   * 5 * (1 - exp(-1))
   *
   * Backward Euler is slightly lower, so use
   * a tight but discretization-appropriate tolerance.
   */

  expected = 5.0 * (1.0 - exp(-1.0));

  if (!Physics2_Near(actual, expected, 0.02))
    goto fail_context;

  printf("[PASS] Vc(1 tau) = %.6f V (continuous target %.6f V)\n", actual,
         expected);

  physics2_context_free(&context);
  physics2_accumulator_free(accumulator);
  physics2_program_free(&program);

  printf("\n=========================================\n");
  printf(" ALL PHYSICS2 TESTS PASSED\n");
  printf("=========================================\n");

  return 0;

fail_context:
  physics2_context_free(&context);

fail_accumulator:
  physics2_accumulator_free(accumulator);

fail_program:
  physics2_program_free(&program);

fail:
  fprintf(stderr, "\n[FAIL] Physics2 regression suite failed.\n");

  return 1;
}

/* ============================================================
 * Usage
 * ============================================================ */

void print_usage(void) {
  printf("\n"
         "====================================================\n"
         " Physics2 + DFM + DB + Compiler Test CLI\n"
         "====================================================\n"
         "\n"
         "Database:\n"
         "  synth db reset [db]\n"
         "  synth db import <csv> [db]\n"
         "  synth db inspect [db]\n"
         "  synth db candidates [db]\n"
         "  synth db topology [db]\n"
         "\n"
         "DFM:\n"
         "  synth dfm test\n"
         "\n"
         "Compiler:\n"
         "  synth compile divider [db] [output.kicad_sch]\n"
         "\n"
         "Typical real-database test:\n"
         "  synth db reset\n"
         "  synth db import "
         "jlcpcb-components-basic-preferred.csv\n"
         "  synth db candidates\n"
         "\n"
         "Typical DFM test:\n"
         "  synth dfm test\n"
         "\n"
         "Physics2:\n"
         "  synth physics2\n"
         "\n"

         "Typical compiler regression test:\n"
         "  synth compile divider\n"
         "\n");
}
