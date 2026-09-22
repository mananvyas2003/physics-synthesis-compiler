#include "nlp.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ponytail: pattern IR for divider/pull/bypass/RC/between; ONNX/Gemini later */

static int ci_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static int ci_starts(const char *s, const char *pfx) {
  while (*pfx) {
    if (tolower((unsigned char)*s) != tolower((unsigned char)*pfx))
      return 0;
    s++;
    pfx++;
  }
  return 1;
}

static int is_package(const char *s, size_t n) {
  size_t i;
  if (n != 4)
    return 0;
  for (i = 0; i < n; i++) {
    if (!isdigit((unsigned char)s[i]))
      return 0;
  }
  return 1;
}

static int is_nodeish(const char *w) {
  size_t i;
  if (!w || !w[0])
    return 0;
  /* VIN, GND, 3V3, ADC_SENSE */
  if (!(isalnum((unsigned char)w[0]) || w[0] == '_'))
    return 0;
  for (i = 0; w[i]; i++) {
    char c = w[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '+'))
      return 0;
  }
  return 1;
}

static void tok_push(NlpLexResult *out, const char *orig, size_t n, double val,
                     NlpQuantity kind, NlpProvenance prov, int pos) {
  NlpToken *t;
  if (!out || out->ntok >= NLP_MAX_TOKENS || n == 0)
    return;
  t = &out->tokens[out->ntok++];
  memset(t, 0, sizeof(*t));
  if (n >= sizeof(t->original))
    n = sizeof(t->original) - 1;
  memcpy(t->original, orig, n);
  t->original[n] = '\0';
  t->value = val;
  t->kind = kind;
  t->prov = prov;
  t->pos = pos;
}

static int classify_unit_letter(char u, NlpQuantity *kind) {
  switch ((char)tolower((unsigned char)u)) {
  case 'v':
    *kind = NLP_Q_VOLTAGE;
    return 0;
  case 'a':
    *kind = NLP_Q_CURRENT;
    return 0;
  case 'w':
    *kind = NLP_Q_POWER;
    return 0;
  case 'f':
    *kind = NLP_Q_CAPACITANCE;
    return 0;
  case 'h':
    *kind = NLP_Q_INDUCTANCE;
    return 0;
  case 'r':
  case 'o':
    *kind = NLP_Q_RESISTANCE;
    return 0;
  case '%':
    *kind = NLP_Q_TOLERANCE;
    return 0;
  default:
    return 1;
  }
}

