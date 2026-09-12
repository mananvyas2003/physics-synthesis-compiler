#ifndef GEMINI_SCHEMATIC_H
#define GEMINI_SCHEMATIC_H

#include "schematic_load.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero if GEMINI_API_KEY (or SYNTH_LLM_API_KEY) is set. */
int gemini_api_key_present(void);

/*
 * Live prompt → schematic-ir.v1 via Gemini generateContent.
 * Writes validated IR to out_ir_path. Max 3 attempts with validator feedback.
 * Returns 0 on success.
 */
int gemini_schematic_from_prompt(const char *prompt_text, const char *out_ir_path,
                                 SchematicIrMeta *meta);

#ifdef __cplusplus
}
#endif

#endif
