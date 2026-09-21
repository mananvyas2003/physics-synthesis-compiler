#include "unit_parse.h"

#include "cJSON.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int unit_parse_si(const char *text, double *out) {
  char buf[64];
  size_t n;
  size_t i;
  size_t j = 0;
  double mag;
  char *end = NULL;
  double mult = 1.0;

  if (!text || !out)
    return 1;

  while (*text && isspace((unsigned char)*text))
    text++;
  n = strlen(text);
  while (n > 0 && isspace((unsigned char)text[n - 1]))
    n--;
  if (n == 0 || n >= sizeof(buf))
    return 1;

  for (i = 0; i < n; i++) {
    if (text[i] == ',' || text[i] == ' ')
      continue;
    buf[j++] = text[i];
  }
  buf[j] = '\0';

  mag = strtod(buf, &end);
  if (end == buf || !isfinite(mag))
    return 1;

  while (*end && isspace((unsigned char)*end))
    end++;

  if (*end == '\0') {
    *out = mag;
    return 0;
  }

  /* SI / engineering prefixes, then optional unit letter */
  if (strncmp(end, "Meg", 3) == 0 || strncmp(end, "meg", 3) == 0) {
    mult = 1e6;
    end += 3;
  } else if (*end == 'M') {
    mult = 1e6;
    end++;
  } else {
    char p = (char)tolower((unsigned char)*end);
    switch (p) {
    case 'p':
      mult = 1e-12;
      end++;
      break;
    case 'n':
      mult = 1e-9;
      end++;
      break;
    case 'u':
      mult = 1e-6;
      end++;
      break;
    case 'm':
      mult = 1e-3;
      end++;
      break;
    case 'k':
      mult = 1e3;
      end++;
      break;
    case 'g':
      mult = 1e9;
      end++;
      break;
    default:
      break;
    }
  }

  while (*end && isspace((unsigned char)*end))
    end++;

  /* Optional bare unit tokens */
  if (*end) {
    char u = (char)tolower((unsigned char)*end);
    if (u == 'o' || u == 'r' || u == 'f' || u == 'v' || u == 'a' || u == 'w' ||
        u == 'h') {
      /* ohm / farad / volt / amp / watt / henry / hz — ignore */
      end++;
      if ((u == 'o' && (end[0] == 'h' || end[0] == 'H')) ||
          (u == 'h' && (end[0] == 'z' || end[0] == 'Z')))
        end++;
    } else if ((unsigned char)end[0] == 0xCE && (unsigned char)end[1] == 0xA9) {
      end += 2; /* UTF-8 Ω */
    } else {
      return 1;
    }
  }

  while (*end && isspace((unsigned char)*end))
    end++;
  if (*end != '\0')
    return 1;

  *out = mag * mult;
  return 0;
}

int unit_parse_number_or_string(const void *cjson_item, double *out) {
  const cJSON *item = (const cJSON *)cjson_item;
  if (!item || !out)
    return 1;
  if (cJSON_IsNumber(item)) {
    *out = item->valuedouble;
    return 0;
  }
  if (cJSON_IsString(item) && item->valuestring)
    return unit_parse_si(item->valuestring, out);
  return 1;
}
