#include "mna.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Physical constants.
 */
static const double MNA_BOLTZMANN = 1.380649e-23;
static const double MNA_ELEMENTARY_CHARGE = 1.602176634e-19;

/*
 * --------------------------------------------------------------------------
 * Utility helpers
 * --------------------------------------------------------------------------
 */

static int valid_node(const MnaCircuit *circuit, size_t node) {
  return circuit != NULL && node <= circuit->node_count;
}

static int grow_array(void **data, size_t *capacity, size_t required_count,
                      size_t element_size) {
  size_t new_capacity;
  size_t bytes;
  void *new_data;

  if (required_count <= *capacity)
    return 1;

  new_capacity = (*capacity == 0) ? 4 : *capacity;

  while (new_capacity < required_count) {
    if (new_capacity > SIZE_MAX / 2)
      return 0;

    new_capacity *= 2;
  }

  if (new_capacity > SIZE_MAX / element_size)
    return 0;

  bytes = new_capacity * element_size;

  new_data = realloc(*data, bytes);

  if (new_data == NULL)
    return 0;

  *data = new_data;
  *capacity = new_capacity;

  return 1;
}

/*
 * --------------------------------------------------------------------------
 * Dynamic matrix
 * --------------------------------------------------------------------------
 */

int mna_matrix_init(MnaMatrix *matrix, size_t rows, size_t cols) {
  size_t element_count;

  if (matrix == NULL)
    return 0;

  memset(matrix, 0, sizeof(*matrix));

  if (rows != 0 && cols > SIZE_MAX / rows)
    return 0;

  element_count = rows * cols;

  matrix->data = calloc(element_count, sizeof(double));

  if (element_count != 0 && matrix->data == NULL)
    return 0;

  matrix->rows = rows;
  matrix->cols = cols;

  return 1;
}

void mna_matrix_free(MnaMatrix *matrix) {
  if (matrix == NULL)
    return;

  free(matrix->data);

  memset(matrix, 0, sizeof(*matrix));
}

void mna_matrix_zero(MnaMatrix *matrix) {
  size_t count;

  if (matrix == NULL || matrix->data == NULL)
    return;

  count = matrix->rows * matrix->cols;

  memset(matrix->data, 0, count * sizeof(double));
}

double *mna_matrix_at(MnaMatrix *matrix, size_t row, size_t col) {
  if (matrix == NULL || matrix->data == NULL || row >= matrix->rows ||
      col >= matrix->cols) {
    return NULL;
  }

  return &matrix->data[row * matrix->cols + col];
}

const double *mna_matrix_at_const(const MnaMatrix *matrix, size_t row,
                                  size_t col) {
  if (matrix == NULL || matrix->data == NULL || row >= matrix->rows ||
      col >= matrix->cols) {
    return NULL;
  }

  return &matrix->data[row * matrix->cols + col];
}

/*
 * --------------------------------------------------------------------------
 * Circuit lifetime
 * --------------------------------------------------------------------------
 */

void mna_circuit_init(MnaCircuit *circuit, size_t node_count) {
  if (circuit == NULL)
    return;

  memset(circuit, 0, sizeof(*circuit));

  circuit->node_count = node_count;
}

void mna_circuit_free(MnaCircuit *circuit) {
  if (circuit == NULL)
    return;

  free(circuit->resistors);
  free(circuit->voltage_sources);
  free(circuit->diodes);

  memset(circuit, 0, sizeof(*circuit));
}

/*
 * --------------------------------------------------------------------------
 * Add elements
 * --------------------------------------------------------------------------
 */

int mna_add_resistor(MnaCircuit *circuit, size_t node_a, size_t node_b,
                     double resistance_ohm) {
  MnaResistor *resistor;

  if (!valid_node(circuit, node_a) || !valid_node(circuit, node_b) ||
      node_a == node_b || !isfinite(resistance_ohm) || resistance_ohm <= 0.0) {
    return 0;
  }

  if (!grow_array((void **)&circuit->resistors, &circuit->resistor_capacity,
                  circuit->resistor_count + 1, sizeof(MnaResistor))) {
    return 0;
  }

  resistor = &circuit->resistors[circuit->resistor_count++];

  resistor->node_a = node_a;
  resistor->node_b = node_b;
  resistor->resistance_ohm = resistance_ohm;

  return 1;
}

