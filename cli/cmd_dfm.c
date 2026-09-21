#include "cli.h"

#include "dfm_compose.h"

#include <stdio.h>
#include <string.h>

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
  DFMPortId source_port;
  DFMPortId sink_port;
  const DFMBlock *source;
  const DFMBlock *sink;

  if (!source_id || !sink_id)
    return 1;

  source_port = DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                            MakeLimits(4.75, 5.25, 0.0, 1.0, 0.0));
  sink_port = DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                          MakeLimits(4.5, 5.5, 0.0, 0.5, 0.0));

  source = DFM_GetBlock(pool, source_id);
  sink = DFM_GetBlock(pool, sink_id);

  if (!source || !sink || !source_port || !sink_port)
    return 1;

  if (!DFM_PortsCompatible(&source->ports[source_port - 1],
                           &sink->ports[sink_port - 1]))
    return 1;

  return 0;
}

static int DFM_TestVoltageMismatch(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "bad_voltage_source");
  DFMBlockId sink_id = DFM_AddBlock(pool, "voltage_sink");
  DFMPortId source_port =
      DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                  MakeLimits(3.0, 3.6, 0.0, 1.0, 0.0));
  DFMPortId sink_port = DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                                    MakeLimits(4.5, 5.5, 0.0, 0.5, 0.0));
  const DFMBlock *source = DFM_GetBlock(pool, source_id);
  const DFMBlock *sink = DFM_GetBlock(pool, sink_id);

  if (!source || !sink || !source_port || !sink_port)
    return 1;

  if (DFM_PortsCompatible(&source->ports[source_port - 1],
                          &sink->ports[sink_port - 1]))
    return 1;

  return 0;
}

static int DFM_TestCurrentMismatch(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "weak_source");
  DFMBlockId sink_id = DFM_AddBlock(pool, "high_current_sink");
  DFMPortId source_port =
      DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                  MakeLimits(4.75, 5.25, 0.0, 0.5, 0.0));
  DFMPortId sink_port = DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                                    MakeLimits(4.5, 5.5, 2.0, 5.0, 0.0));
  const DFMBlock *source = DFM_GetBlock(pool, source_id);
  const DFMBlock *sink = DFM_GetBlock(pool, sink_id);

  if (!source || !sink || !source_port || !sink_port)
    return 1;

  if (DFM_PortsCompatible(&source->ports[source_port - 1],
                          &sink->ports[sink_port - 1]))
    return 1;

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

  if (DFM_PortsCompatible(&ab->ports[a_port - 1], &bb->ports[b_port - 1]))
    return 1;

  return 0;
}

static int DFM_TestGround(DFMBlockPool *pool) {
  DFMBlockId a = DFM_AddBlock(pool, "ground_a");
  DFMBlockId b = DFM_AddBlock(pool, "ground_b");
  DFMPortId a_port =
      DFM_AddPort(pool, a, "gnd", DFM_PORT_GND, MakeLimits(0.0, 0.0, 0.0, 0.0, 0.0));
  DFMPortId b_port =
      DFM_AddPort(pool, b, "gnd", DFM_PORT_GND, MakeLimits(0.0, 0.0, 0.0, 0.0, 0.0));
  const DFMBlock *ab = DFM_GetBlock(pool, a);
  const DFMBlock *bb = DFM_GetBlock(pool, b);

  if (!ab || !bb || !a_port || !b_port)
    return 1;

  if (!DFM_PortsCompatible(&ab->ports[a_port - 1], &bb->ports[b_port - 1]))
    return 1;

  return 0;
}

static int DFM_TestComposition(DFMBlockPool *pool) {
  DFMBlockId source_id = DFM_AddBlock(pool, "composition_source");
  DFMBlockId sink_id = DFM_AddBlock(pool, "composition_sink");
  DFMPortId source_port;
  DFMPortId sink_port;
  DFMComposition composition;
  DFMInstanceId source_instance;
  DFMInstanceId sink_instance;

  source_port = DFM_AddPort(pool, source_id, "power_out", DFM_PORT_POWER_OUT,
                            MakeLimits(4.75, 5.25, 0.0, 1.0, 0.0));
  sink_port = DFM_AddPort(pool, sink_id, "power_in", DFM_PORT_POWER_IN,
                          MakeLimits(4.5, 5.5, 0.0, 0.5, 0.0));

  if (!source_id || !sink_id || !source_port || !sink_port)
    return 1;

  DFM_CompositionInit(&composition);

  source_instance = DFM_AddInstance(&composition, source_id, "PWR1");
  sink_instance = DFM_AddInstance(&composition, sink_id, "U1");

  if (!source_instance || !sink_instance) {
    DFM_CompositionFree(&composition);
    return 1;
  }

  if (!DFM_Connect(&composition, source_instance, source_port, sink_instance,
                   sink_port)) {
    DFM_CompositionFree(&composition);
    return 1;
  }

  if (!DFM_ValidateComposition(pool, &composition)) {
    DFM_CompositionFree(&composition);
    return 1;
  }

  DFM_CompositionFree(&composition);
  return 0;
}

int cmd_dfm_run_suite(void) {
  DFMBlockPool pool;
  int failures = 0;

  DFM_Init(&pool);

  failures += DFM_TestCompatiblePorts(&pool);
  failures += DFM_TestVoltageMismatch(&pool);
  failures += DFM_TestCurrentMismatch(&pool);
  failures += DFM_TestSourceToSource(&pool);
  failures += DFM_TestGround(&pool);
  failures += DFM_TestComposition(&pool);

  DFM_Free(&pool);
  return failures;
}

int cmd_dfm(int argc, char **argv) {
  int failures;

  if (argc < 3 || strcmp(argv[2], "test") != 0) {
    fprintf(stderr, "Usage: synth dfm test\n");
    return 1;
  }

  failures = cmd_dfm_run_suite();
  if (failures != 0) {
    fprintf(stderr, "[DFM] %d test(s) failed\n", failures);
    return 1;
  }

  printf("dfm_failures=0\n");
  return 0;
}