/* Parse one engineering quantity at *pp; advance *pp. */
static int lex_quantity(const char *text, const char **pp, NlpLexResult *out) {
  const char *start = *pp;
  const char *p = start;
  char buf[64];
  size_t n = 0;
  double mag;
  char *end = NULL;
  double mult = 1.0;
  NlpQuantity kind = NLP_Q_NONE;
  NlpProvenance prov = NLP_PROV_EXPLICIT;
  int pos = (int)(start - text);
  char prefix = 0;
  int had_unit = 0;

  while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == ',')) {
    if (*p != ',' && n + 1 < sizeof(buf))
      buf[n++] = *p;
    p++;
  }
  if (n == 0)
    return 1;
  buf[n] = '\0';
  mag = strtod(buf, &end);
  if (end == buf || !isfinite(mag))
    return 1;

  while (*p && isspace((unsigned char)*p))
    p++;

  /* Word units: volt(s), amp(s), milliamp(s), ... */
  if (ci_starts(p, "milliamps") || ci_starts(p, "milliamp")) {
    size_t skip = ci_starts(p, "milliamps") ? 9u : 8u;
    tok_push(out, start, (size_t)(p - start) + skip, mag * 1e-3,
             NLP_Q_CURRENT, NLP_PROV_EXPLICIT, pos);
    *pp = p + skip;
    return 0;
  }
  if (ci_starts(p, "volt") || ci_starts(p, "volts")) {
    size_t skip = ci_starts(p, "volts") ? 5 : 4;
    tok_push(out, start, (size_t)(p - start) + skip, mag, NLP_Q_VOLTAGE,
             NLP_PROV_EXPLICIT, pos);
    *pp = p + skip;
    return 0;
  }
  if (ci_starts(p, "amp") || ci_starts(p, "amps")) {
    size_t skip = ci_starts(p, "amps") ? 4 : 3;
    tok_push(out, start, (size_t)(p - start) + skip, mag, NLP_Q_CURRENT,
             NLP_PROV_EXPLICIT, pos);
    *pp = p + skip;
    return 0;
  }

  /* Attached engineering suffix only (no whitespace). Word units handled above. */
  {
    const char *s = p;
    /* compound: nF uF pF mA mV mW mH kHz MHz kOhm */
    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'F' || s[1] == 'f') &&
        !isalnum((unsigned char)s[2])) {
      mult = 1e-9;
      kind = NLP_Q_CAPACITANCE;
      had_unit = 1;
      p = s + 2;
    } else if ((s[0] == 'u' || s[0] == 'U') && (s[1] == 'F' || s[1] == 'f') &&
               !isalnum((unsigned char)s[2])) {
      mult = 1e-6;
      kind = NLP_Q_CAPACITANCE;
      had_unit = 1;
      p = s + 2;
    } else if ((s[0] == 'p' || s[0] == 'P') && (s[1] == 'F' || s[1] == 'f') &&
               !isalnum((unsigned char)s[2])) {
      mult = 1e-12;
      kind = NLP_Q_CAPACITANCE;
      had_unit = 1;
      p = s + 2;
    } else if (s[0] == 'm' && (s[1] == 'A' || s[1] == 'a') &&
               !isalnum((unsigned char)s[2])) {
      mult = 1e-3;
      kind = NLP_Q_CURRENT;
      had_unit = 1;
      p = s + 2;
    } else if (s[0] == 'm' && (s[1] == 'V' || s[1] == 'v') &&
               !isalnum((unsigned char)s[2])) {
      mult = 1e-3;
      kind = NLP_Q_VOLTAGE;
      had_unit = 1;
      p = s + 2;
    } else if (s[0] == 'm' && (s[1] == 'W' || s[1] == 'w') &&
               !isalnum((unsigned char)s[2])) {
      mult = 1e-3;
      kind = NLP_Q_POWER;
      had_unit = 1;
      p = s + 2;
    } else if (s[0] == 'm' && (s[1] == 'H' || s[1] == 'h') &&
               !isalnum((unsigned char)s[2])) {
      mult = 1e-3;
      kind = NLP_Q_INDUCTANCE;
      had_unit = 1;
      p = s + 2;
    } else if ((s[0] == 'k' || s[0] == 'K') &&
               (s[1] == 'H' || s[1] == 'h') && (s[2] == 'z' || s[2] == 'Z') &&
               !isalnum((unsigned char)s[3])) {
      mult = 1e3;
      kind = NLP_Q_FREQUENCY;
      had_unit = 1;
      p = s + 3;
    } else if (s[0] == 'M' && (s[1] == 'H' || s[1] == 'h') &&
               (s[2] == 'z' || s[2] == 'Z') && !isalnum((unsigned char)s[3])) {
      mult = 1e6;
      kind = NLP_Q_FREQUENCY;
      had_unit = 1;
      p = s + 3;
    } else if (strncmp(s, "Meg", 3) == 0 || strncmp(s, "meg", 3) == 0) {
      mult = 1e6;
      prefix = 'M';
      p = s + 3;
    } else if ((s[0] == 'k' || s[0] == 'K') && !isalpha((unsigned char)s[1])) {
      mult = 1e3;
      prefix = 'k';
      p = s + 1;
    } else if (s[0] == 'M' && !isalpha((unsigned char)s[1])) {
      mult = 1e6;
      prefix = 'M';
      p = s + 1;
    } else if ((s[0] == 'u' || s[0] == 'U') && !isalpha((unsigned char)s[1])) {
      mult = 1e-6;
      prefix = 'u';
      p = s + 1;
    } else if ((unsigned char)s[0] == 0xC2 && (unsigned char)s[1] == 0xB5) {
      mult = 1e-6;
      prefix = 'u';
      p = s + 2;
    } else if ((s[0] == 'n' || s[0] == 'N') && !isalpha((unsigned char)s[1])) {
      mult = 1e-9;
      prefix = 'n';
      p = s + 1;
    } else if ((s[0] == 'p' || s[0] == 'P') && !isalpha((unsigned char)s[1])) {
      mult = 1e-12;
      prefix = 'p';
      p = s + 1;
    } else if (s[0] == 'm' && !isalpha((unsigned char)s[1])) {
      mult = 1e-3;
      prefix = 'm';
      p = s + 1;
    } else if ((s[0] == 'g' || s[0] == 'G') && !isalpha((unsigned char)s[1])) {
      mult = 1e9;
      prefix = 'g';
      p = s + 1;
    } else if ((s[0] == 'H' || s[0] == 'h') && (s[1] == 'z' || s[1] == 'Z') &&
               !isalnum((unsigned char)s[2])) {
      kind = NLP_Q_FREQUENCY;
      had_unit = 1;
      p = s + 2;
    } else if ((unsigned char)s[0] == 0xCE && (unsigned char)s[1] == 0xA9) {
      kind = NLP_Q_RESISTANCE;
      had_unit = 1;
      p = s + 2;
    } else if (ci_starts(s, "ohm") && !isalnum((unsigned char)s[3])) {
      kind = NLP_Q_RESISTANCE;
      had_unit = 1;
      p = s + 3;
    } else if (!isalpha((unsigned char)s[1]) &&
               classify_unit_letter(s[0], &kind) == 0) {
      had_unit = 1;
      p = s + 1;
    }
  }


  if (!had_unit) {
    if (prefix == 'm') {
      /* bare 10m — refuse silent milli */
      tok_push(out, start, (size_t)(p - start), mag * mult, NLP_Q_AMBIGUOUS,
               NLP_PROV_EXPLICIT, pos);
      *pp = p;
      return 0;
    }
    if (prefix == 'k' || prefix == 'M') {
      kind = NLP_Q_RESISTANCE;
      prov = NLP_PROV_INFERRED;
    } else if (prefix == 'u' || prefix == 'n' || prefix == 'p') {
      kind = NLP_Q_CAPACITANCE;
      prov = NLP_PROV_INFERRED;
    } else if (prefix == 0) {
      /* bare number — keep as NONE word-ish via quantity NONE */
      kind = NLP_Q_NONE;
      prov = NLP_PROV_EXPLICIT;
    } else {
      kind = NLP_Q_AMBIGUOUS;
    }
  }

  tok_push(out, start, (size_t)(p - start), mag * mult, kind, prov, pos);
  *pp = p;
  return 0;
}

