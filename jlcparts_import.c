#include "jlcparts_import.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Using cJSON to extract the embedded specifications */
#include "cJSON.h"

#define MAX_FIELDS 32
#define MAX_FIELD_LEN 8192

/* --------------------------------------------------------- */
/* 1. DATA-DRIVEN CATEGORY MAPPING TABLE                     */
/* --------------------------------------------------------- */

typedef struct {
  const char *name1; /* jlcparts extra.category.name1 */
  const char *name2; /* extra.category.name2, or NULL to match any */
  PartTypes part_type;
  const char *value_key;
  const char *v_rating_key;
  const char *i_rating_key;
  const char *power_key; /* NEW: Power rating */
  const char *tol_key;   /* NEW: Tolerance class */
} CategoryRule;

static char *DuplicateString(const char *src) {
  size_t len;
  char *dst;

  if (src == NULL)
    return NULL;

  len = strlen(src);

  dst = malloc(len + 1);
  if (dst == NULL)
    return NULL;

  memcpy(dst, src, len + 1);

  return dst;
}

static const CategoryRule CATEGORY_RULES[] = {
    /* --- DC / PASSIVE COMPONENTS --- */
    {"Resistors", NULL, PART_RESISTOR, "Resistance", "Overload Voltage (Max)",
     NULL, "Power Per Element", "Tolerance"},

    {"Capacitors", NULL, PART_CAPACITOR, "Capacitance", "Voltage Rated", NULL,
     NULL, "Tolerance"},

    {"Inductors, Coils, Chokes", NULL, PART_INDUCTOR, "Inductance", NULL,
     "Rated Current", NULL, "Tolerance"},

    /* --- SEMICONDUCTORS --- */
    {"Transistors/Thyristors", "MOSFETs", PART_TRANSISTOR, NULL,
     "Drain Source Voltage (Vdss)", "Continuous Drain Current (Id)",
     "Power Dissipation (Pd)", NULL},

    {"Diodes", NULL, PART_DIODE, "Voltage - DC Reverse(Vr)",
     "Voltage - DC Reverse(Vr)", "Current - Rectified", NULL, NULL},

    /* --- AC / IEC SAFETY COMPONENTS (NEW) --- */

    /* MOVs / Varistors (Overvoltage Protection) */
    {"Circuit Protection", "Varistors, MOVs", PART_OTHER,
     "Varistor Voltage (Min)", "Maximum AC Volts", "Peak Surge Current", NULL,
     NULL},

    /* Fuses (Overcurrent Protection) */
    {"Circuit Protection", "Fuses", PART_OTHER, NULL, "Voltage Rating - AC",
     "Current Rating", NULL, NULL},

    /* Relays (Switching) */
    {"Relays", "Power Relays", PART_OTHER, "Coil Voltage",
     "Switching Voltage (Max)", "Switching Current (Max)", NULL, NULL},

    /* Bridge Rectifiers (AC to DC) */
    {"Diodes", "Bridge Rectifiers", PART_DIODE, "Voltage - Peak Reverse (Max)",
     "Voltage - Peak Reverse (Max)", "Current - Average Rectified (Io)", NULL,
     NULL},

    /* Transformers */
    {"Transformers", NULL, PART_OTHER, "Primary Voltage", "Secondary Voltage",
     "Current Rating", NULL, NULL}};
#define CATEGORY_RULE_COUNT (sizeof(CATEGORY_RULES) / sizeof(CATEGORY_RULES[0]))

static ToleranceClass ParseToleranceString(const char *str) {
  if (!str)
    return TOLERANCE_E24; /* Safe default */

  if (strstr(str, "0.1%") || strstr(str, "0.5%"))
    return TOLERANCE_E192;
  if (strstr(str, "1%"))
    return TOLERANCE_E96;
  if (strstr(str, "2%"))
    return TOLERANCE_E48;
  if (strstr(str, "5%"))
    return TOLERANCE_E24;
  if (strstr(str, "10%") || strstr(str, "20%"))
    return TOLERANCE_E12;

  return TOLERANCE_E24; /* Standard fallback for bulk parts */
}

