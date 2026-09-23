#ifndef SCHEMATIC_LOAD_H
#define SCHEMATIC_LOAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char name[64];
  char clarifying_question[256];
} SchematicIrMeta;

/* Structural validate against schematic-ir.v1 / seed shape. Return 0 if ok. */
int schematic_ir_validate_file(const char *path);

/* Validate and fill meta.name. */
int schematic_ir_load_and_validate(const char *path, SchematicIrMeta *out);

/*
 * Prompt → schematic IR.
 * force_offline + fixtures/prompts/NNN.txt: corpus map only.
 * Else: deterministic offline NLP on text (no fixture filename matching).
 * preferred_out_path: write target for NLP; may be NULL for corpus map.
 */
int schematic_provider_from_prompt(const char *prompt_path,
                                   const char *prompt_text_override,
                                   const char *preferred_out_path,
                                   int force_offline, char *out_ir_path,
                                   size_t out_len, SchematicIrMeta *meta);

#ifdef __cplusplus
}
#endif

#endif