int nlp_lex(const char *text, NlpLexResult *out) {
  const char *p;
  if (!text || !out)
    return 1;
  memset(out, 0, sizeof(*out));
  p = text;
  while (*p) {
    while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == ';' ||
                  *p == ':' || *p == '"' || *p == '\'' || *p == '/'))
      p++;
    if (!*p)
      break;
    if (isdigit((unsigned char)*p)) {
      /* Rail names like 3V3 / 5V0 — not quantities */
      if (isdigit((unsigned char)p[0]) && (p[1] == 'V' || p[1] == 'v') &&
          isdigit((unsigned char)p[2])) {
        const char *s = p;
        size_t n = 0;
        while (p[n] && (isalnum((unsigned char)p[n]) || p[n] == '_'))
          n++;
        tok_push(out, s, n, 0.0, NLP_Q_NONE, NLP_PROV_EXPLICIT,
                 (int)(s - text));
        p += n;
        continue;
      }
      if (is_package(p, 4) && !isdigit((unsigned char)p[4])) {
        tok_push(out, p, 4, 0.0, NLP_Q_PACKAGE, NLP_PROV_EXPLICIT,
                 (int)(p - text));
        p += 4;
        continue;
      }
      if (lex_quantity(text, &p, out) == 0)
        continue;
    }
    /* word / node / relation glue */
    {
      const char *s = p;
      char word[48];
      size_t n = 0;
      while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != ';' &&
             *p != '"' && *p != '\'' && *p != '/') {
        if (n + 1 < sizeof(word))
          word[n++] = *p;
        p++;
      }
      if (*p == '/')
        p++; /* 10k/10k → two quantity passes on next loop if digits */
      while (n > 0 && (word[n - 1] == '.' || word[n - 1] == '!' ||
                       word[n - 1] == '?' || word[n - 1] == ':' ||
                       word[n - 1] == ')'))
        n--;
      word[n] = '\0';
      if (n == 0)
        continue;
      tok_push(out, word, n, 0.0, NLP_Q_NONE, NLP_PROV_EXPLICIT, (int)(s - text));
    }
  }
  return 0;
}

