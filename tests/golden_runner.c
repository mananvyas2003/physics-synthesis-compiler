#include "golden_cases.h"

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  const char *expected_relpath;
  int (*run)(FILE *out, const char *fixture_root);
} GoldenCase;

static int run_g01(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g01_resistor_stamp(out);
}

static int run_g02(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g02_divider_5v(out);
}

static int run_g03(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g03_three_resistor(out);
}

static int run_g04(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g04_dfm_suite(out);
}

static int run_g05(FILE *out, const char *fixture_root) {
  return golden_g05_compile_divider_sch(out, fixture_root);
}

static int run_g06(FILE *out, const char *fixture_root) {
  return golden_g06_generate_net(out, fixture_root);
}

static int run_g07(FILE *out, const char *fixture_root) {
  return golden_g07_generate_bom(out, fixture_root);
}

static int run_g08(FILE *out, const char *fixture_root) {
  return golden_g08_snapshot(out, fixture_root);
}

static int run_g09(FILE *out, const char *fixture_root) {
  return golden_g09_spec_corpus(out, fixture_root);
}

static int run_g10(FILE *out, const char *fixture_root) {
  return golden_g10_compose_gate4(out, fixture_root);
}

static int run_g11(FILE *out, const char *fixture_root) {
  return golden_g11_bind_rationale(out, fixture_root);
}

static int run_g12(FILE *out, const char *fixture_root) {
  return golden_g12_verify_report(out, fixture_root);
}

static int run_g13(FILE *out, const char *fixture_root) {
  return golden_g13_schematic_corpus(out, fixture_root);
}

static int run_g14(FILE *out, const char *fixture_root) {
  return golden_g14_prompt_generate(out, fixture_root);
}

static int run_g15(FILE *out, const char *fixture_root) {
  return golden_g15_compose_expand_sch(out, fixture_root);
}

static int run_g16(FILE *out, const char *fixture_root) {
  return golden_g16_structural_erc(out, fixture_root);
}

static int run_g17(FILE *out, const char *fixture_root) {
  return golden_g17_led_series(out, fixture_root);
}

static int run_g18(FILE *out, const char *fixture_root) {
  return golden_g18_rc_low_pass(out, fixture_root);
}

static int run_g19(FILE *out, const char *fixture_root) {
  return golden_g19_invalid_role(out, fixture_root);
}

static int run_g20(FILE *out, const char *fixture_root) {
  return golden_g20_rl_low_pass(out, fixture_root);
}

static int run_g21(FILE *out, const char *fixture_root) {
  return golden_g21_ldo_3v3(out, fixture_root);
}

static int run_g22(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g22_diode_newton(out);
}

static int run_g23(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g23_singular_float(out);
}

static int run_g24(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g24_conflict_vsources(out);
}

static int run_g25(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g25_be_capacitor(out);
}

static int run_g26(FILE *out, const char *fixture_root) {
  return golden_g26_903_sensor_power(out, fixture_root);
}

static int run_g27(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g27_isource_resistor(out);
}

static int run_g28(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g28_dc_capacitor_open(out);
}

static int run_g29(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g29_dc_inductor_short(out);
}

static int run_g30(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g30_rl_be_step(out);
}

static int run_g31(FILE *out, const char *fixture_root) {
  return golden_g31_multipin_lib_symbol(out, fixture_root);
}

static int run_g32(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g32_vcvs(out);
}

static int run_g33(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g33_vccs(out);
}

static int run_g34(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g34_ccvs(out);
}

static int run_g35(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g35_cccs(out);
}

static int run_g36(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g36_vcvs_phys_lower(out);
}

static int run_g37(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g37_newton_report(out);
}

static int run_g38(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g38_newton_bad_init(out);
}

static int run_g39(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g39_newton_diverge(out);
}

static int run_g40(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g40_two_diodes(out);
}

static int run_g41(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g41_diode_reverse(out);
}

static int run_g42(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g42_diode_cap_be(out);
}

static int run_g43(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g43_diode_kcl_residual(out);
}

static int run_g44(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g44_bjt_cutoff(out);
}

static int run_g45(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g45_bjt_forward_active(out);
}

static int run_g46(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g46_bjt_saturation(out);
}

static int run_g47(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g47_bjt_kcl(out);
}

static int run_g48(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g48_nmos_cutoff(out);
}

static int run_g49(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g49_nmos_linear(out);
}

static int run_g50(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g50_nmos_saturation(out);
}

static int run_g51(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g51_nmos_kcl(out);
}