int mna_add_voltage_source(MnaCircuit *circuit, size_t node_a, size_t node_b,
                           double voltage_v) {
  MnaVoltageSource *source;

  if (!valid_node(circuit, node_a) || !valid_node(circuit, node_b) ||
      node_a == node_b || !isfinite(voltage_v)) {
    return 0;
  }

  if (!grow_array(
          (void **)&circuit->voltage_sources, &circuit->voltage_source_capacity,
          circuit->voltage_source_count + 1, sizeof(MnaVoltageSource))) {
    return 0;
  }

  source = &circuit->voltage_sources[circuit->voltage_source_count++];

  source->node_a = node_a;
  source->node_b = node_b;
  source->voltage_v = voltage_v;

  return 1;
}

int mna_add_diode(MnaCircuit *circuit, size_t node_anode, size_t node_cathode,
                  double saturation_current_a, double ideality_factor,
                  double temperature_k) {
  MnaDiode *diode;

  if (!valid_node(circuit, node_anode) || !valid_node(circuit, node_cathode) ||
      node_anode == node_cathode) {
    return 0;
  }

  if (!isfinite(saturation_current_a) || saturation_current_a <= 0.0) {
    return 0;
  }

  if (!isfinite(ideality_factor) || ideality_factor <= 0.0) {
    return 0;
  }

  if (!isfinite(temperature_k) || temperature_k <= 0.0) {
    return 0;
  }

  if (!grow_array((void **)&circuit->diodes, &circuit->diode_capacity,
                  circuit->diode_count + 1, sizeof(MnaDiode))) {
    return 0;
  }

  diode = &circuit->diodes[circuit->diode_count++];

  diode->node_anode = node_anode;
  diode->node_cathode = node_cathode;
  diode->saturation_current_a = saturation_current_a;
  diode->ideality_factor = ideality_factor;
  diode->temperature_k = temperature_k;

  return 1;
}

/*
 * --------------------------------------------------------------------------
 * Solver options
 * --------------------------------------------------------------------------
 */

void mna_options_default(MnaOptions *options) {
  if (options == NULL)
    return;

  options->max_iterations = 100;

  options->absolute_current_tolerance_a = 1.0e-12;

  options->relative_tolerance = 1.0e-9;

  options->pivot_tolerance = 1.0e-12;

  /*
   * Nonlinear voltage step limiting.
   *
   * This is a numerical convergence aid, not an electrical component.
   */
  options->maximum_voltage_step_v = 0.25;
}

/*
 * --------------------------------------------------------------------------
 * Result lifetime
 * --------------------------------------------------------------------------
 */

void mna_result_free(MnaResult *result) {
  if (result == NULL)
    return;

  free(result->x);

  memset(result, 0, sizeof(*result));
}

/*
 * --------------------------------------------------------------------------
 * Node/vector helpers
 * --------------------------------------------------------------------------
 */

static double node_value(const MnaCircuit *circuit, const double *x,
                         size_t node) {
  (void)circuit;

  /*
   * Ground is not represented in x.
   */
  if (node == 0)
    return 0.0;

  return x[node - 1];
}

static double *matrix_node_entry(MnaMatrix *matrix, size_t node_a,
                                 size_t node_b) {
  if (node_a == 0)
    return NULL;

  return mna_matrix_at(matrix, node_a - 1, node_b - 1);
}

/*
 * --------------------------------------------------------------------------
 * Device equations
 * --------------------------------------------------------------------------
 */

/*
 * Thermal voltage:
 *
 *     Vt = kT/q
 */
static double diode_thermal_voltage(const MnaDiode *diode) {
  return (MNA_BOLTZMANN * diode->temperature_k) / MNA_ELEMENTARY_CHARGE;
}

