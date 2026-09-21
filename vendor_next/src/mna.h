#ifndef MNA_H
#define MNA_H

#include <stddef.h>

/*
 * --------------------------------------------------------------------------
 * Dynamic dense matrix
 * --------------------------------------------------------------------------
 *
 * Row-major contiguous storage:
 *
 *     data[row * cols + col]
 *
 * This gives us:
 *
 *     - dynamic dimensions
 *     - contiguous numerical storage
 *     - simple cache-friendly access
 *     - no per-row allocation
 */
typedef struct {
  size_t rows;
  size_t cols;
  double *data;

} MnaMatrix;

int mna_matrix_init(MnaMatrix *matrix, size_t rows, size_t cols);

void mna_matrix_free(MnaMatrix *matrix);

void mna_matrix_zero(MnaMatrix *matrix);

double *mna_matrix_at(MnaMatrix *matrix, size_t row, size_t col);

const double *mna_matrix_at_const(const MnaMatrix *matrix, size_t row,
                                  size_t col);

/*
 * --------------------------------------------------------------------------
 * Circuit elements
 * --------------------------------------------------------------------------
 *
 * Node 0 is always ground.
 *
 * Valid circuit nodes are:
 *
 *     0 ... node_count
 *
 * where node 0 = GND.
 */

/*
 * Linear resistor:
 *
 *     I = G * (Va - Vb)
 *
 * where:
 *
 *     G = 1 / R
 */
typedef struct {
  size_t node_a;
  size_t node_b;

  double resistance_ohm;

} MnaResistor;

/*
 * Independent ideal voltage source.
 *
 * Voltage convention:
 *
 *     Va - Vb = voltage
 *
 * The corresponding branch current becomes an additional
 * MNA unknown.
 */
typedef struct {
  size_t node_a;
  size_t node_b;

  double voltage_v;

} MnaVoltageSource;

/*
 * Nonlinear PN-junction diode.
 *
 * Shockley equation:
 *
 *     I = Is * ( exp(Vd / (n * Vt)) - 1 )
 *
 * with:
 *
 *     Vd = Va - Vc
 *     Vt = kT/q
 */
typedef struct {
  size_t node_anode;
  size_t node_cathode;

  double saturation_current_a;
  double ideality_factor;
  double temperature_k;

} MnaDiode;

/*
 * --------------------------------------------------------------------------
 * Circuit container
 * --------------------------------------------------------------------------
 *
 * The arrays grow dynamically as elements are added.
 *
 * The MNA matrix itself is created when solving, because only then do we
 * know the final number of voltage-source branch unknowns.
 */
typedef struct {
  size_t node_count;

  MnaResistor *resistors;
  size_t resistor_count;
  size_t resistor_capacity;

  MnaVoltageSource *voltage_sources;
  size_t voltage_source_count;
  size_t voltage_source_capacity;

  MnaDiode *diodes;
  size_t diode_count;
  size_t diode_capacity;

} MnaCircuit;

void mna_circuit_init(MnaCircuit *circuit, size_t node_count);

void mna_circuit_free(MnaCircuit *circuit);

int mna_add_resistor(MnaCircuit *circuit, size_t node_a, size_t node_b,
                     double resistance_ohm);

int mna_add_voltage_source(MnaCircuit *circuit, size_t node_a, size_t node_b,
                           double voltage_v);

int mna_add_diode(MnaCircuit *circuit, size_t node_anode, size_t node_cathode,
                  double saturation_current_a, double ideality_factor,
                  double temperature_k);

/*
 * --------------------------------------------------------------------------
 * Solver configuration
 * --------------------------------------------------------------------------
 */
typedef struct {
  size_t max_iterations;

  /*
   * Newton convergence:
   *
   * maximum allowed KCL residual is approximately based on this
   * current tolerance.
   */
  double absolute_current_tolerance_a;
  double relative_tolerance;

  /*
   * Gaussian elimination singularity threshold.
   */
  double pivot_tolerance;

  /*
   * Newton damping.
   *
   * Prevents a nonlinear iteration from making an enormous voltage jump.
   */
  double maximum_voltage_step_v;

} MnaOptions;

void mna_options_default(MnaOptions *options);

/*
 * --------------------------------------------------------------------------
 * Solver result
 * --------------------------------------------------------------------------
 *
 * Unknown vector:
 *
 *     [ V1 V2 ... VN Ivs1 Ivs2 ... IvsM ]
 *
 * The node-voltage unknowns come first.
 * Voltage-source branch currents follow.
 */
typedef enum {
  MNA_STATUS_SUCCESS = 0,
  MNA_STATUS_INVALID_ARGUMENT,
  MNA_STATUS_ALLOCATION_FAILURE,
  MNA_STATUS_SINGULAR_MATRIX,
  MNA_STATUS_NOT_CONVERGED

} MnaStatus;

typedef struct {
  MnaStatus status;

  size_t unknown_count;
  double *x;

  size_t iterations;

  /*
   * Maximum voltage step from the last Newton iteration.
   */
  double maximum_voltage_step_v;

  /*
   * Maximum absolute KCL residual from the final state.
   */
  double maximum_kcl_residual_a;

} MnaResult;

void mna_result_free(MnaResult *result);

/*
 * Solve the DC operating point.
 *
 * This function intentionally returns no numerical value directly.
 * All raw unknowns are written into result->x.
 */
void mna_solve(const MnaCircuit *circuit, const MnaOptions *options,
               MnaResult *result);

/*
 * Convenience accessors.
 */
double mna_node_voltage(const MnaResult *result, size_t node);

double mna_voltage_source_current(const MnaCircuit *circuit,
                                  const MnaResult *result, size_t source_index);

/*
 * --------------------------------------------------------------------------
 * Independent validation
 * --------------------------------------------------------------------------
 *
 * Validation evaluates the ORIGINAL physical equations, not the
 * Newton-linearized equations used internally during the solve.
 */
typedef struct {
  int valid;

  double maximum_kcl_residual_a;
  double maximum_voltage_source_residual_v;
  double maximum_diode_residual_a;

} MnaValidation;

int mna_validate(const MnaCircuit *circuit, const MnaResult *result,
                 const MnaOptions *options, MnaValidation *validation);

#endif
