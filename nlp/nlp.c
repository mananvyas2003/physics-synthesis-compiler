#include "nlp.h"

#include "compiler.h"
#include "e_series.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ponytail: keyword patterns + closed-form value synthesis; a parser with
 * real grammar (or the ONNX path) when patterns stop scaling. */

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

/* =========================================================
 * Design builder → schematic-ir.v1. One writer for every pattern.
 * ========================================================= */

typedef struct {
  char role[16];
  const char *type;
  double value;
  double power;    /* W rating; 0 → type default */
  const char *pkg; /* NULL → design package */
  const char *pins[3];
  char nets[3][48];
  int n;
} NlpPart;

typedef struct {
  NlpPart p[8];
  int n;
  char measure[48];
  double mmin, mmax;
  int limits;
  char prov[512];  /* JSON members "key": "explicit|inferred|defaulted" */
  char notes[256]; /* JSON strings: stated requirements not modeled */
} NlpDesign;

static NlpPart *add_part(NlpDesign *d, const char *role, const char *type,
                         double value, const char *a, const char *b) {
  NlpPart *p = &d->p[d->n++];
  int diode = strcmp(type, "led") == 0 || strcmp(type, "diode") == 0;
  memset(p, 0, sizeof(*p));
  snprintf(p->role, sizeof(p->role), "%s", role);
  p->type = type;
  p->value = value;
  p->pins[0] = diode ? "A" : "1";
  p->pins[1] = diode ? "K" : "2";
  snprintf(p->nets[0], sizeof(p->nets[0]), "%s", a);
  snprintf(p->nets[1], sizeof(p->nets[1]), "%s", b);
  p->n = 2;
  return p;
}

static void add_prov(NlpDesign *d, const char *key, const char *src) {
  size_t len = strlen(d->prov);
  snprintf(d->prov + len, sizeof(d->prov) - len, "%s\"%s\": \"%s\"",
           len ? ", " : "", key, src);
}

static void add_note(NlpDesign *d, const char *note) {
  size_t len = strlen(d->notes);
  snprintf(d->notes + len, sizeof(d->notes) - len, "%s\"%s\"",
           len ? ", " : "", note);
}

static void set_clarify(char *buf, size_t n, const char *msg) {
  if (buf && n)
    snprintf(buf, n, "%s", msg);
}

static const char *part_pkg(const NlpPart *p, const char *pkg) {
  if (p->pkg)
    return p->pkg;
  if (strcmp(p->type, "mosfet") == 0 || strcmp(p->type, "ldo") == 0)
    return "SOT-23";
  return pkg ? pkg : "0603";
}

/* Smallest DFM-allowed chip package whose ampacity covers i (A). */
static const char *chip_pkg_for_current(double i) {
  if (i <= 0.1)
    return "0603";
  if (i <= 0.2)
    return "0805";
  if (i <= 0.35)
    return "1206";
  return NULL;
}

