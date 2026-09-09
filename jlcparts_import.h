#pragma once

#include "db.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char **fields;
  int field_count;
} CsvRow;

/* Base CSV Parsing (State Machine) */
CsvRow *ParseCsvLine(const char *line);
void FreeCsvRow(CsvRow *row);

/* Main Import Function */
int ImportJLCPCBCsv(DB *db, const char *filepath);