/* Helper to safely extract string values for custom parsing */
static void ExtractAttributeString(cJSON *extra, const char *key, char *out_buf,
                                   size_t max_len) {
  if (!extra || !key || !out_buf)
    return;
  cJSON *attributes = cJSON_GetObjectItemCaseSensitive(extra, "attributes");
  if (!attributes)
    return;
  cJSON *item = cJSON_GetObjectItemCaseSensitive(attributes, key);
  if (item && cJSON_IsString(item)) {
    strncpy(out_buf, item->valuestring, max_len - 1);
    out_buf[max_len - 1] = '\0';
  }
}

/* --------------------------------------------------------- */
/* 2. BASE CSV PARSER (State Machine)                        */
/* --------------------------------------------------------- */

CsvRow *ParseCsvLine(const char *line) {
  CsvRow *row = malloc(sizeof(CsvRow));
  if (!row)
    return NULL;

  row->fields = malloc(sizeof(char *) * MAX_FIELDS);
  row->field_count = 0;

  char field_buf[MAX_FIELD_LEN];
  int buf_pos = 0;
  int in_quotes = 0;

  for (const char *p = line; *p != '\0'; p++) {
    char c = *p;

    if (in_quotes) {
      if (c == '"') {
        if (*(p + 1) == '"') {
          field_buf[buf_pos++] = '"';
          p++;
        } else {
          in_quotes = 0;
        }
      } else {
        field_buf[buf_pos++] = c;
      }
    } else {
      if (c == '"') {
        in_quotes = 1;
      } else if (c == ',') {
        field_buf[buf_pos] = '\0';
        row->fields[row->field_count] = DuplicateString(field_buf);
        row->field_count++;
        buf_pos = 0;
      } else {
        field_buf[buf_pos++] = c;
      }
    }
  }

  field_buf[buf_pos] = '\0';

  /* Strip trailing newline characters from the last field if they exist */
  int len = strlen(field_buf);
  while (len > 0 &&
         (field_buf[len - 1] == '\n' || field_buf[len - 1] == '\r')) {
    field_buf[len - 1] = '\0';
    len--;
  }

  row->fields[row->field_count] = DuplicateString(field_buf);
  row->field_count++;

  return row;
}

void FreeCsvRow(CsvRow *row) {
  if (!row)
    return;
  for (int i = 0; i < row->field_count; i++) {
    free(row->fields[i]);
  }
  free(row->fields);
  free(row);
}

/* --------------------------------------------------------- */
/* 3. JSON EXTRACTION & ENGINEERING VALUE PARSER             */
/* --------------------------------------------------------- */

static double ParseUnitValue(const char *str) {
  if (!str || str[0] == '-')
    return 0.0; /* Handle "-" as 0.0 missing data */

  char *endptr;
  double val = strtod(str, &endptr);
  if (endptr == str)
    return 0.0;

  while (*endptr == ' ')
    endptr++;

  /* Specifically handle the 2-byte UTF-8 micro symbol (µ) */
  if ((unsigned char)endptr[0] == 0xC2 && (unsigned char)endptr[1] == 0xB5) {
    return val * 1e-6;
  }

  switch (*endptr) {
  case 'p':
    return val * 1e-12;
  case 'n':
    return val * 1e-9;
  case 'u':
    return val * 1e-6;
  case 'm':
    return val * 1e-3; /* Catches mΩ, mA, mV */
  case 'k':
  case 'K':
    return val * 1e3;
  case 'M':
    return val * 1e6;
  case 'G':
    return val * 1e9;
  default:
    return val;
  }
}

static void ExtractAttributeValue(cJSON *extra, const char *key,
                                  double *out_value) {
  if (!extra || !key || !out_value)
    return;

  cJSON *attributes = cJSON_GetObjectItemCaseSensitive(extra, "attributes");
  if (!attributes)
    return;

  cJSON *item = cJSON_GetObjectItemCaseSensitive(attributes, key);
  if (item && cJSON_IsString(item)) {
    *out_value = ParseUnitValue(item->valuestring);
  }
}