static int run_g52(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g52_switch_on(out);
}

static int run_g53(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g53_switch_off(out);
}

static int run_g54(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g54_opamp_follower(out);
}

static int run_g55(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g55_opamp_noninv(out);
}

static int run_g56(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g56_ldo_regulate(out);
}

static int run_g57(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g57_ldo_dropout(out);
}

static int run_g58(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g58_battery_load(out);
}

static int run_g59(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g59_battery_unloaded(out);
}

static int run_g60(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g60_ac_rc_lpf(out);
}

static int run_g61(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g61_ac_rc_hpf(out);
}

static int run_g62(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g62_ac_rl(out);
}

static int run_g63(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g63_ac_divider(out);
}

static int run_g64(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g64_rc_transient_steps(out);
}

static int run_g65(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g65_rl_transient_steps(out);
}

static int run_g66(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g66_rlc_transient(out);
}

static int run_g67(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g67_diode_cap_steps(out);
}

static int run_g68(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g68_transient_reject(out);
}

static int run_g69(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g69_dfm_pass(out);
}

static int run_g70(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g70_dfm_missing_package(out);
}

static int run_g71(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g71_dfm_telemetry(out);
}

static int run_g72(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g72_pcb_nets(out);
}

static int run_g73(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g73_nlp_lex(out);
}

static int run_g74(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g74_nlp_between(out);
}

static int run_g75(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g75_nlp_ambiguous(out);
}

static int run_g76(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g76_nlp_runtime(out);
}

static int run_g77(FILE *out, const char *fixture_root) {
  return golden_g77_nlp_corpus(out, fixture_root);
}

static int run_g78(FILE *out, const char *fixture_root) {
  return golden_g78_gemini_replay(out, fixture_root);
}

static int run_g79(FILE *out, const char *fixture_root) {
  (void)fixture_root;
  return golden_g79_gemini_live_gate(out);
}

