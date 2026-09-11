#ifndef CLI_H
#define CLI_H

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_DB_PATH "test_board.db"

void print_usage(void);

int cmd_db(int argc, char **argv);
int cmd_dfm(int argc, char **argv);
int cmd_compile(int argc, char **argv);
int cmd_physics2(int argc, char **argv);
int cmd_generate(int argc, char **argv);
int cmd_generate_design(const char *design_json, const char *out_dir);

/* Shared by golden_runner and `synth dfm test`. */
int cmd_dfm_run_suite(void);

const char *cli_fixture_root(void);
char *cli_join_path(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