/*
 * Evaluate the Shockley equation.
 *
 * The exponential is numerically limited at extreme voltages. Beyond
 * roughly 40 thermal-voltage units this diode is already many orders of
 * magnitude away from the useful operating region, while unclamped
 * exp() can destroy the numerical solve.
 */
static void diode_evaluate(const MnaDiode *diode, double voltage_v,
                           double *current_a, double *conductance_s) {
  double vt;
  double exponent;
  double exp_value;

  vt = diode_thermal_voltage(diode);

  exponent = voltage_v / (diode->ideality_factor * vt);

  if (exponent > 40.0)
    exponent = 40.0;

  if (exponent < -40.0)
    exponent = -40.0;

  exp_value = exp(exponent);

  *current_a = diode->saturation_current_a * (exp_value - 1.0);

  *conductance_s =
      diode->saturation_current_a * exp_value / (diode->ideality_factor * vt);
}

/*
 * --------------------------------------------------------------------------
 * MNA stamping
 * --------------------------------------------------------------------------
 *
 * We use:
 *
 *     A x = z
 *
 * where x contains node voltages followed by voltage-source currents.
 */

/*
 * Resistor stamp:
 *
 *       a       b
 *
 *       +       -
 *       |
 *       G
 *       |
 *       -
 *
 * Matrix contribution:
 *
 *     Aaa += G
 *     Abb += G
 *     Aab -= G
 *     Aba -= G
 */
static void stamp_resistor(MnaMatrix *a, size_t node_a, size_t node_b,
                           double resistance) {
  double g;

  g = 1.0 / resistance;

  if (node_a != 0)
    *mna_matrix_at(a, node_a - 1, node_a - 1) += g;

  if (node_b != 0)
    *mna_matrix_at(a, node_b - 1, node_b - 1) += g;

  if (node_a != 0 && node_b != 0) {
    *mna_matrix_at(a, node_a - 1, node_b - 1) -= g;
    *mna_matrix_at(a, node_b - 1, node_a - 1) -= g;
  }
}

/*
 * Voltage source stamp.
 *
 * Branch current is the additional unknown.
 *
 *     Va - Vb = Vs
 */
static void stamp_voltage_source(MnaMatrix *a, double *rhs, size_t node_a,
                                 size_t node_b, double voltage,
                                 size_t branch_index, size_t node_count) {
  size_t k;

  k = node_count + branch_index;

  if (node_a != 0) {
    *mna_matrix_at(a, node_a - 1, k) += 1.0;
    *mna_matrix_at(a, k, node_a - 1) += 1.0;
  }

  if (node_b != 0) {
    *mna_matrix_at(a, node_b - 1, k) -= 1.0;
    *mna_matrix_at(a, k, node_b - 1) -= 1.0;
  }

  rhs[k] += voltage;
}

/*
 * Nonlinear diode linearization.
 *
 * Original equation:
 *
 *     I(V) = Is (exp(V/(nVt)) - 1)
 *
 * Newton linearization around V_old:
 *
 *     I(V) ~= Gd * V + Ieq
 *
 * where:
 *
 *     Gd  = dI/dV
 *     Ieq = I(Vold) - Gd * Vold
 */
static void stamp_diode(MnaMatrix *a, double *rhs, const MnaDiode *diode,
                        const double *x, size_t node_count) {
  double va;
  double vc;
  double vd;

  double current;
  double conductance;
  double equivalent_current;

  size_t anode;
  size_t cathode;

  (void)node_count;

  anode = diode->node_anode;
  cathode = diode->node_cathode;

  va = node_value(NULL, x, anode);
  vc = node_value(NULL, x, cathode);

  vd = va - vc;

  diode_evaluate(diode, vd, &current, &conductance);

  equivalent_current = current - conductance * vd;

  /*
   * Conductance portion.
   */
  if (anode != 0)
    *mna_matrix_at(a, anode - 1, anode - 1) += conductance;

  if (cathode != 0)
    *mna_matrix_at(a, cathode - 1, cathode - 1) += conductance;

  if (anode != 0 && cathode != 0) {
    *mna_matrix_at(a, anode - 1, cathode - 1) -= conductance;

    *mna_matrix_at(a, cathode - 1, anode - 1) -= conductance;
  }

  /*
   * Equivalent current-source contribution.
   *
   * Diode current is defined from anode -> cathode.
   */
  if (anode != 0)
    rhs[anode - 1] -= equivalent_current;

  if (cathode != 0)
    rhs[cathode - 1] += equivalent_current;
}

