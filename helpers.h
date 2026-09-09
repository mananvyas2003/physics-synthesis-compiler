#ifndef HELPERS_H
#define HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

void print_usage(void);

int cmd_db(int argc, char **argv);
int cmd_dfm(int argc, char **argv);
int cmd_compile(int argc, char **argv);
int cmd_physics2(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