/* --------------------------------------------------------- */
/* 4. ROW IMPORT LOGIC                                       */
/* --------------------------------------------------------- */

bool ImportJlcPartsRow(const char *mfr, const char *package, const char *name1,
                       const char *name2, cJSON *extra, DBPart *out) {
  memset(out, 0, sizeof(DBPart));

  for (size_t i = 0; i < CATEGORY_RULE_COUNT; i++) {
    const CategoryRule *rule = &CATEGORY_RULES[i];

    if (strcmp(name1, rule->name1) != 0)
      continue;
    if (rule->name2 && strcmp(name2, rule->name2) != 0)
      continue;

    double value = 0.0, v_rating = 0.0, i_rating = 0.0, power = 0.0;
    char tol_str[32] = {0};

    if (rule->value_key)
      ExtractAttributeValue(extra, rule->value_key, &value);
    if (rule->v_rating_key)
      ExtractAttributeValue(extra, rule->v_rating_key, &v_rating);
    if (rule->i_rating_key)
      ExtractAttributeValue(extra, rule->i_rating_key, &i_rating);
    if (rule->power_key)
      ExtractAttributeValue(extra, rule->power_key, &power);
    if (rule->tol_key)
      ExtractAttributeString(extra, rule->tol_key, tol_str, sizeof(tol_str));

    strncpy(out->mpn, mfr, sizeof(out->mpn) - 1);
    strncpy(out->package, package, sizeof(out->package) - 1);
    out->type = rule->part_type;
    out->value = value;
    out->v_rating = v_rating;
    out->i_rating = i_rating;
    out->power_rating_w = power;
    out->tolerance_class = ParseToleranceString(tol_str);

    return true; /* Match found and parsed */
  }
  return false;
}

/* --------------------------------------------------------- */
/* 5. MAIN FILE PROCESSING LOOP                              */
/* --------------------------------------------------------- */

int ImportJLCPCBCsv(DB *db, const char *filepath) {
  FILE *file = fopen(filepath, "r");
  if (!file) {
    fprintf(stderr, "Failed to open %s\n", filepath);
    return 0;
  }

  /* Wrap the entire import in a single transaction using the public API */
  if (db) {
    DB_BeginTransaction(db);
  }

  char line_buf[16384]; /* Large buffer to safely hold long CSV lines */
  int rows_inserted = 0;
  int is_first_line = 1;

  while (fgets(line_buf, sizeof(line_buf), file)) {
    if (is_first_line) {
      is_first_line = 0;
      continue;
    }

    CsvRow *row = ParseCsvLine(line_buf);
    if (!row)
      continue;

    /* Index 15 is the JSON "extra" column in this specific CSV export */
    if (row->field_count > 15) {
      const char *mfr_part = row->fields[4];
      const char *package = row->fields[5];
      const char *extra_str = row->fields[15];

      if (strlen(extra_str) > 5) {
        cJSON *extra = cJSON_Parse(extra_str);
        if (extra) {
          cJSON *category = cJSON_GetObjectItemCaseSensitive(extra, "category");
          if (category) {
            cJSON *name1_item =
                cJSON_GetObjectItemCaseSensitive(category, "name1");
            cJSON *name2_item =
                cJSON_GetObjectItemCaseSensitive(category, "name2");

            const char *name1 =
                cJSON_IsString(name1_item) ? name1_item->valuestring : "";
            const char *name2 =
                cJSON_IsString(name2_item) ? name2_item->valuestring : "";

            DBPart part;
            if (ImportJlcPartsRow(mfr_part, package, name1, name2, extra,
                                  &part)) {
              if (DB_InsertPartFull(db, &part, NULL) == DB_OK) {
                rows_inserted++;
              }
            }
          }
          cJSON_Delete(extra);
        }
      }
    }
    FreeCsvRow(row);
  }

  fclose(file);

  if (db) {
    DB_CommitTransaction(db);
  }

  return rows_inserted;
}