/*
 * --------------------------------------------------------------------------
 * Matrix assembly
 * --------------------------------------------------------------------------
 */

static void assemble_linearized_system(const MnaCircuit *circuit,
                                       const double *x_old, MnaMatrix *matrix,
                                       double *rhs) {
  size_t i;

  mna_matrix_zero(matrix);

  memset(rhs, 0, matrix->rows * sizeof(double));

  /*
   * Linear elements.
   */
  for (i = 0; i < circuit->resistor_count; ++i) {
    const MnaResistor *r = &circuit->resistors[i];

    stamp_resistor(matrix, r->node_a, r->node_b, r->resistance_ohm);
  }

  /*
   * Ideal voltage sources add branch-current unknowns.
   */
  for (i = 0; i < circuit->voltage_source_count; ++i) {
    const MnaVoltageSource *source = &circuit->voltage_sources[i];

    stamp_voltage_source(matrix, rhs, source->node_a, source->node_b,
                         source->voltage_v, i, circuit->node_count);
  }

  /*
   * Nonlinear elements are linearized around x_old.
   */
  for (i = 0; i < circuit->diode_count; ++i) {
    stamp_diode(matrix, rhs, &circuit->diodes[i], x_old, circuit->node_count);
  }
}

/*
 * --------------------------------------------------------------------------
 * Dense linear solve
 * --------------------------------------------------------------------------
 *
 * Gaussian elimination with partial pivoting.
 *
 * A is overwritten.
 * rhs is overwritten.
 * x receives the solution.
 */
static int solve_linear_system(MnaMatrix *matrix, double *rhs, double *x,
                               double pivot_tolerance) {
  size_t n;
  size_t i;
  size_t j;
  size_t k;
  size_t pivot_row;

  n = matrix->rows;

  for (k = 0; k < n; ++k) {
    double pivot_abs;
    double row_scale;

    pivot_row = k;
    pivot_abs = 0.0;

    /*
     * Find largest pivot below this diagonal.
     */
    for (i = k; i < n; ++i) {
      double value;

      value = fabs(*mna_matrix_at(matrix, i, k));

      if (value > pivot_abs) {
        pivot_abs = value;
        pivot_row = i;
      }
    }

    /*
     * Scale-aware singularity test.
     */
    row_scale = 0.0;

    for (j = k; j < n; ++j) {
      double value;

      value = fabs(*mna_matrix_at(matrix, pivot_row, j));

      if (value > row_scale)
        row_scale = value;
    }

    if (row_scale == 0.0 || pivot_abs <= pivot_tolerance * row_scale) {
      return 0;
    }

    /*
     * Swap rows.
     */
    if (pivot_row != k) {
      for (j = 0; j < n; ++j) {
        double *a1 = mna_matrix_at(matrix, k, j);

        double *a2 = mna_matrix_at(matrix, pivot_row, j);

        double temporary = *a1;

        *a1 = *a2;
        *a2 = temporary;
      }

      {
        double temporary = rhs[k];

        rhs[k] = rhs[pivot_row];
        rhs[pivot_row] = temporary;
      }
    }

    /*
     * Eliminate below pivot.
     */
    for (i = k + 1; i < n; ++i) {
      double factor;

      factor = *mna_matrix_at(matrix, i, k) / *mna_matrix_at(matrix, k, k);

      *mna_matrix_at(matrix, i, k) = 0.0;

      for (j = k + 1; j < n; ++j) {
        *mna_matrix_at(matrix, i, j) -= factor * *mna_matrix_at(matrix, k, j);
      }

      rhs[i] -= factor * rhs[k];
    }
  }

  /*
   * Back substitution.
   */
  for (i = n; i-- > 0;) {
    double sum;

    sum = rhs[i];

    for (j = i + 1; j < n; ++j) {
      sum -= *mna_matrix_at(matrix, i, j) * x[j];
    }

    x[i] = sum / *mna_matrix_at(matrix, i, i);
  }

  return 1;
}

