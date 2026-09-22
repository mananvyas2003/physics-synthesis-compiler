#ifndef NLP_RUNTIME_H
#define NLP_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional local neural NLP backend (BiLSTM/BiGRU ONNX).
 * Default build: stub only — no ONNX Runtime, no model, no physics coupling.
 *
 * Enable with SYNTH_ENABLE_ONNX + vendored ORT + exported model path
 * (SYNTH_NLP_ONNX_MODEL). Until then nlp_runtime_available() == 0.
 */

typedef struct {
  char intent[64];
  char clarifying_question[256];
  float confidence; /* 0..1 when available */
} NlpSemanticCandidate;

/* 1 if a loaded ONNX session exists; 0 in default builds. */
int nlp_runtime_available(void);

/*
 * Semantic interpret only — never equations/MPNs/DFM/PCB.
 * Return 0 with candidate filled; 1 if unavailable or infer failed
 * (clarifying_question set).
 */
int nlp_runtime_infer(const char *text, NlpSemanticCandidate *out);

#ifdef __cplusplus
}
#endif

#endif
