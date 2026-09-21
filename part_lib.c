#include "part_lib.h"

#include <stdio.h>
#include <string.h>

static const char *a_1[] = {"1", "A", "a", "anode", "p", "P", "+"};
static const char *a_2[] = {"2", "K", "k", "cathode", "n", "N", "-"};
static const char *a_e[] = {"E", "e", "emitter", "S", "s", "source"};
static const char *a_b[] = {"B", "b", "base", "G", "g", "gate"};
static const char *a_c[] = {"C", "c", "collector", "D", "d", "drain"};
static const char *a_vin[] = {"VIN", "vin", "IN"};
static const char *a_vout[] = {"VOUT", "vout", "OUT"};
static const char *a_gnd[] = {"GND", "gnd", "VSS"};
static const char *a_inp[] = {"IN+", "INP", "inp", "+"};
static const char *a_inn[] = {"IN-", "INN", "inn", "-"};
static const char *a_out[] = {"OUT", "out", "OUTPUT"};
static const char *a_vcc[] = {"VCC", "vcc", "VDD", "vdd"};
static const char *a_vee[] = {"VEE", "vee", "VSS", "GND", "gnd"};
static const char *a_sw1[] = {"1", "COM", "com", "A"};
static const char *a_sw2[] = {"2", "NO", "no", "B"};

static const PartLibPin pins_2passive[] = {
    {"1", a_1, 7, PIN_PASSIVE},
    {"2", a_2, 7, PIN_PASSIVE},
};

static const PartLibPin pins_diode[] = {
    {"1", a_1, 7, PIN_ANODE},
    {"2", a_2, 7, PIN_CATHODE},
};

static const PartLibPin pins_bjt[] = {
    {"E", a_e, 6, PIN_PASSIVE},
    {"B", a_b, 6, PIN_INPUT},
    {"C", a_c, 6, PIN_PASSIVE},
};

static const PartLibPin pins_ldo[] = {
    {"VIN", a_vin, 3, PIN_POWER_IN},
    {"VOUT", a_vout, 3, PIN_POWER_OUT},
    {"GND", a_gnd, 3, PIN_GND},
};

static const PartLibPin pins_opamp[] = {
    {"IN+", a_inp, 4, PIN_INPUT},
    {"IN-", a_inn, 4, PIN_INPUT},
    {"OUT", a_out, 3, PIN_OUTPUT},
    {"VCC", a_vcc, 4, PIN_POWER_IN},
    {"VEE", a_vee, 5, PIN_GND},
};

static const PartLibPin pins_switch[] = {
    {"1", a_sw1, 4, PIN_PASSIVE},
    {"2", a_sw2, 4, PIN_PASSIVE},
};

static const PartLibPin pins_battery[] = {
    {"1", a_1, 7, PIN_POWER_OUT},
    {"2", a_2, 7, PIN_GND},
};

static const PartLibPin pins_connector[] = {
    {"1", a_1, 4, PIN_BIDIR},
    {"2", a_2, 4, PIN_BIDIR},
};

static const PartLibEntry g_entries[] = {
    {"resistor", PART_RESISTOR, 2, pins_2passive, "Device:R", "ohm", 10000.0, 1},
    {"capacitor", PART_CAPACITOR, 2, pins_2passive, "Device:C", "farad", 100e-9,
     1},
    {"inductor", PART_INDUCTOR, 2, pins_2passive, "Device:L", "henry", 10e-6, 1},
    {"diode", PART_DIODE, 2, pins_diode, "Device:D", "volt", 0.7, 1},
    {"led", PART_DIODE, 2, pins_diode, "Device:LED", "volt", 2.0, 1},
    {"transistor", PART_TRANSISTOR, 3, pins_bjt, "Device:Q_NPN_BCE", "none", 1.0,
     1},
    {"bjt", PART_TRANSISTOR, 3, pins_bjt, "Device:Q_NPN_BCE", "none", 1.0, 1},
    {"mosfet", PART_TRANSISTOR, 3, pins_bjt, "Device:Q_NMOS_GSD", "none", 1.0, 1},
    {"opamp", PART_IC, 5, pins_opamp, "Device:OpAmp", "none", 1.0, 1},
    {"regulator", PART_IC, 3, pins_ldo, "Device:LDO", "volt", 3.3, 1},
    {"ldo", PART_IC, 3, pins_ldo, "Device:LDO", "volt", 3.3, 1},
    {"switch", PART_CONNECTOR, 2, pins_switch, "Device:SW", "none", 1.0, 1},
    {"battery", PART_OTHER, 2, pins_battery, "Device:Battery", "volt", 3.7, 1},
    {"connector", PART_CONNECTOR, 2, pins_connector, "Device:Conn", "none", 1.0,
     1},
};