/*
 * --------------------------------------------------------------------------
 * Exact nonlinear residual evaluation
 * --------------------------------------------------------------------------
 */

static void evaluate_residuals(const MnaCircuit *circuit, const double *x,
                               double *kcl_residuals, double *source_residuals,
                               double *diode_residuals) {
  size_t i;

  /*
   * Zero all residuals.
   *
   * Node residual = sum of currents leaving node.
   */
  memset(kcl_residuals, 0, circuit->node_count * sizeof(double));

  memset(source_residuals, 0, circuit->voltage_source_count * sizeof(double));

  memset(diode_residuals, 0, circuit->diode_count * sizeof(double));

  /*
   * Resistors.
   */
  for (i = 0; i < circuit->resistor_count; ++i) {
    const MnaResistor *r = &circuit->resistors[i];

    double va = node_value(circuit, x, r->node_a);

    double vb = node_value(circuit, x, r->node_b);

    double current = (va - vb) / r->resistance_ohm;

    if (r->node_a != 0)
      kcl_residuals[r->node_a - 1] += current;

    if (r->node_b != 0)
      kcl_residuals[r->node_b - 1] -= current;
  }

  /*
   * Voltage-source branch currents.
   */
  for (i = 0; i < circuit->voltage_source_count; ++i) {
    const MnaVoltageSource *source = &circuit->voltage_sources[i];

    double va = node_value(circuit, x, source->node_a);

    double vb = node_value(circuit, x, source->node_b);

    double branch_current = x[circuit->node_count + i];

    source_residuals[i] = va - vb - source->voltage_v;

    if (source->node_a != 0)
      kcl_residuals[source->node_a - 1] += branch_current;

    if (source->node_b != 0)
      kcl_residuals[source->node_b - 1] -= branch_current;
  }

  /*
   * Exact nonlinear diode current.
   */
  for (i = 0; i < circuit->diode_count; ++i) {
    const MnaDiode *diode = &circuit->diodes[i];

    double va = node_value(circuit, x, diode->node_anode);

    double vc = node_value(circuit, x, diode->node_cathode);

    double current;
    double conductance;

    diode_evaluate(diode, va - vc, &current, &conductance);

    (void)conductance;

    diode_residuals[i] = current;

    if (diode->node_anode != 0)
      kcl_residuals[diode->node_anode - 1] += current;

    if (diode->node_cathode != 0)
      kcl_residuals[diode->node_cathode - 1] -= current;
  }
}

/*
 * --------------------------------------------------------------------------
 * Solver
 * --------------------------------------------------------------------------
 */