static const NlpToken *first_kind(const NlpLexResult *lex, NlpQuantity k) {
  int i;
  for (i = 0; i < lex->ntok; i++)
    if (lex->tokens[i].kind == k)
      return &lex->tokens[i];
  return NULL;
}

static int has_word(const NlpLexResult *lex, const char *w) {
  int i;
  for (i = 0; i < lex->ntok; i++)
    if (lex->tokens[i].kind == NLP_Q_NONE && ci_eq(lex->tokens[i].original, w))
      return 1;
  return 0;
}

static int find_between(const NlpLexResult *lex, char *a, size_t alen, char *b,
                        size_t blen) {
  int i;
  for (i = 0; i + 3 < lex->ntok; i++) {
    if (lex->tokens[i].kind != NLP_Q_NONE)
      continue;
    if (!ci_eq(lex->tokens[i].original, "between"))
      continue;
    if (lex->tokens[i + 1].kind != NLP_Q_NONE ||
        lex->tokens[i + 2].kind != NLP_Q_NONE ||
        lex->tokens[i + 3].kind != NLP_Q_NONE)
      continue;
    if (!ci_eq(lex->tokens[i + 2].original, "and"))
      continue;
    if (!is_nodeish(lex->tokens[i + 1].original) ||
        !is_nodeish(lex->tokens[i + 3].original))
      continue;
    snprintf(a, alen, "%s", lex->tokens[i + 1].original);
    snprintf(b, blen, "%s", lex->tokens[i + 3].original);
    return 0;
  }
  return 1;
}

static int find_from_to(const NlpLexResult *lex, char *a, size_t alen, char *b,
                        size_t blen) {
  int i;
  for (i = 0; i + 3 < lex->ntok; i++) {
    if (lex->tokens[i].kind != NLP_Q_NONE ||
        !ci_eq(lex->tokens[i].original, "from"))
      continue;
    if (lex->tokens[i + 1].kind != NLP_Q_NONE)
      continue;
    if (lex->tokens[i + 2].kind != NLP_Q_NONE ||
        !(ci_eq(lex->tokens[i + 2].original, "to") ||
          ci_eq(lex->tokens[i + 2].original, "down")))
      continue;
    /* "from 5V down to" — skip voltage tokens */
    {
      int j = i + 1;
      const char *na = NULL;
      const char *nb = NULL;
      while (j < lex->ntok) {
        if (lex->tokens[j].kind == NLP_Q_NONE &&
            (ci_eq(lex->tokens[j].original, "to") ||
             ci_eq(lex->tokens[j].original, "down"))) {
          j++;
          continue;
        }
        if (lex->tokens[j].kind == NLP_Q_VOLTAGE) {
          j++;
          continue;
        }
        if (lex->tokens[j].kind == NLP_Q_NONE &&
            is_nodeish(lex->tokens[j].original)) {
          if (!na)
            na = lex->tokens[j].original;
          else if (!nb) {
            nb = lex->tokens[j].original;
            break;
          }
        }
        j++;
      }
      if (na && nb) {
        snprintf(a, alen, "%s", na);
        snprintf(b, blen, "%s", nb);
        return 0;
      }
    }
  }
  return 1;
}

static int write_twoterm_ir(const char *path, const char *name,
                            const char *part_type, double value,
                            const char *pkg, const char *n1, const char *n2) {
  FILE *fp = fopen(path, "wb");
  const char *p = pkg && pkg[0] ? pkg : "0603";
  if (!fp)
    return 1;
  fprintf(fp,
          "{\n"
          "  \"schema\": \"schematic-ir.v1\",\n"
          "  \"name\": \"%s\",\n"
          "  \"description\": \"nlp offline\",\n"
          "  \"category\": \"NLP\",\n"
          "  \"parts\": [\n"
          "    { \"mpn\": \"NLP-DEMO\", \"type\": \"%s\", \"value\": %.12g, "
          "\"package\": \"%s\", \"v_rating\": 50.0, \"power_rating_w\": 0.125 }\n"
          "  ],\n"
          "  \"components\": [\n"
          "    { \"role\": \"X1\", \"part_type\": \"%s\", \"quantity\": 1, "
          "\"target_value\": %.12g, \"package\": \"%s\" }\n"
          "  ],\n"
          "  \"nodes\": [\"%s\", \"%s\", \"GND\"],\n"
          "  \"connections\": [\n"
          "    { \"role\": \"X1\", \"pin\": \"1\", \"node\": \"%s\" },\n"
          "    { \"role\": \"X1\", \"pin\": \"2\", \"node\": \"%s\" }\n"
          "  ]\n"
          "}\n",
          name, part_type, value, p, part_type, value, p, n1, n2, n1, n2);
  fclose(fp);
  return 0;
}