static int emit_design(const NlpDesign *d, const char *name, const char *pkg,
                       const char *path, char *clar, size_t clar_len) {
  char nets[24][48];
  int nnet = 0, i, k, powered = 0;
  FILE *fp;

  for (i = 0; i < d->n; i++) {
    if (strcmp(d->p[i].type, "battery") == 0)
      powered = 1;
    for (k = 0; k < d->p[i].n; k++) {
      int j;
      for (j = 0; j < nnet && strcmp(nets[j], d->p[i].nets[k]) != 0; j++)
        ;
      if (j == nnet && nnet < 24)
        snprintf(nets[nnet++], sizeof(nets[0]), "%s", d->p[i].nets[k]);
      if (compiler_rail_voltage(d->p[i].nets[k], NULL) != RAIL_NONE)
        powered = 1;
    }
  }
  if (!powered) {
    set_clarify(clar, clar_len,
                "No supply given; name the rail (e.g. 3V3, 5V, VIN) or state "
                "its voltage");
    return 1;
  }
  fp = fopen(path, "wb");
  if (!fp)
    return 1;
  fprintf(fp, "{\n  \"schema\": \"schematic-ir.v1\",\n  \"name\": \"%s\",\n"
              "  \"description\": \"nlp offline\",\n  \"category\": \"NLP\",\n",
          name);
  if (d->measure[0]) {
    fprintf(fp, "  \"measure\": { \"node\": \"%s\"", d->measure);
    if (d->limits)
      fprintf(fp, ", \"min\": %.6g, \"max\": %.6g", d->mmin, d->mmax);
    fprintf(fp, " },\n");
  }
  fprintf(fp, "  \"provenance\": { %s },\n  \"unmodeled\": [ %s ],\n",
          d->prov, d->notes);
  fprintf(fp, "  \"parts\": [\n");
  for (i = 0; i < d->n; i++) {
    const NlpPart *p = &d->p[i];
    const char *t = p->type;
    int semi = strcmp(t, "mosfet") == 0 || strcmp(t, "ldo") == 0;
    fprintf(fp,
            "    { \"mpn\": \"NLP-%s\", \"type\": \"%s\", \"value\": %.12g, "
            "\"package\": \"%s\", \"v_rating\": %g, \"i_rating\": %g, "
            "\"power_rating_w\": %g }%s\n",
            p->role, t, p->value, part_pkg(p, pkg),
            strcmp(t, "ldo") == 0 ? 16.0 : (semi ? 30.0 : 50.0),
            strcmp(t, "led") == 0 ? 0.02 : (semi ? 0.5 : 1.0),
            p->power > 0.0 ? p->power
                           : (strcmp(t, "resistor") == 0 ? 0.125 : 0.5),
            i + 1 < d->n ? "," : "");
  }
  fprintf(fp, "  ],\n  \"components\": [\n");
  for (i = 0; i < d->n; i++)
    fprintf(fp,
            "    { \"role\": \"%s\", \"part_type\": \"%s\", \"quantity\": 1, "
            "\"target_value\": %.12g, \"package\": \"%s\" }%s\n",
            d->p[i].role, d->p[i].type, d->p[i].value, part_pkg(&d->p[i], pkg),
            i + 1 < d->n ? "," : "");
  fprintf(fp, "  ],\n  \"nodes\": [");
  for (i = 0; i < nnet; i++)
    fprintf(fp, "%s\"%s\"", i ? ", " : "", nets[i]);
  for (i = 0; i < nnet && strcmp(nets[i], "GND") != 0; i++)
    ;
  fprintf(fp, "%s],\n  \"connections\": [\n", i == nnet ? ", \"GND\"" : "");
  for (i = 0; i < d->n; i++)
    for (k = 0; k < d->p[i].n; k++)
      fprintf(fp, "    { \"role\": \"%s\", \"pin\": \"%s\", \"node\": \"%s\" }%s\n",
              d->p[i].role, d->p[i].pins[k], d->p[i].nets[k],
              (i + 1 < d->n || k + 1 < d->p[i].n) ? "," : "");
  fprintf(fp, "  ]\n}\n");
  fclose(fp);
  return 0;
}

/* 3.3 → "3V3", 5 → "5V", 12 → "12V", 1.8 → "1V8". */
static void rail_name(double v, char *out, size_t n) {
  long tenths = lround(v * 10.0);
  if (tenths % 10 == 0)
    snprintf(out, n, "%ldV", tenths / 10);
  else
    snprintf(out, n, "%ldV%ld", tenths / 10, tenths % 10);
}

static double e24(double v) { return e_series_nearest(E_SERIES_E24, v); }

/* Supply rail: explicit rail word (3V3, VIN, ...) wins, else a voltage. */
static int find_rail(const NlpLexResult *lex, char *out, size_t n) {
  int i;
  for (i = 0; i < lex->ntok; i++)
    if (lex->tokens[i].kind == NLP_Q_NONE &&
        compiler_rail_voltage(lex->tokens[i].original, NULL) != RAIL_NONE) {
      snprintf(out, n, "%s", lex->tokens[i].original);
      return 0;
    }
  for (i = 0; i < lex->ntok; i++)
    if (lex->tokens[i].kind == NLP_Q_VOLTAGE && lex->tokens[i].value > 0.0) {
      rail_name(lex->tokens[i].value, out, n);
      return 0;
    }
  return 1;
}

/* Largest and smallest stated voltages; count returned. */
static int voltage_span(const NlpLexResult *lex, double *vmax, double *vmin) {
  int i, cnt = 0;
  for (i = 0; i < lex->ntok; i++) {
    double v = lex->tokens[i].value;
    if (lex->tokens[i].kind != NLP_Q_VOLTAGE || !(v > 0.0))
      continue;
    if (cnt == 0 || v > *vmax)
      *vmax = v;
    if (cnt == 0 || v < *vmin)
      *vmin = v;
    cnt++;
  }
  return cnt;
}