void mna_solve(const MnaCircuit *circuit, const MnaOptions *options,
               MnaResult *result) {
  MnaOptions local_options;

  MnaMatrix matrix;

  double *rhs;
  double *x_new;

  double *kcl_residuals;
  double *source_residuals;
  double *diode_residuals;

  size_t unknown_count;
  size_t iteration;

  double max_voltage_step;

  if (result == NULL)
    return;

  mna_result_free(result);

  if (circuit == NULL) {
    result->status = MNA_STATUS_INVALID_ARGUMENT;

    return;
  }

  if (options == NULL) {
    mna_options_default(&local_options);
    options = &local_options;
  }

  unknown_count = circuit->node_count + circuit->voltage_source_count;

  result->unknown_count = unknown_count;

  /*
   * No unknowns is technically a valid empty system.
   */
  if (unknown_count == 0) {
    result->status = MNA_STATUS_SUCCESS;

    return;
  }

  result->x = calloc(unknown_count, sizeof(double));

  if (result->x == NULL) {
    result->status = MNA_STATUS_ALLOCATION_FAILURE;

    return;
  }

  if (!mna_matrix_init(&matrix, unknown_count, unknown_count)) {
    result->status = MNA_STATUS_ALLOCATION_FAILURE;

    free(result->x);
    result->x = NULL;

    return;
  }

  rhs = calloc(unknown_count, sizeof(double));

  x_new = calloc(unknown_count, sizeof(double));

  kcl_residuals = calloc(circuit->node_count, sizeof(double));

  source_residuals = calloc(circuit->voltage_source_count, sizeof(double));

  diode_residuals = calloc(circuit->diode_count, sizeof(double));

  if ((rhs == NULL && unknown_count != 0) ||
      (x_new == NULL && unknown_count != 0) ||
      (kcl_residuals == NULL && circuit->node_count != 0) ||
      (source_residuals == NULL && circuit->voltage_source_count != 0) ||
      (diode_residuals == NULL && circuit->diode_count != 0)) {
    result->status = MNA_STATUS_ALLOCATION_FAILURE;

    free(rhs);
    free(x_new);
    free(kcl_residuals);
    free(source_residuals);
    free(diode_residuals);

    mna_matrix_free(&matrix);

    return;
  }

  max_voltage_step = DBL_MAX;

  for (iteration = 0; iteration < options->max_iterations; ++iteration) {
    size_t i;

    /*
     * Build J*x = rhs for the current Newton point.
     */
    assemble_linearized_system(circuit, result->x, &matrix, rhs);

    /*
     * Gaussian elimination destroys matrix/rhs.
     * That's okay because the entire linearized system gets rebuilt
     * next Newton iteration.
     */
    if (!solve_linear_system(&matrix, rhs, x_new, options->pivot_tolerance)) {
      result->status = MNA_STATUS_SINGULAR_MATRIX;

      break;
    }

    /*
     * Compute Newton step.
     */
    max_voltage_step = 0.0;

    for (i = 0; i < circuit->node_count; ++i) {
      double delta = x_new[i] - result->x[i];

      double absolute_delta = fabs(delta);

      if (absolute_delta > max_voltage_step)
        max_voltage_step = absolute_delta;
    }

    /*
     * Voltage limiting.
     *
     * If Newton requests an absurdly large voltage jump,
     * scale the entire update.
     */
    if (options->maximum_voltage_step_v > 0.0 &&
        max_voltage_step > options->maximum_voltage_step_v) {
      double scale = options->maximum_voltage_step_v / max_voltage_step;

      for (i = 0; i < unknown_count; ++i) {
        x_new[i] = result->x[i] + scale * (x_new[i] - result->x[i]);
      }

      max_voltage_step = options->maximum_voltage_step_v;
    }

    /*
     * Accept Newton point.
     */
    memcpy(result->x, x_new, unknown_count * sizeof(double));

    /*
     * Evaluate the ORIGINAL nonlinear equations.
     */
    evaluate_residuals(circuit, result->x, kcl_residuals, source_residuals,
                       diode_residuals);

    {
      double max_kcl = 0.0;
      double max_source = 0.0;
      double current_scale = 0.0;
      double voltage_scale = 0.0;

      for (i = 0; i < circuit->node_count; ++i) {
        double value = fabs(kcl_residuals[i]);

        if (value > max_kcl)
          max_kcl = value;

        if (value > current_scale)
          current_scale = value;
      }

      for (i = 0; i < circuit->voltage_source_count; ++i) {
        double value = fabs(source_residuals[i]);

        if (value > max_source)
          max_source = value;

        if (value > voltage_scale)
          voltage_scale = value;
      }

      result->maximum_kcl_residual_a = max_kcl;

      result->maximum_voltage_step_v = max_voltage_step;

      result->iterations = iteration + 1;

      if (voltage_scale < 1.0)
        voltage_scale = 1.0;

      if (max_kcl <=
              options->absolute_current_tolerance_a +
                  options->relative_tolerance * (current_scale + 1.0e-30) &&
          max_source <= options->relative_tolerance * voltage_scale + 1.0e-12) {
        result->status = MNA_STATUS_SUCCESS;

        break;
      }
    }
  }

  if (iteration >= options->max_iterations && result->status == 0) {
    result->status = MNA_STATUS_NOT_CONVERGED;
  }

  free(rhs);
  free(x_new);
  free(kcl_residuals);
  free(source_residuals);
  free(diode_residuals);

  mna_matrix_free(&matrix);
}