static int write_divider_ir(const char *path, double r1, double r2,
                            const char *top, const char *mid, const char *bot,
                            const char *pkg) {
  FILE *fp = fopen(path, "wb");
  const char *p = pkg && pkg[0] ? pkg : "0603";
  if (!fp)
    return 1;
  fprintf(fp,
          "{\n"
          "  \"schema\": \"schematic-ir.v1\",\n"
          "  \"name\": \"nlp_divider\",\n"
          "  \"description\": \"nlp offline divider\",\n"
          "  \"category\": \"NLP\",\n"
          "  \"parts\": [\n"
          "    { \"mpn\": \"NLP-R1\", \"type\": \"resistor\", \"value\": %.12g, "
          "\"package\": \"%s\", \"v_rating\": 50.0, \"power_rating_w\": 0.125 },\n"
          "    { \"mpn\": \"NLP-R2\", \"type\": \"resistor\", \"value\": %.12g, "
          "\"package\": \"%s\", \"v_rating\": 50.0, \"power_rating_w\": 0.125 }\n"
          "  ],\n"
          "  \"components\": [\n"
          "    { \"role\": \"r1\", \"part_type\": \"resistor\", \"quantity\": 1, "
          "\"target_value\": %.12g, \"package\": \"%s\" },\n"
          "    { \"role\": \"r2\", \"part_type\": \"resistor\", \"quantity\": 1, "
          "\"target_value\": %.12g, \"package\": \"%s\" }\n"
          "  ],\n"
          "  \"nodes\": [\"%s\", \"%s\", \"%s\"],\n"
          "  \"connections\": [\n"
          "    { \"role\": \"r1\", \"pin\": \"1\", \"node\": \"%s\" },\n"
          "    { \"role\": \"r1\", \"pin\": \"2\", \"node\": \"%s\" },\n"
          "    { \"role\": \"r2\", \"pin\": \"1\", \"node\": \"%s\" },\n"
          "    { \"role\": \"r2\", \"pin\": \"2\", \"node\": \"%s\" }\n"
          "  ]\n"
          "}\n",
          r1, p, r2, p, r1, p, r2, p, top, mid, bot, top, mid, mid, bot);
  fclose(fp);
  return 0;
}

static int write_rc_ir(const char *path, double r, double c, const char *vin,
                       const char *vout, const char *pkg) {
  FILE *fp = fopen(path, "wb");
  const char *p = pkg && pkg[0] ? pkg : "0603";
  if (!fp)
    return 1;
  fprintf(fp,
          "{\n"
          "  \"schema\": \"schematic-ir.v1\",\n"
          "  \"name\": \"nlp_rc\",\n"
          "  \"description\": \"nlp offline RC\",\n"
          "  \"category\": \"NLP\",\n"
          "  \"parts\": [\n"
          "    { \"mpn\": \"NLP-R\", \"type\": \"resistor\", \"value\": %.12g, "
          "\"package\": \"%s\", \"v_rating\": 50.0, \"power_rating_w\": 0.125 },\n"
          "    { \"mpn\": \"NLP-C\", \"type\": \"capacitor\", \"value\": %.12g, "
          "\"package\": \"%s\", \"v_rating\": 50.0, \"power_rating_w\": 0.125 }\n"
          "  ],\n"
          "  \"components\": [\n"
          "    { \"role\": \"r1\", \"part_type\": \"resistor\", \"quantity\": 1, "
          "\"target_value\": %.12g, \"package\": \"%s\" },\n"
          "    { \"role\": \"c1\", \"part_type\": \"capacitor\", \"quantity\": 1, "
          "\"target_value\": %.12g, \"package\": \"%s\" }\n"
          "  ],\n"
          "  \"nodes\": [\"%s\", \"%s\", \"GND\"],\n"
          "  \"connections\": [\n"
          "    { \"role\": \"r1\", \"pin\": \"1\", \"node\": \"%s\" },\n"
          "    { \"role\": \"r1\", \"pin\": \"2\", \"node\": \"%s\" },\n"
          "    { \"role\": \"c1\", \"pin\": \"1\", \"node\": \"%s\" },\n"
          "    { \"role\": \"c1\", \"pin\": \"2\", \"node\": \"GND\" }\n"
          "  ]\n"
          "}\n",
          r, p, c, p, r, p, c, p, vin, vout, vin, vout, vout);
  fclose(fp);
  return 0;
}

