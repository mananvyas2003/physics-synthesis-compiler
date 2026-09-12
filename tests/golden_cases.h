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

#ifdef __cplusplus
}
#endif

#endif