static const int g_entry_count = (int)(sizeof(g_entries) / sizeof(g_entries[0]));

const PartLibEntry *part_lib_find(const char *type_name) {
  int i;
  if (!type_name)
    return NULL;
  for (i = 0; i < g_entry_count; i++) {
    if (strcmp(g_entries[i].type_name, type_name) == 0)
      return &g_entries[i];
  }
  return NULL;
}

PartTypes part_lib_db_type(const char *type_name) {
  const PartLibEntry *e = part_lib_find(type_name);
  return e ? e->db_type : PART_OTHER;
}

int part_lib_supported(const char *type_name) {
  const PartLibEntry *e = part_lib_find(type_name);
  return e && e->compile_ok;
}

int part_lib_normalize_pin(const char *type_name, const char *pin, char *out,
                           size_t out_len) {
  const PartLibEntry *e;
  int i, a;
  if (!pin || !out || out_len == 0)
    return 1;
  e = part_lib_find(type_name);
  if (!e) {
    if (strcmp(pin, "A") == 0 || strcmp(pin, "a") == 0 ||
        strcmp(pin, "anode") == 0) {
      snprintf(out, out_len, "1");
      return 0;
    }
    if (strcmp(pin, "K") == 0 || strcmp(pin, "k") == 0 ||
        strcmp(pin, "cathode") == 0) {
      snprintf(out, out_len, "2");
      return 0;
    }
    snprintf(out, out_len, "%s", pin);
    return 0;
  }
  for (i = 0; i < e->pin_count; i++) {
    if (strcmp(e->pins[i].name, pin) == 0) {
      snprintf(out, out_len, "%s", e->pins[i].name);
      return 0;
    }
    for (a = 0; a < e->pins[i].alias_count; a++) {
      if (strcmp(e->pins[i].aliases[a], pin) == 0) {
        snprintf(out, out_len, "%s", e->pins[i].name);
        return 0;
      }
    }
  }
  return 1;
}

int part_lib_required_pins(const char *type_name, const char **out_names,
                           int max_names) {
  const PartLibEntry *e = part_lib_find(type_name);
  int i;
  if (!e || !out_names || max_names < e->pin_count)
    return -1;
  for (i = 0; i < e->pin_count; i++)
    out_names[i] = e->pins[i].name;
  return e->pin_count;
}

const char *part_lib_kicad_id(PartTypes type) {
  switch (type) {
  case PART_CAPACITOR:
    return "Device:C";
  case PART_INDUCTOR:
    return "Device:L";
  case PART_DIODE:
    return "Device:LED";
  case PART_TRANSISTOR:
    return "Device:Q_NPN_BCE";
  case PART_IC:
    return "Device:LDO";
  case PART_CONNECTOR:
    return "Device:SW";
  case PART_OTHER:
    return "Device:Battery";
  case PART_RESISTOR:
  default:
    return "Device:R";
  }
}

const char *part_lib_type_label(PartTypes type) {
  switch (type) {
  case PART_RESISTOR:
    return "resistor";
  case PART_CAPACITOR:
    return "capacitor";
  case PART_INDUCTOR:
    return "inductor";
  case PART_DIODE:
    return "diode/LED";
  case PART_TRANSISTOR:
    return "transistor/MOSFET";
  case PART_IC:
    return "IC/op-amp/regulator";
  case PART_CONNECTOR:
    return "connector/switch";
  case PART_OTHER:
    return "battery/other";
  default:
    return "unsupported";
  }
}