static int has_any(const NlpLexResult *lex, const char *const *words) {
  for (; *words; words++)
    if (has_word(lex, *words))
      return 1;
  return 0;
}

int nlp_text_to_schematic_ir(const char *text, const char *out_ir_path,
                             char *clarifying_question, size_t clarify_len) {
  static const char *const w_pullup[] = {"pull-up", "pullup", "pull-down",
                                         "pulldown", NULL};
  static const char *const w_reg[] = {"regulator", "LDO", "ldo", NULL};
  static const char *const w_mos[] = {"MOSFET", "mosfet", "N-MOS", "NMOS",
                                      "nmos", "n-mos", NULL};
  static const char *const w_led[] = {"LED", "led", NULL};
  static const char *const w_ind[] = {"indicator", "status", NULL};
  static const char *const w_revpol[] = {"reverse-polarity", "reverse",
                                         "polarity", NULL};
  static const char *const w_decouple[] = {"bypass", "decouple", "decoupling",
                                           "capacitor", NULL};
  static const char *const w_lpf[] = {"RC", "rc", "low-pass", "lowpass",
                                      "filter", NULL};
  static const char *const w_vague[] = {
      "stable", "small", "normal", "Ambiguous", "ambiguous", "somewhere",
      "Something", "something", "please", NULL};
  NlpLexResult lex;
  NlpDesign d;
  const NlpToken *r, *c, *pkg, *cur, *freq;
  const char *pk;
  char a[64], b[64], rail[48], r2[48];
  double vmax = 0.0, vmin = 0.0;
  int i, nv;

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

  memset(&d, 0, sizeof(d));
  pkg = first_kind(&lex, NLP_Q_PACKAGE);
  pk = pkg ? pkg->original : "0603";
  add_prov(&d, "package", pkg ? "explicit" : "defaulted");
  r = first_kind(&lex, NLP_Q_RESISTANCE);
  c = first_kind(&lex, NLP_Q_CAPACITANCE);
  cur = first_kind(&lex, NLP_Q_CURRENT);
  freq = first_kind(&lex, NLP_Q_FREQUENCY);
  nv = voltage_span(&lex, &vmax, &vmin);

  /* Two-terminal R or C between A and B. */
  if ((r || c) && find_between(&lex, a, sizeof(a), b, sizeof(b)) == 0) {
    add_part(&d, "X1", r ? "resistor" : "capacitor", r ? r->value : c->value,
             a, b);
    return emit_design(&d, r ? "nlp_resistor" : "nlp_cap", pk, out_ir_path,
                       clarifying_question, clarify_len);
  }

  /* Pull-ups: I2C names both bus lines; otherwise the signal must be named. */
  if (r && has_any(&lex, w_pullup)) {
    if ((has_word(&lex, "I2C") || has_word(&lex, "i2c")) &&
        find_rail(&lex, rail, sizeof(rail)) == 0) {
      add_part(&d, "R_SDA", "resistor", r->value, rail, "SDA");
      add_part(&d, "R_SCL", "resistor", r->value, rail, "SCL");
      add_prov(&d, "signals", "inferred");
      return emit_design(&d, "nlp_i2c_pullups", pk, out_ir_path,
                         clarifying_question, clarify_len);
    }
    set_clarify(clarifying_question, clarify_len,
                "Pull-up needs rail and signal nodes (e.g. between 3V3 and SDA)");
    return 1;
  }

  /* Linear regulator: VIN = higher stated voltage, VOUT = lower. */
  if (has_any(&lex, w_reg) && nv >= 2 && vmax > vmin) {
    rail_name(vmax, rail, sizeof(rail));
    rail_name(vmin, r2, sizeof(r2));
    {
      NlpPart *u = add_part(&d, "U1", "ldo", vmin, rail, r2);
      u->pins[0] = "VIN";
      u->pins[1] = "VOUT";
      u->pins[2] = "GND";
      snprintf(u->nets[2], sizeof(u->nets[2]), "GND");
      u->n = 3;
    }
    add_part(&d, "C_OUT", "capacitor", 10e-6, r2, "GND");
    snprintf(d.measure, sizeof(d.measure), "%s", r2);
    d.mmin = vmin * 0.98;
    d.mmax = vmin * 1.02;
    d.limits = 1;
    add_prov(&d, "output_cap", "defaulted");
    add_prov(&d, "regulation_limit_pct", "defaulted");
    return emit_design(&d, "nlp_ldo", pk, out_ir_path, clarifying_question,
                       clarify_len);
  }

  /* N-MOS low-side switch: load rail = higher voltage, gate drive = lower. */
  if (has_any(&lex, w_mos) && has_word(&lex, "switch")) {
    NlpPart *q;
    double iload = cur ? cur->value : 0.1;
    if (nv < 2 || !(vmax > vmin)) {
      set_clarify(clarifying_question, clarify_len,
                  "Low-side switch needs the load voltage and the gate drive "
                  "voltage (e.g. 12V load, 3.3V control)");
      return 1;
    }
    rail_name(vmax, rail, sizeof(rail));
    rail_name(vmin, r2, sizeof(r2));
    {
      /* Rated 2x dissipation. DFM ampacity is checked at the rated power:
       * I_est = sqrt(P_rated / R) = sqrt(2) * iload. */
      NlpPart *rl =
          add_part(&d, "R_LOAD", "resistor", e24(vmax / iload), rail, "DRAIN");
      rl->power = 2.0 * vmax * iload;
      rl->pkg = chip_pkg_for_current(1.4143 * iload);
      if (!rl->pkg) {
        set_clarify(clarifying_question, clarify_len,
                    "Load current too high for an on-board load resistor; "
                    "state the load (current or resistance) under 0.25 A");
        return 1;
      }
      add_note(&d, "R_LOAD stands in for the external load");
    }
    add_part(&d, "R_G", "resistor", 100.0, r2, "GATE");
    add_part(&d, "R_PD", "resistor", 100e3, "GATE", "GND");
    q = add_part(&d, "Q1", "mosfet", 1.5, "GND", "GATE"); /* S, G, D */
    q->pins[0] = "S";
    q->pins[1] = "G";
    q->pins[2] = "D";
    snprintf(q->nets[2], sizeof(q->nets[2]), "DRAIN");
    q->n = 3;
    snprintf(d.measure, sizeof(d.measure), "DRAIN");
    d.mmin = 0.0;
    d.mmax = 0.5; /* "on": drain pulled near ground */
    d.limits = 1;
    add_prov(&d, "load_current", cur ? "explicit" : "defaulted");
    add_prov(&d, "gate_network", "defaulted");
    add_prov(&d, "on_state_limit", "inferred");
    return emit_design(&d, "nlp_lowside_switch", pk, out_ir_path,
                       clarifying_question, clarify_len);
  }

  /* Battery input with series-diode reverse-polarity protection. */
  if ((has_word(&lex, "battery") || has_word(&lex, "Battery")) &&
      has_any(&lex, w_revpol)) {
    double vbat = nv ? vmax : 3.7;
    NlpPart *bt = add_part(&d, "BT1", "battery", vbat, "VBAT", "GND");
    bt->pins[0] = "1";
    bt->pins[1] = "2";
    add_part(&d, "D_RP", "diode", 0.3, "VBAT", "VSYS"); /* Schottky Vf */
    add_part(&d, "C_BULK", "capacitor", 10e-6, "VSYS", "GND");
    snprintf(d.measure, sizeof(d.measure), "VSYS");
    d.mmin = vbat - 0.5;
    d.mmax = vbat;
    d.limits = 1;
    add_prov(&d, "battery_voltage", nv ? "explicit" : "defaulted");
    add_prov(&d, "protection", "defaulted");
    return emit_design(&d, "nlp_battery_revpol", pk, out_ir_path,
                       clarifying_question, clarify_len);
  }

  /* LED indicator: R = (Vrail - Vf) / I. */
  if (has_any(&lex, w_led) && has_any(&lex, w_ind)) {
    double v, iled = cur ? cur->value : 0.002;
    if (find_rail(&lex, rail, sizeof(rail)) != 0 ||
        compiler_rail_voltage(rail, &v) == RAIL_NONE || v <= 2.2) {
      set_clarify(clarifying_question, clarify_len,
                  "LED indicator needs the rail voltage (e.g. 3.3V or 3V3)");
      return 1;
    }
    add_part(&d, "R_LED", "resistor", e24((v - 2.0) / iled), rail, "LED_A");
    add_part(&d, "D_LED", "led", 2.0, "LED_A", "GND");
    add_prov(&d, "led_vf", "defaulted");
    add_prov(&d, "led_current", cur ? "explicit" : "defaulted");
    return emit_design(&d, "nlp_led_indicator", pk, out_ir_path,
                       clarifying_question, clarify_len);
  }

  /* Divider: explicit resistors, or synthesized from Vin → Vout target. */
  if (has_word(&lex, "divider") && (r || nv >= 2)) {
    double r1v, r2v;
    const char *top = "VIN";
    const char *mid = (has_word(&lex, "ADC") || has_word(&lex, "adc"))
                          ? "ADC_SENSE"
                          : "VOUT";
    if (find_rail(&lex, rail, sizeof(rail)) == 0)
      top = rail;
    if (find_from_to(&lex, a, sizeof(a), b, sizeof(b)) == 0 &&
        compiler_rail_voltage(a, NULL) != RAIL_NONE)
      top = a;
    if (has_word(&lex, "VOUT"))
      mid = "VOUT";
    if (r) {
      r1v = r2v = r->value;
      for (i = 0; i < lex.ntok; i++)
        if (&lex.tokens[i] != r && lex.tokens[i].kind == NLP_Q_RESISTANCE) {
          r2v = lex.tokens[i].value;
          break;
        }
      add_prov(&d, "resistors", "explicit");
    } else {
      r2v = 10e3;
      r1v = e24(r2v * (vmax / vmin - 1.0));
      rail_name(vmax, rail, sizeof(rail));
      top = rail;
      d.mmin = vmin * 0.95; /* "roughly": ±5 % */
      d.mmax = vmin * 1.05;
      d.limits = 1;
      add_prov(&d, "r_bottom", "defaulted");
      add_prov(&d, "r_top", "inferred");
      add_prov(&d, "target_tolerance_pct", "inferred");
    }
    add_part(&d, "r1", "resistor", r1v, top, mid);
    add_part(&d, "r2", "resistor", r2v, mid, "GND");
    snprintf(d.measure, sizeof(d.measure), "%s", mid);
    return emit_design(&d, "nlp_divider", pk, out_ir_path, clarifying_question,
                       clarify_len);
  }

  /* RC low-pass: explicit R and C, or C synthesized for the cutoff. */
  if (has_any(&lex, w_lpf) && ((r && c) || freq)) {
    double rv = r ? r->value : 10e3;
    double cv = c ? c->value
                  : e24(1.0 / (2.0 * 3.14159265358979 * rv * freq->value));
    add_part(&d, "r1", "resistor", rv, "VIN", "VOUT");
    add_part(&d, "c1", "capacitor", cv, "VOUT", "GND");
    snprintf(d.measure, sizeof(d.measure), "VOUT");
    add_prov(&d, "r", r ? "explicit" : "defaulted");
    add_prov(&d, "c", c ? "explicit" : "inferred");
    return emit_design(&d, "nlp_rc", pk, out_ir_path, clarifying_question,
                       clarify_len);
  }

  /* Decoupling: every stated capacitor from the rail to GND. */
  if (c && has_any(&lex, w_decouple)) {
    int n = 0;
    if (find_rail(&lex, rail, sizeof(rail)) != 0) {
      set_clarify(clarifying_question, clarify_len,
                  "Which rail is decoupled? Name it (3V3, 5V, VIN) or give "
                  "its voltage");
      return 1;
    }
    for (i = 0; i < lex.ntok && d.n < 8; i++)
      if (lex.tokens[i].kind == NLP_Q_CAPACITANCE) {
        char role[8];
        snprintf(role, sizeof(role), "C%d", ++n);
        add_part(&d, role, "capacitor", lex.tokens[i].value, rail, "GND");
      }
    if (cur)
      add_note(&d, "stated load current is not modeled (no load element)");
    return emit_design(&d, "nlp_decoupling", pk, out_ir_path,
                       clarifying_question, clarify_len);
  }

  /* R from A to B. */
  if (r && find_from_to(&lex, a, sizeof(a), b, sizeof(b)) == 0) {
    add_part(&d, "X1", "resistor", r->value, a, b);
    return emit_design(&d, "nlp_resistor", pk, out_ir_path, clarifying_question,
                       clarify_len);
  }

  if (has_any(&lex, w_vague)) {
    set_clarify(clarifying_question, clarify_len,
                "Ambiguous prompt; specify topology, values, and nodes");
    return 1;
  }

  set_clarify(clarifying_question, clarify_len,
              "Unsupported offline NLP pattern; clarify component, value, "
              "and nodes (between/from/to/divider/bypass/RC/LDO/switch/LED)");
  return 1;
}
