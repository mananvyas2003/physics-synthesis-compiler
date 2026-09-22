#ifndef NLP_H
#define NLP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NLP_Q_NONE = 0,
  NLP_Q_VOLTAGE,
  NLP_Q_CURRENT,
  NLP_Q_POWER,
  NLP_Q_RESISTANCE,
  NLP_Q_CAPACITANCE,
  NLP_Q_INDUCTANCE,
  NLP_Q_FREQUENCY,
  NLP_Q_TOLERANCE,
  NLP_Q_PACKAGE,
  NLP_Q_AMBIGUOUS
} NlpQuantity;

typedef enum {
  NLP_PROV_EXPLICIT = 0,
  NLP_PROV_INFERRED,
  NLP_PROV_DEFAULTED
} NlpProvenance;

typedef struct {
  char original[48];
  double value; /* SI where applicable; package unused */
  NlpQuantity kind;
  NlpProvenance prov;
  int pos; /* byte offset in source */
} NlpToken;

#define NLP_MAX_TOKENS 96

typedef struct {
  NlpToken tokens[NLP_MAX_TOKENS];
  int ntok;
} NlpLexResult;

/* Engineering lexer. Bare "10m" → NLP_Q_AMBIGUOUS (not milli-garbage). */
int nlp_lex(const char *text, NlpLexResult *out);

/*
 * Deterministic offline NLP → schematic-ir.v1 JSON.
 * Return 0 on write success; 1 with clarifying_question filled.
 * Does not use Gemini, network, or fixture filename maps.
 */
int nlp_text_to_schematic_ir(const char *text, const char *out_ir_path,
                             char *clarifying_question, size_t clarify_len);

#ifdef __cplusplus
}
#endif

#endif
