#include "mna.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int close_enough(double a, double b, double tolerance) {
  return fabs(a - b) <= tolerance;
}

/*
 * --------------------------------------------------------------------------
 * Test 1:
 * Dynamic matrix
 * --------------------------------------------------------------------------
 */
static void test_matrix(void) {
  MnaMatrix matrix;

  assert(mna_matrix_init(&matrix, 4, 5));

  assert(matrix.rows == 4);
  assert(matrix.cols == 5);
  assert(matrix.data != NULL);

  *mna_matrix_at(&matrix, 0, 0) = 10.0;
  *mna_matrix_at(&matrix, 1, 2) = 20.0;
  *mna_matrix_at(&matrix, 3, 4) = 30.0;

  assert(close_enough(*mna_matrix_at(&matrix, 0, 0), 10.0, 1.0e-15));

  assert(close_enough(*mna_matrix_at(&matrix, 1, 2), 20.0, 1.0e-15));

  assert(close_enough(*mna_matrix_at(&matrix, 3, 4), 30.0, 1.0e-15));

  mna_matrix_zero(&matrix);

  assert(close_enough(*mna_matrix_at(&matrix, 0, 0), 0.0, 1.0e-15));

  assert(close_enough(*mna_matrix_at(&matrix, 3, 4), 0.0, 1.0e-15));

  mna_matrix_free(&matrix);

  assert(matrix.data == NULL);

  printf("PASS: dynamic matrix\n");
}

/*
 * --------------------------------------------------------------------------
 * Test 2:
 * Pure resistor divider
 *
 *       5 V
 *        |
 *       10k
 *        |
 *       VOUT
 *        |
 *       10k
 *        |
 *       GND
 *
 * Expected:
 *
 *       VOUT = 2.5 V
 * --------------------------------------------------------------------------
 */
static void test_resistor_divider(void) {
  MnaCircuit circuit;
  MnaOptions options;
  MnaResult result = {0};
  MnaValidation validation;

  double vout;

  mna_circuit_init(&circuit, 2);

  assert(mna_add_voltage_source(&circuit, 1, 0, 5.0));

  assert(mna_add_resistor(&circuit, 1, 2, 10000.0));

  assert(mna_add_resistor(&circuit, 2, 0, 10000.0));

  mna_options_default(&options);

  mna_solve(&circuit, &options, &result);

  assert(result.status == MNA_STATUS_SUCCESS);

  vout = mna_node_voltage(&result, 2);

  assert(close_enough(vout, 2.5, 1.0e-9));

  assert(mna_validate(&circuit, &result, &options, &validation));

  assert(validation.valid);

  printf("PASS: resistor divider\n");

  mna_result_free(&result);
  mna_circuit_free(&circuit);
}

/*
 * --------------------------------------------------------------------------
 * Test 3:
 * Nonlinear diode circuit
 *
 *              1k
 *       5 V ---/\/\--- VOUT
 *                         |
 *                        |>
 *                         |
 *                        GND
 *
 * The diode follows:
 *
 *       I = Is * ( exp(V/(n*Vt)) - 1 )
 *
 * Therefore this is NOT a fixed-resistance problem.
 * Newton-Raphson must linearize the diode repeatedly.
 * --------------------------------------------------------------------------
 */
