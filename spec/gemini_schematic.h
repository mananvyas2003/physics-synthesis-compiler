#ifndef GEMINI_SCHEMATIC_H
#define GEMINI_SCHEMATIC_H

#include "schematic_load.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero if GEMINI_API_KEY (or SYNTH_LLM_API_KEY) is set. */
int gemini_api_key_present(void);

/* Non-zero if SYNTH_GEMINI_REPLAY points at a cassette directory. */
int gemini_replay_active(void);

/*
 * Live/replay prompt → schematic-ir.v1.
 * Replay: SYNTH_GEMINI_REPLAY=<cassette_dir> with http_response.json (no network).
 * Live: GEMINI_API_KEY + curl generateContent (max retries with validator feedback).
 * Returns 0 on success.
 */
int gemini_schematic_from_prompt(const char *prompt_text, const char *out_ir_path,
                                 SchematicIrMeta *meta);

#ifdef __cplusplus
}
#endif

#endif