static int run_g80(FILE *out, const char *fixture_root) {
  return golden_g80_macro_corpus(out, fixture_root);
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

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  buf = malloc((size_t)size + 1u);
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

static void normalize_newlines(char *text) {
  char *src;
  char *dst;

  if (!text)
    return;

  src = text;
  dst = text;

  while (*src) {
    if (*src == '\r') {
      src++;
      continue;
    }
    *dst++ = *src++;
  }

  *dst = '\0';
}

static int write_temp_and_compare(const GoldenCase *gc, const char *fixture_root,
                                  const char *expected_path) {
  char actual_path[256];
  FILE *fp;
  char *expected;
  char *actual;
  int rc;

  snprintf(actual_path, sizeof(actual_path), "golden_actual_%s.txt", gc->name);

  fp = fopen(actual_path, "wb");
  if (!fp) {
    fprintf(stderr, "[GOLDEN] cannot write %s\n", actual_path);
    return 1;
  }

  rc = gc->run(fp, fixture_root);
  fclose(fp);

  if (rc != 0) {
    fprintf(stderr, "[GOLDEN] %s case execution failed\n", gc->name);
    remove(actual_path);
    return 1;
  }

  expected = read_file(expected_path);
  actual = read_file(actual_path);

  if (!expected || !actual) {
    fprintf(stderr, "[GOLDEN] %s missing expected or actual file\n", gc->name);
    free(expected);
    free(actual);
    remove(actual_path);
    return 1;
  }

  normalize_newlines(expected);
  normalize_newlines(actual);

  if (strcmp(expected, actual) != 0) {
    fprintf(stderr, "[GOLDEN] DIFF %s\n--- expected ---\n%s\n--- actual ---\n%s\n",
            gc->name, expected, actual);
    free(expected);
    free(actual);
    remove(actual_path);
    return 1;
  }

  free(expected);
  free(actual);
  remove(actual_path);
  return 0;
}

int main(int argc, char **argv) {
  const char *fixture_root;
  const GoldenCase cases[] = {
      {"g01_resistor_stamp", "tests/golden/g01_resistor_stamp/expected.txt",
       run_g01},
      {"g02_divider_5v", "tests/golden/g02_divider_5v/expected.txt", run_g02},
      {"g03_three_resistor", "tests/golden/g03_three_resistor/expected.txt",
       run_g03},
      {"g04_dfm_suite", "tests/golden/g04_dfm_suite/expected.txt", run_g04},
      {"g05_compile_divider_sch",
       "tests/golden/g05_compile_divider_sch/expected.txt", run_g05},
      {"g06_generate_net", "tests/golden/g06_generate_net/expected.txt",
       run_g06},
      {"g07_generate_bom", "tests/golden/g07_generate_bom/expected.txt",
       run_g07},
      {"g08_snapshot", "tests/golden/g08_snapshot/expected.txt", run_g08},
      {"g09_spec_corpus", "tests/golden/g09_spec_corpus/expected.txt", run_g09},
      {"g10_compose_gate4", "tests/golden/g10_compose_gate4/expected.txt",
       run_g10},
      {"g11_bind_rationale", "tests/golden/g11_bind_rationale/expected.txt",
       run_g11},
      {"g12_verify_report", "tests/golden/g12_verify_report/expected.txt",
       run_g12},
      {"g13_schematic_corpus", "tests/golden/g13_schematic_corpus/expected.txt",
       run_g13},
      {"g14_prompt_generate", "tests/golden/g14_prompt_generate/expected.txt",
       run_g14},
      {"g15_compose_expand_sch",
       "tests/golden/g15_compose_expand_sch/expected.txt", run_g15},
      {"g16_structural_erc", "tests/golden/g16_structural_erc/expected.txt",
       run_g16},
      {"g17_led_series", "tests/golden/g17_led_series/expected.txt", run_g17},
      {"g18_rc_low_pass", "tests/golden/g18_rc_low_pass/expected.txt", run_g18},
      {"g19_invalid_role", "tests/golden/g19_invalid_role/expected.txt",
       run_g19},
      {"g20_rl_low_pass", "tests/golden/g20_rl_low_pass/expected.txt", run_g20},
      {"g21_ldo_3v3", "tests/golden/g21_ldo_3v3/expected.txt", run_g21},
      {"g22_diode_newton", "tests/golden/g22_diode_newton/expected.txt",
       run_g22},
      {"g23_singular_float", "tests/golden/g23_singular_float/expected.txt",
       run_g23},
      {"g24_conflict_vsources",
       "tests/golden/g24_conflict_vsources/expected.txt", run_g24},
      {"g25_be_capacitor", "tests/golden/g25_be_capacitor/expected.txt",
       run_g25},
      {"g26_903_sensor_power",
       "tests/golden/g26_903_sensor_power/expected.txt", run_g26},
      {"g27_isource_resistor",
       "tests/golden/g27_isource_resistor/expected.txt", run_g27},
      {"g28_dc_capacitor_open",
       "tests/golden/g28_dc_capacitor_open/expected.txt", run_g28},
      {"g29_dc_inductor_short",
       "tests/golden/g29_dc_inductor_short/expected.txt", run_g29},
      {"g30_rl_be_step", "tests/golden/g30_rl_be_step/expected.txt", run_g30},
      {"g31_multipin_lib_symbol",
       "tests/golden/g31_multipin_lib_symbol/expected.txt", run_g31},
      {"g32_vcvs", "tests/golden/g32_vcvs/expected.txt", run_g32},
      {"g33_vccs", "tests/golden/g33_vccs/expected.txt", run_g33},
      {"g34_ccvs", "tests/golden/g34_ccvs/expected.txt", run_g34},
      {"g35_cccs", "tests/golden/g35_cccs/expected.txt", run_g35},
      {"g36_vcvs_phys_lower", "tests/golden/g36_vcvs_phys_lower/expected.txt",
       run_g36},
      {"g37_newton_report", "tests/golden/g37_newton_report/expected.txt",
       run_g37},
      {"g38_newton_bad_init", "tests/golden/g38_newton_bad_init/expected.txt",
       run_g38},
      {"g39_newton_diverge", "tests/golden/g39_newton_diverge/expected.txt",
       run_g39},
      {"g40_two_diodes", "tests/golden/g40_two_diodes/expected.txt", run_g40},
      {"g41_diode_reverse", "tests/golden/g41_diode_reverse/expected.txt",
       run_g41},
      {"g42_diode_cap_be", "tests/golden/g42_diode_cap_be/expected.txt",
       run_g42},
      {"g43_diode_kcl_residual",
       "tests/golden/g43_diode_kcl_residual/expected.txt", run_g43},
      {"g44_bjt_cutoff", "tests/golden/g44_bjt_cutoff/expected.txt", run_g44},
      {"g45_bjt_forward_active",
       "tests/golden/g45_bjt_forward_active/expected.txt", run_g45},
      {"g46_bjt_saturation", "tests/golden/g46_bjt_saturation/expected.txt",
       run_g46},
      {"g47_bjt_kcl", "tests/golden/g47_bjt_kcl/expected.txt", run_g47},
      {"g48_nmos_cutoff", "tests/golden/g48_nmos_cutoff/expected.txt",
       run_g48},
      {"g49_nmos_linear", "tests/golden/g49_nmos_linear/expected.txt",
       run_g49},
      {"g50_nmos_saturation", "tests/golden/g50_nmos_saturation/expected.txt",
       run_g50},
      {"g51_nmos_kcl", "tests/golden/g51_nmos_kcl/expected.txt", run_g51},
      {"g52_switch_on", "tests/golden/g52_switch_on/expected.txt", run_g52},
      {"g53_switch_off", "tests/golden/g53_switch_off/expected.txt", run_g53},
      {"g54_opamp_follower", "tests/golden/g54_opamp_follower/expected.txt",
       run_g54},
      {"g55_opamp_noninv", "tests/golden/g55_opamp_noninv/expected.txt",
       run_g55},
      {"g56_ldo_regulate", "tests/golden/g56_ldo_regulate/expected.txt",
       run_g56},
      {"g57_ldo_dropout", "tests/golden/g57_ldo_dropout/expected.txt",
       run_g57},
      {"g58_battery_load", "tests/golden/g58_battery_load/expected.txt",
       run_g58},
      {"g59_battery_unloaded", "tests/golden/g59_battery_unloaded/expected.txt",
       run_g59},
      {"g60_ac_rc_lpf", "tests/golden/g60_ac_rc_lpf/expected.txt", run_g60},
      {"g61_ac_rc_hpf", "tests/golden/g61_ac_rc_hpf/expected.txt", run_g61},
      {"g62_ac_rl", "tests/golden/g62_ac_rl/expected.txt", run_g62},
      {"g63_ac_divider", "tests/golden/g63_ac_divider/expected.txt", run_g63},
      {"g64_rc_transient_steps",
       "tests/golden/g64_rc_transient_steps/expected.txt", run_g64},
      {"g65_rl_transient_steps",
       "tests/golden/g65_rl_transient_steps/expected.txt", run_g65},
      {"g66_rlc_transient", "tests/golden/g66_rlc_transient/expected.txt",
       run_g66},
      {"g67_diode_cap_steps", "tests/golden/g67_diode_cap_steps/expected.txt",
       run_g67},
      {"g68_transient_reject", "tests/golden/g68_transient_reject/expected.txt",
       run_g68},
      {"g69_dfm_pass", "tests/golden/g69_dfm_pass/expected.txt", run_g69},
      {"g70_dfm_missing_package",
       "tests/golden/g70_dfm_missing_package/expected.txt", run_g70},
      {"g71_dfm_telemetry", "tests/golden/g71_dfm_telemetry/expected.txt",
       run_g71},
      {"g72_pcb_nets", "tests/golden/g72_pcb_nets/expected.txt", run_g72},
      {"g73_nlp_lex", "tests/golden/g73_nlp_lex/expected.txt", run_g73},
      {"g74_nlp_between", "tests/golden/g74_nlp_between/expected.txt", run_g74},
      {"g75_nlp_ambiguous", "tests/golden/g75_nlp_ambiguous/expected.txt",
       run_g75},
      {"g76_nlp_runtime", "tests/golden/g76_nlp_runtime/expected.txt", run_g76},
      {"g77_nlp_corpus", "tests/golden/g77_nlp_corpus/expected.txt", run_g77},
      {"g78_gemini_replay", "tests/golden/g78_gemini_replay/expected.txt",
       run_g78},
      {"g79_gemini_live_gate", "tests/golden/g79_gemini_live_gate/expected.txt",
       run_g79},
      {"g80_macro_corpus", "tests/golden/g80_macro_corpus/expected.txt", run_g80},
  };
  size_t i;
  int failures = 0;

  (void)argc;
  (void)argv;

  fixture_root = cli_fixture_root();

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char *expected_path =
        cli_join_path(fixture_root, cases[i].expected_relpath);

    if (!expected_path) {
      failures++;
      continue;
    }

    if (write_temp_and_compare(&cases[i], fixture_root, expected_path) != 0)
      failures++;
    else
      fprintf(stdout, "GOLDEN_OK %s\n", cases[i].name);

    free(expected_path);
  }

  if (failures != 0) {
    fprintf(stderr, "golden_runner failures=%d\n", failures);
    return 1;
  }

  fprintf(stdout, "golden_runner failures=0\n");
  return 0;
}