static void test_nonlinear_diode(void) {
  MnaCircuit circuit;
  MnaOptions options;
  MnaResult result = {0};
  MnaValidation validation;

  double vout;
  double current;

  mna_circuit_init(&circuit, 2);

  /*
   * 5 V source.
   */
  assert(mna_add_voltage_source(&circuit, 1, 0, 5.0));

  /*
   * 1 kΩ feed resistor.
   */
  assert(mna_add_resistor(&circuit, 1, 2, 1000.0));

  /*
   * Diode:
   *
   * Is = 1 pA
   * n  = 1
   * T  = 300 K
   */
  assert(mna_add_diode(&circuit, 2, 0, 1.0e-12, 1.0, 300.0));

  mna_options_default(&options);

  options.max_iterations = 100;

  options.absolute_current_tolerance_a = 1.0e-10;

  options.maximum_voltage_step_v = 0.25;

  mna_solve(&circuit, &options, &result);

  assert(result.status == MNA_STATUS_SUCCESS);

  vout = mna_node_voltage(&result, 2);

  current = (5.0 - vout) / 1000.0;

  printf("Diode operating point:\n"
         "    Vout = %.9f V\n"
         "    I    = %.9f A\n"
         "    iterations = %zu\n",
         vout, current, result.iterations);

  /*
   * Approximate expected operating point.
   */
  assert(close_enough(vout, 0.574147, 1.0e-4));

  assert(close_enough(current, 0.00442585, 1.0e-6));

  /*
   * Independent nonlinear validation.
   */
  assert(mna_validate(&circuit, &result, &options, &validation));

  assert(validation.valid);

  assert(validation.maximum_kcl_residual_a < 1.0e-9);

  printf("PASS: nonlinear diode\n");

  mna_result_free(&result);
  mna_circuit_free(&circuit);
}

/*
 * --------------------------------------------------------------------------
 * Test 4:
 * Two nonlinear devices in the same circuit.
 *
 *              R1
 *  5 V -------/\/\------ V1
 *                         |
 *                        D1
 *                         |
 *                        V2
 *                         |
 *                        D2
 *                         |
 *                        GND
 *
 * D1 and D2 are nonlinear simultaneously.
 *
 * This is important because we are testing whether the Newton machinery
 * works for MORE THAN ONE nonlinear device.
 * --------------------------------------------------------------------------
 */
static void test_two_diodes(void) {
  MnaCircuit circuit;
  MnaOptions options;
  MnaResult result = {0};
  MnaValidation validation;

  double v1;
  double v2;

  mna_circuit_init(&circuit, 2);

  assert(mna_add_voltage_source(&circuit, 1, 0, 5.0));

  assert(mna_add_resistor(&circuit, 1, 2, 1000.0));

  /*
   * Two diode junctions in series.
   *
   * D1: node 2 -> ground
   * D2: node 2 -> ground
   *
   * This is intentionally a stress test for multiple nonlinear
   * contributions. It is NOT meant to represent two physically
   * independent series diodes; the next topology test will exercise
   * distinct nodes.
   */
  assert(mna_add_diode(&circuit, 2, 0, 1.0e-12, 1.0, 300.0));

  assert(mna_add_diode(&circuit, 2, 0, 1.0e-12, 1.0, 300.0));

  mna_options_default(&options);

  options.max_iterations = 100;

  mna_solve(&circuit, &options, &result);

  assert(result.status == MNA_STATUS_SUCCESS);

  v1 = mna_node_voltage(&result, 1);
  v2 = mna_node_voltage(&result, 2);

  assert(close_enough(v1, 5.0, 1.0e-9));

  assert(v2 > 0.0);
  assert(v2 < 1.0);

  assert(mna_validate(&circuit, &result, &options, &validation));

  assert(validation.valid);

  printf("PASS: multiple nonlinear devices\n");

  mna_result_free(&result);
  mna_circuit_free(&circuit);
}

/*
 * --------------------------------------------------------------------------
 * Future device tests
 * --------------------------------------------------------------------------
 *
 * These are deliberately NOT called yet.
 *
 * They require actual device models and stamps in mna.c.
 *
 *     test_mosfet();
 *     test_bjt();
 *     test_led();
 *     test_zener();
 *
 * We should NOT add fake APIs merely to make these compile.
 * --------------------------------------------------------------------------
 */

int main(void) {
  test_matrix();

  test_resistor_divider();

  test_nonlinear_diode();

  test_two_diodes();

  printf("\n");
  printf("================================\n");
  printf("MNA TEST SUITE: PASS\n");
  printf("================================\n");

  return 0;
}