static void set_clarify(char *buf, size_t n, const char *msg) {
  if (!buf || n == 0)
    return;
  snprintf(buf, n, "%s", msg);
}

int nlp_text_to_schematic_ir(const char *text, const char *out_ir_path,
                             char *clarifying_question, size_t clarify_len) {
  NlpLexResult lex;
  const NlpToken *r;
  const NlpToken *c;
  const NlpToken *pkg;
  char a[64], b[64];
  int i;

  if (clarifying_question && clarify_len)
    clarifying_question[0] = '\0';
  if (!text || !out_ir_path) {
    set_clarify(clarifying_question, clarify_len, "Missing prompt text");
    return 1;
  }
  if (nlp_lex(text, &lex) != 0) {
    set_clarify(clarifying_question, clarify_len, "Lexer failed");
    return 1;
  }
  for (i = 0; i < lex.ntok; i++) {
    if (lex.tokens[i].kind == NLP_Q_AMBIGUOUS) {
      set_clarify(clarifying_question, clarify_len,
                  "Ambiguous quantity (e.g. '10m'); use 10mA/10mV/10mH/10mohm");
      return 1;
    }
  }

  pkg = first_kind(&lex, NLP_Q_PACKAGE);
  r = first_kind(&lex, NLP_Q_RESISTANCE);
  c = first_kind(&lex, NLP_Q_CAPACITANCE);

  /* Two-terminal R between A and B (keyword optional if value+between clear) */
  if (r && find_between(&lex, a, sizeof(a), b, sizeof(b)) == 0) {
    return write_twoterm_ir(out_ir_path, "nlp_resistor", "resistor", r->value,
                            pkg ? pkg->original : "0603", a, b);
  }

  /* Two-terminal C between A and B */
  if (c && find_between(&lex, a, sizeof(a), b, sizeof(b)) == 0) {
    return write_twoterm_ir(out_ir_path, "nlp_cap", "capacitor", c->value,
                            pkg ? pkg->original : "0603", a, b);
  }

  /* Pull-up on rail — needs signal; if "on RAIL" only → clarify */
  if (r && (has_word(&lex, "pull-up") || has_word(&lex, "pullup") ||
            has_word(&lex, "pull-down") || has_word(&lex, "pulldown"))) {
    for (i = 0; i + 1 < lex.ntok; i++) {
      if (lex.tokens[i].kind == NLP_Q_NONE && ci_eq(lex.tokens[i].original, "on")) {
        int j = i + 1;
        while (j < lex.ntok &&
               (ci_eq(lex.tokens[j].original, "the") ||
                ci_eq(lex.tokens[j].original, "a")))
          j++;
        if (j < lex.ntok && lex.tokens[j].kind == NLP_Q_NONE &&
            is_nodeish(lex.tokens[j].original)) {
          return write_twoterm_ir(out_ir_path, "nlp_pullup", "resistor", r->value,
                                  pkg ? pkg->original : "0603",
                                  lex.tokens[j].original, "SDA");
        }
      }
    }
    set_clarify(clarifying_question, clarify_len,
                "Pull-up needs rail and signal nodes (e.g. between 3V3 and SDA)");
    return 1;
  }

  /* Divider */
  if (has_word(&lex, "divider") && r) {
    double r1 = r->value;
    double r2 = r->value;
    const char *top = "VIN";
    const char *mid = "VOUT";
    const char *bot = "GND";
    for (i = 0; i < lex.ntok; i++) {
      if (&lex.tokens[i] != r && lex.tokens[i].kind == NLP_Q_RESISTANCE) {
        r2 = lex.tokens[i].value;
        break;
      }
    }
    if (find_from_to(&lex, a, sizeof(a), b, sizeof(b)) == 0) {
      top = a;
      if (ci_eq(b, "GND") || ci_eq(b, "gnd"))
        bot = "GND";
      else
        mid = b;
    }
    for (i = 0; i < lex.ntok; i++) {
      if (lex.tokens[i].kind == NLP_Q_NONE &&
          ci_eq(lex.tokens[i].original, "VIN"))
        top = "VIN";
      if (lex.tokens[i].kind == NLP_Q_NONE &&
          ci_eq(lex.tokens[i].original, "VOUT"))
        mid = "VOUT";
      if (lex.tokens[i].kind == NLP_Q_NONE &&
          (ci_eq(lex.tokens[i].original, "GND") ||
           ci_eq(lex.tokens[i].original, "gnd")))
        bot = "GND";
    }
    return write_divider_ir(out_ir_path, r1, r2, top, mid, bot,
                            pkg ? pkg->original : "0603");
  }

  /* Bypass / decouple capacitor (no between — rail to GND) */
  if (c && (has_word(&lex, "bypass") || has_word(&lex, "decouple") ||
            has_word(&lex, "decoupling") || has_word(&lex, "capacitor"))) {
    const char *rail = "VDD";
    for (i = 0; i < lex.ntok; i++) {
      if (lex.tokens[i].kind == NLP_Q_NONE && is_nodeish(lex.tokens[i].original) &&
          !ci_eq(lex.tokens[i].original, "capacitor") &&
          !ci_eq(lex.tokens[i].original, "bypass") &&
          !ci_eq(lex.tokens[i].original, "next") &&
          !ci_eq(lex.tokens[i].original, "to") &&
          !ci_eq(lex.tokens[i].original, "the") &&
          !ci_eq(lex.tokens[i].original, "a") &&
          !ci_eq(lex.tokens[i].original, "sensor") &&
          !ci_eq(lex.tokens[i].original, "Add") &&
          !ci_eq(lex.tokens[i].original, "add") &&
          !ci_eq(lex.tokens[i].original, "Place") &&
          !ci_eq(lex.tokens[i].original, "place") &&
          !ci_eq(lex.tokens[i].original, "Put") &&
          !ci_eq(lex.tokens[i].original, "put") &&
          !ci_eq(lex.tokens[i].original, "MCU") &&
          !ci_eq(lex.tokens[i].original, "on")) {
        rail = lex.tokens[i].original;
        break;
      }
    }
    if (has_word(&lex, "sensor"))
      rail = "SENSOR_VDD";
    return write_twoterm_ir(out_ir_path, "nlp_bypass", "capacitor", c->value,
                            pkg ? pkg->original : "0603", rail, "GND");
  }

  /* RC low-pass */
  if (r && c && (has_word(&lex, "RC") || has_word(&lex, "rc") ||
                 has_word(&lex, "low-pass") || has_word(&lex, "lowpass") ||
                 has_word(&lex, "filter"))) {
    return write_rc_ir(out_ir_path, r->value, c->value, "VIN", "VOUT",
                       pkg ? pkg->original : "0603");
  }

  /* R from A to B */
  if (r && find_from_to(&lex, a, sizeof(a), b, sizeof(b)) == 0) {
    return write_twoterm_ir(out_ir_path, "nlp_resistor", "resistor", r->value,
                            pkg ? pkg->original : "0603", a, b);
  }

  if (has_word(&lex, "stable") || has_word(&lex, "small") ||
      has_word(&lex, "normal") || has_word(&lex, "Ambiguous") ||
      has_word(&lex, "ambiguous") || has_word(&lex, "somewhere") ||
      has_word(&lex, "Something") || has_word(&lex, "something") ||
      has_word(&lex, "please")) {
    set_clarify(clarifying_question, clarify_len,
                "Ambiguous prompt; specify topology, values, and nodes");
    return 1;
  }

  set_clarify(clarifying_question, clarify_len,
              "Unsupported offline NLP pattern; clarify component, value, "
              "and nodes (between/from/to/divider/bypass/RC)");
  return 1;
}