/*
 * --------------------------------------------------------------------------
 * Result accessors
 * --------------------------------------------------------------------------
 */

double mna_node_voltage(const MnaResult *result, size_t node) {
  if (result == NULL || result->x == NULL || node == 0 ||
      node > result->unknown_count) {
    return 0.0;
  }

  return result->x[node - 1];
}

double mna_voltage_source_current(const MnaCircuit *circuit,
                                  const MnaResult *result,
                                  size_t source_index) {
  if (circuit == NULL || result == NULL || result->x == NULL ||
      source_index >= circuit->voltage_source_count) {
    return 0.0;
  }

  return result->x[circuit->node_count + source_index];
}

/*
 * --------------------------------------------------------------------------
 * Independent validation
 * --------------------------------------------------------------------------
 */

int mna_validate(const MnaCircuit *circuit, const MnaResult *result,
                 const MnaOptions *options, MnaValidation *validation) {
  double *kcl_residuals;
  double *source_residuals;
  double *diode_residuals;

  double max_kcl = 0.0;
  double max_source = 0.0;
  double max_diode = 0.0;

  size_t i;

  if (validation != NULL)
    memset(validation, 0, sizeof(*validation));

  if (circuit == NULL || result == NULL || result->x == NULL ||
      validation == NULL) {
    return 0;
  }

  if (options == NULL) {
    MnaOptions local_options;

    mna_options_default(&local_options);

    return mna_validate(circuit, result, &local_options, validation);
  }

  kcl_residuals = calloc(circuit->node_count, sizeof(double));

  source_residuals = calloc(circuit->voltage_source_count, sizeof(double));

  diode_residuals = calloc(circuit->diode_count, sizeof(double));

  if ((kcl_residuals == NULL && circuit->node_count != 0) ||
      (source_residuals == NULL && circuit->voltage_source_count != 0) ||
      (diode_residuals == NULL && circuit->diode_count != 0)) {
    free(kcl_residuals);
    free(source_residuals);
    free(diode_residuals);

    return 0;
  }

  evaluate_residuals(circuit, result->x, kcl_residuals, source_residuals,
                     diode_residuals);

  for (i = 0; i < circuit->node_count; ++i) {
    double value = fabs(kcl_residuals[i]);

    if (value > max_kcl)
      max_kcl = value;
  }

  for (i = 0; i < circuit->voltage_source_count; ++i) {
    double value = fabs(source_residuals[i]);

    if (value > max_source)
      max_source = value;
  }

  for (i = 0; i < circuit->diode_count; ++i) {
    double value = fabs(diode_residuals[i]);

    if (value > max_diode)
      max_diode = value;
  }

  validation->maximum_kcl_residual_a = max_kcl;
  validation->maximum_voltage_source_residual_v = max_source;
  validation->maximum_diode_residual_a = max_diode;

  /*
   * diode_residuals[] stores device current (for KCL), not a constitutive
   * residual — diode law is enforced via KCL once that current is included.
   * Voltage-source residual is in volts; do not compare to current tolerance.
   */
  {
    const double absolute_voltage_tolerance_v = 1.0e-9;

    (void)max_diode;
    validation->valid =
        (max_kcl <= options->absolute_current_tolerance_a) &&
        (max_source <= absolute_voltage_tolerance_v);
  }

  free(kcl_residuals);
  free(source_residuals);
  free(diode_residuals);

  return validation->valid;
}
