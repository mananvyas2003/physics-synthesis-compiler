#include "diag_error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char g_diag_error[1024];

void diag_clear_error(void) { g_diag_error[0] = '\0'; }

void diag_set_error(const char *fmt, ...) {
  va_list ap;
  if (!fmt) {
    g_diag_error[0] = '\0';
    return;
  }
  va_start(ap, fmt);
  vsnprintf(g_diag_error, sizeof(g_diag_error), fmt, ap);
  va_end(ap);
}

const char *diag_last_error(void) {
  return g_diag_error[0] ? g_diag_error : "";
}
