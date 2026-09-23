#ifndef GOLDEN_CASES_H
#define GOLDEN_CASES_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write deterministic machine-readable output to `out`. Return 0 on success. */
int golden_g01_resistor_stamp(FILE *out);
int golden_g02_divider_5v(FILE *out);
int golden_g03_three_resistor(FILE *out);
int golden_g04_dfm_suite(FILE *out);
int golden_g05_compile_divider_sch(FILE *out, const char *fixture_root);
int golden_g06_generate_net(FILE *out, const char *fixture_root);
int golden_g07_generate_bom(FILE *out, const char *fixture_root);
int golden_g08_snapshot(FILE *out, const char *fixture_root);
int golden_g09_spec_corpus(FILE *out, const char *fixture_root);
int golden_g10_compose_gate4(FILE *out, const char *fixture_root);
int golden_g11_bind_rationale(FILE *out, const char *fixture_root);
int golden_g12_verify_report(FILE *out, const char *fixture_root);

int golden_g13_schematic_corpus(FILE *out, const char *fixture_root);
int golden_g14_prompt_generate(FILE *out, const char *fixture_root);
int golden_g15_compose_expand_sch(FILE *out, const char *fixture_root);
int golden_g16_structural_erc(FILE *out, const char *fixture_root);
int golden_g17_led_series(FILE *out, const char *fixture_root);
int golden_g18_rc_low_pass(FILE *out, const char *fixture_root);
int golden_g19_invalid_role(FILE *out, const char *fixture_root);
int golden_g20_rl_low_pass(FILE *out, const char *fixture_root);
int golden_g21_ldo_3v3(FILE *out, const char *fixture_root);
int golden_g22_diode_newton(FILE *out);
int golden_g23_singular_float(FILE *out);
int golden_g24_conflict_vsources(FILE *out);
int golden_g25_be_capacitor(FILE *out);
int golden_g26_903_sensor_power(FILE *out, const char *fixture_root);
int golden_g27_isource_resistor(FILE *out);
int golden_g28_dc_capacitor_open(FILE *out);
int golden_g29_dc_inductor_short(FILE *out);
int golden_g30_rl_be_step(FILE *out);
int golden_g31_multipin_lib_symbol(FILE *out, const char *fixture_root);
int golden_g32_vcvs(FILE *out);
int golden_g33_vccs(FILE *out);
int golden_g34_ccvs(FILE *out);
int golden_g35_cccs(FILE *out);
int golden_g37_newton_report(FILE *out);
int golden_g38_newton_bad_init(FILE *out);
int golden_g39_newton_diverge(FILE *out);
int golden_g40_two_diodes(FILE *out);
int golden_g41_diode_reverse(FILE *out);
int golden_g42_diode_cap_be(FILE *out);
int golden_g43_diode_kcl_residual(FILE *out);
int golden_g44_bjt_cutoff(FILE *out);
int golden_g45_bjt_forward_active(FILE *out);
int golden_g46_bjt_saturation(FILE *out);
int golden_g47_bjt_kcl(FILE *out);
int golden_g48_nmos_cutoff(FILE *out);
int golden_g49_nmos_linear(FILE *out);
int golden_g50_nmos_saturation(FILE *out);
int golden_g51_nmos_kcl(FILE *out);
int golden_g52_switch_on(FILE *out);
int golden_g53_switch_off(FILE *out);
int golden_g54_opamp_follower(FILE *out);
int golden_g55_opamp_noninv(FILE *out);
int golden_g56_ldo_regulate(FILE *out);
int golden_g57_ldo_dropout(FILE *out);
int golden_g58_battery_load(FILE *out);
int golden_g59_battery_unloaded(FILE *out);
int golden_g60_ac_rc_lpf(FILE *out);
int golden_g61_ac_rc_hpf(FILE *out);
int golden_g62_ac_rl(FILE *out);
int golden_g63_ac_divider(FILE *out);
int golden_g64_rc_transient_steps(FILE *out);
int golden_g65_rl_transient_steps(FILE *out);
int golden_g66_rlc_transient(FILE *out);
int golden_g67_diode_cap_steps(FILE *out);
int golden_g68_transient_reject(FILE *out);
int golden_g69_dfm_pass(FILE *out);
int golden_g70_dfm_missing_package(FILE *out);
int golden_g71_dfm_telemetry(FILE *out);
int golden_g73_nlp_lex(FILE *out);
int golden_g74_nlp_between(FILE *out);
int golden_g75_nlp_ambiguous(FILE *out);
int golden_g76_nlp_runtime(FILE *out);
int golden_g77_nlp_corpus(FILE *out, const char *fixture_root);
int golden_g80_macro_corpus(FILE *out, const char *fixture_root);
int golden_g81_rail_sources(FILE *out);
int golden_g82_generate_physics(FILE *out, const char *fixture_root);
int golden_g83_nlp_physics(FILE *out, const char *fixture_root);

#ifdef __cplusplus
}
#endif

#endif
