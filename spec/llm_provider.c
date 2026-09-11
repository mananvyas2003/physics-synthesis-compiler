#include "llm_provider.h"

#include "cli.h"

#include <stdio.h>
#include <string.h>

static int file_prompt_to_spec(SpecProvider *self, const char *prompt_path,
                               SpecV1 *out) {
  char paired[512];
  const char *slash;
  const char *base;
  char num[8];
  size_t i;
  size_t n = 0;

  (void)self;
  if (!prompt_path || !out)
    return 1;

  /* Map fixtures/prompts/NNN.txt → fixtures/specs/NNN.json */
  slash = strrchr(prompt_path, '/');
  if (!slash)
    slash = strrchr(prompt_path, '\\');
  base = slash ? slash + 1 : prompt_path;
  for (i = 0; base[i] && base[i] != '.' && n < sizeof(num) - 1; i++)
    num[n++] = base[i];
  num[n] = '\0';
  if (n == 0)
    return 1;

  snprintf(paired, sizeof(paired), "%s/fixtures/specs/%s.json",
           cli_fixture_root(), num);
  return spec_load_and_validate(paired, out);
}

static SpecProvider g_file_provider = {"file", file_prompt_to_spec};

SpecProvider *spec_file_provider(void) { return &g_file_provider; }

int spec_provider_validate_retry(SpecProvider *provider, const char *prompt_path,
                                 const char *spec_path, SpecV1 *out) {
  int attempt;

  if (!out)
    return 1;

  for (attempt = 0; attempt < 3; attempt++) {
    if (spec_path) {
      if (spec_load_and_validate(spec_path, out) == 0)
        return 0;
    } else if (provider && provider->prompt_to_spec) {
      if (provider->prompt_to_spec(provider, prompt_path, out) == 0 &&
          spec_validate(out) == 0)
        return 0;
    }
  }

  snprintf(out->clarifying_question, sizeof(out->clarifying_question),
           "Spec invalid after 3 attempts; clarify rails/current/form_factor.");
  return 1;
}
