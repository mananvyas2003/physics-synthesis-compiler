#include "emit.h"

#include <stdio.h>
#include <string.h>

/*
 * KiCad legacy S-expression netlist (simplified, openable).
 * Components and nets derived from CompiledSchematic.
 */
int emit_ki_cad_netlist(const char *path, const CompiledSchematic *schematic) {
  FILE *fp;
  int i;
  char nets[64][64];
  int net_count = 0;

  if (!path || !schematic)
    return 0;

  fp = fopen(path, "wb");
  if (!fp)
    return 0;

  fprintf(fp, "(export (version \"E\")\n");
  fprintf(fp, "  (design\n");
  fprintf(fp, "    (source \"%s\")\n", schematic->name);
  fprintf(fp, "    (date \"generated\")\n");
  fprintf(fp, "    (tool \"physics-synthesis-compiler\")\n");
  fprintf(fp, "  )\n");
  fprintf(fp, "  (components\n");

  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    fprintf(fp, "    (comp (ref \"%s\")\n", c->role);
    fprintf(fp, "      (value \"%.12g\")\n", c->part.value);
    fprintf(fp, "      (footprint \"%s\")\n", c->part.package);
    fprintf(fp, "      (fields\n");
    fprintf(fp, "        (field (name \"MPN\") \"%s\")\n", c->part.mpn);
    fprintf(fp, "      )\n");
    fprintf(fp, "      (libsource (lib \"Device\") (part \"R\") (description \"\"))\n");
    fprintf(fp, "    )\n");
  }

  fprintf(fp, "  )\n");
  fprintf(fp, "  (nets\n");

  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *c = &schematic->components[i];
    int n;
    int found1 = 0;
    int found2 = 0;

    for (n = 0; n < net_count; n++) {
      if (strcmp(nets[n], c->node1) == 0)
        found1 = 1;
      if (strcmp(nets[n], c->node2) == 0)
        found2 = 1;
    }

    if (!found1 && net_count < 64) {
      strncpy(nets[net_count], c->node1, sizeof(nets[0]) - 1);
      nets[net_count][sizeof(nets[0]) - 1] = '\0';
      net_count++;
    }
    if (!found2 && net_count < 64) {
      strncpy(nets[net_count], c->node2, sizeof(nets[0]) - 1);
      nets[net_count][sizeof(nets[0]) - 1] = '\0';
      net_count++;
    }
  }

  for (i = 0; i < net_count; i++) {
    int cidx;
    fprintf(fp, "    (net (code \"%d\") (name \"%s\")\n", i + 1, nets[i]);

    for (cidx = 0; cidx < schematic->component_count; cidx++) {
      const CompiledComponent *c = &schematic->components[cidx];
      if (strcmp(c->node1, nets[i]) == 0)
        fprintf(fp, "      (node (ref \"%s\") (pin \"%s\"))\n", c->role,
                c->pin1);
      if (strcmp(c->node2, nets[i]) == 0)
        fprintf(fp, "      (node (ref \"%s\") (pin \"%s\"))\n", c->role,
                c->pin2);
    }

    fprintf(fp, "    )\n");
  }

  fprintf(fp, "  )\n");
  fprintf(fp, ")\n");
  fclose(fp);
  return 1;
}
