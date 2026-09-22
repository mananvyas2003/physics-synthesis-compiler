#include "emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Phase 18 PCB phase-one: deterministic placement + nets + outline.
 * No autoroute. Do not claim optimized PCB. */

static const char *pcb_footprint(PartTypes type, const char *package) {
  static char buf[96];
  if (!package || !package[0])
    return "Resistor_SMD:R_0603_1608Metric";
  if (type == PART_CAPACITOR) {
    if (strcmp(package, "0603") == 0)
      return "Capacitor_SMD:C_0603_1608Metric";
    snprintf(buf, sizeof(buf), "Capacitor_SMD:C_%s", package);
    return buf;
  }
  if (type == PART_INDUCTOR) {
    snprintf(buf, sizeof(buf), "Inductor_SMD:L_%s", package);
    return buf;
  }
  if (type == PART_DIODE) {
    if (strcmp(package, "0603") == 0)
      return "LED_SMD:LED_0603_1608Metric";
    snprintf(buf, sizeof(buf), "LED_SMD:LED_%s", package);
    return buf;
  }
  if (strcmp(package, "0402") == 0)
    return "Resistor_SMD:R_0402_1005Metric";
  if (strcmp(package, "0603") == 0)
    return "Resistor_SMD:R_0603_1608Metric";
  if (strcmp(package, "0805") == 0)
    return "Resistor_SMD:R_0805_2012Metric";
  if (strcmp(package, "1206") == 0)
    return "Resistor_SMD:R_1206_3216Metric";
  snprintf(buf, sizeof(buf), "Resistor_SMD:R_%s", package);
  return buf;
}

typedef struct {
  char name[64];
  int id; /* 1-based KiCad net number */
} PcbNet;

static int find_or_add_net(PcbNet *nets, int *nnets, int cap, const char *name) {
  int i;
  if (!name || !name[0])
    return 0;
  for (i = 0; i < *nnets; i++) {
    if (strcmp(nets[i].name, name) == 0)
      return nets[i].id;
  }
  if (*nnets >= cap)
    return 0;
  strncpy(nets[*nnets].name, name, sizeof(nets[0].name) - 1);
  nets[*nnets].name[sizeof(nets[0].name) - 1] = '\0';
  nets[*nnets].id = *nnets + 1;
  (*nnets)++;
  return nets[*nnets - 1].id;
}

static const char *comp_net(const CompiledComponent *cc, int pin_idx) {
  if (pin_idx >= 0 && pin_idx < 8 && cc->nodes[pin_idx][0])
    return cc->nodes[pin_idx];
  if (pin_idx == 0 && cc->node1[0])
    return cc->node1;
  if (pin_idx == 1 && cc->node2[0])
    return cc->node2;
  if (pin_idx == 2 && cc->node3[0])
    return cc->node3;
  return "";
}

static void write_smd_pad(FILE *fp, const char *num, double x, double y,
                          int net_id, const char *net_name) {
  fprintf(fp,
          "\t\t(pad \"%s\" smd roundrect\n"
          "\t\t\t(at %.3f %.3f)\n"
          "\t\t\t(size 0.8 0.95)\n"
          "\t\t\t(layers \"F.Cu\" \"F.Paste\" \"F.Mask\")\n"
          "\t\t\t(roundrect_rratio 0.25)\n",
          num, x, y);
  if (net_id > 0 && net_name && net_name[0])
    fprintf(fp, "\t\t\t(net %d \"%s\")\n", net_id, net_name);
  fprintf(fp, "\t\t)\n");
}

