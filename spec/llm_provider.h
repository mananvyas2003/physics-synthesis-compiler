#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "spec_load.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpecProvider SpecProvider;

struct SpecProvider {
  const char *name;
  int (*prompt_to_spec)(SpecProvider *self, const char *prompt_path,
                        SpecV1 *out);
};

/* Offline file provider: prompt path ignored; loads paired hand-written spec. */
SpecProvider *spec_file_provider(void);

/*
 * Validate-and-retry: up to 3 attempts. On exhaustion, fills
 * out->clarifying_question and returns 1 (never guesses).
 */
int spec_provider_validate_retry(SpecProvider *provider, const char *prompt_path,
                                 const char *spec_path, SpecV1 *out);

#ifdef __cplusplus
}
#endif

#endif
