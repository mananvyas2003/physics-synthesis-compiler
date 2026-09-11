#include "emit.h"

#include <stdio.h>
#include <string.h>

static void csv_escape(FILE *fp, const char *text) {
  int need_quotes = 0;
  const char *p;

  if (!text)
    text = "";

  for (p = text; *p; p++) {
    if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
      need_quotes = 1;
      break;
    }
  }

  if (!need_quotes) {
    fputs(text, fp);
    return;
  }

  fputc('"', fp);
  for (p = text; *p; p++) {
    if (*p == '"')
      fputc('"', fp);
    fputc(*p, fp);
  }
  fputc('"', fp);
}

int emit_bom_csv(const char *path, const CompiledSchematic *schematic) {
  FILE *fp;
  int i;

  if (!path || !schematic)
    return 0;

  fp = fopen(path, "wb");
  if (!fp)
    return 0;

  fprintf(fp,
          "refdes,value,package,MPN,JLCPCB,quantity,rationale,alternate_mpn,"
          "unit_cost\n");

  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    char value_buf[64];
    char cost_buf[64];

    snprintf(value_buf, sizeof(value_buf), "%.12g", c->part.value);
    snprintf(cost_buf, sizeof(cost_buf), "%.6g", c->unit_cost);

    csv_escape(fp, c->role);
    fputc(',', fp);
    csv_escape(fp, value_buf);
    fputc(',', fp);
    csv_escape(fp, c->part.package);
    fputc(',', fp);
    csv_escape(fp, c->part.mpn);
    fputc(',', fp);
    /* JLCPCB id: use MPN when that is the catalogue key. */
    csv_escape(fp, c->part.mpn);
    fputc(',', fp);
    fprintf(fp, "1,");
    csv_escape(fp, c->rationale[0] ? c->rationale : "n/a");
    fputc(',', fp);
    csv_escape(fp, c->has_alternate ? c->alternate_mpn : "");
    fputc(',', fp);
    csv_escape(fp, cost_buf);
    fputc('\n', fp);
  }

  fclose(fp);
  return 1;
}