int emit_kicad_pcb(const char *path, const CompiledSchematic *schematic) {
  FILE *fp;
  PcbNet nets[128];
  int nnets = 0;
  int i, p;
  double board_w = 50.0;
  double board_h = 40.0;
  double pitch = 5.0;
  double origin_x = 10.0;
  double origin_y = 10.0;
  int cols;

  if (!path || !schematic || schematic->component_count <= 0)
    return 0;

  memset(nets, 0, sizeof(nets));
  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *cc = &schematic->components[i];
    int pc = cc->pin_count > 0 ? cc->pin_count : 2;
    for (p = 0; p < pc && p < 8; p++)
      find_or_add_net(nets, &nnets, 128, comp_net(cc, p));
  }

  cols = schematic->component_count > 4 ? 4 : schematic->component_count;
  if (cols < 1)
    cols = 1;
  board_w = origin_x * 2.0 + (cols - 1) * pitch + 10.0;
  board_h = origin_y * 2.0 +
            ((schematic->component_count + cols - 1) / cols - 1) * pitch + 10.0;
  if (board_w < 30.0)
    board_w = 30.0;
  if (board_h < 20.0)
    board_h = 20.0;

  fp = fopen(path, "w");
  if (!fp)
    return 0;

  fprintf(fp,
          "(kicad_pcb\n"
          "\t(version 20221018)\n"
          "\t(generator \"physics-synthesis-compiler\")\n"
          "\t(generator_version \"phase18\")\n"
          "\t(general\n"
          "\t\t(thickness 1.6)\n"
          "\t)\n"
          "\t(paper \"A4\")\n"
          "\t(layers\n"
          "\t\t(0 \"F.Cu\" signal)\n"
          "\t\t(31 \"B.Cu\" signal)\n"
          "\t\t(36 \"B.SilkS\" user \"B.Silkscreen\")\n"
          "\t\t(37 \"B.Mask\" user)\n"
          "\t\t(38 \"F.SilkS\" user \"F.Silkscreen\")\n"
          "\t\t(39 \"F.Mask\" user)\n"
          "\t\t(44 \"Edge.Cuts\" user)\n"
          "\t)\n"
          "\t(net 0 \"\")\n");

  for (i = 0; i < nnets; i++)
    fprintf(fp, "\t(net %d \"%s\")\n", nets[i].id, nets[i].name);

  /* Board outline on Edge.Cuts */
  fprintf(fp,
          "\t(gr_rect\n"
          "\t\t(start 0 0)\n"
          "\t\t(end %.3f %.3f)\n"
          "\t\t(stroke (width 0.1) (type default))\n"
          "\t\t(fill none)\n"
          "\t\t(layer \"Edge.Cuts\")\n"
          "\t\t(uuid \"00000000-0000-4000-8000-pcb000000001\")\n"
          "\t)\n",
          board_w, board_h);

  for (i = 0; i < schematic->component_count; i++) {
    const CompiledComponent *cc = &schematic->components[i];
    const char *fp_id = pcb_footprint(cc->part.type, cc->part.package);
    const char *ref = cc->role[0] ? cc->role : "U?";
    int pc = cc->pin_count > 0 ? cc->pin_count : 2;
    int col = i % cols;
    int row = i / cols;
    double x = origin_x + col * pitch;
    double y = origin_y + row * pitch;
    char uuid[64];

    snprintf(uuid, sizeof(uuid), "00000000-0000-4000-8000-%012d", 1000 + i);

    fprintf(fp,
            "\t(footprint \"%s\"\n"
            "\t\t(layer \"F.Cu\")\n"
            "\t\t(uuid \"%s\")\n"
            "\t\t(at %.3f %.3f)\n"
            "\t\t(property \"Reference\" \"%s\"\n"
            "\t\t\t(at 0 -1.5 0)\n"
            "\t\t\t(layer \"F.SilkS\")\n"
            "\t\t\t(uuid \"%s-ref\")\n"
            "\t\t\t(effects (font (size 0.8 0.8))))\n"
            "\t\t(property \"Value\" \"%.6g\"\n"
            "\t\t\t(at 0 1.5 0)\n"
            "\t\t\t(layer \"F.Fab\")\n"
            "\t\t\t(uuid \"%s-val\")\n"
            "\t\t\t(effects (font (size 0.8 0.8))))\n"
            "\t\t(property \"Footprint\" \"%s\"\n"
            "\t\t\t(at 0 0 0)\n"
            "\t\t\t(unlocked yes)\n"
            "\t\t\t(layer \"F.Fab\")\n"
            "\t\t\t(uuid \"%s-fp\")\n"
            "\t\t\t(effects (font (size 1.27 1.27)) (hide yes)))\n"
            "\t\t(attr smd)\n",
            fp_id, uuid, x, y, ref, uuid, cc->part.value, uuid, fp_id, uuid);

    /* Inline 2-pad SMD geometry (library-independent); pin1 left, pin2 right. */
    for (p = 0; p < pc && p < 8; p++) {
      const char *nn = comp_net(cc, p);
      int nid = find_or_add_net(nets, &nnets, 128, nn);
      char pnum[8];
      double px = (p == 0) ? -0.75 : (p == 1) ? 0.75 : (p % 2 ? 0.75 : -0.75);
      double py = (p < 2) ? 0.0 : ((p / 2) * 0.5);
      snprintf(pnum, sizeof(pnum), "%d", p + 1);
      write_smd_pad(fp, pnum, px, py, nid, nn);
    }

    fprintf(fp, "\t)\n");
  }

  fprintf(fp, ")\n");
  fclose(fp);
  return 1;
}
