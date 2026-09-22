#include "nlp_runtime.h"

#include <stdio.h>
#include <string.h>

/* ponytail: stub until ORT+model shipped; do not fake BiLSTM scores */

int nlp_runtime_available(void) {
#if defined(SYNTH_ENABLE_ONNX) && defined(SYNTH_NLP_ONNX_MODEL)
  /* Hook for future OrtSession; still fail-closed without a real load. */
  return 0;
#else
  return 0;
#endif
}

int nlp_runtime_infer(const char *text, NlpSemanticCandidate *out) {
  if (out)
    memset(out, 0, sizeof(*out));
  if (!out)
    return 1;
  (void)text;
  if (!nlp_runtime_available()) {
    snprintf(out->clarifying_question, sizeof(out->clarifying_question),
             "Neural NLP unavailable (no ONNX Runtime/model); use deterministic "
             "parse without --neural");
    return 1;
  }
  snprintf(out->clarifying_question, sizeof(out->clarifying_question),
           "ONNX session not loaded");
  return 1;
}
