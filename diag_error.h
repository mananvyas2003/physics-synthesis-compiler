#ifndef DIAG_ERROR_H
#define DIAG_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Shared last-error buffer for seed / IR / compile (single-threaded CLI). */
void diag_set_error(const char *fmt, ...);
const char *diag_last_error(void);
void diag_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif
