#ifndef PART_LIB_H
#define PART_LIB_H

#include "db.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PART_LIB_MAX_PINS 8

typedef enum {
  PIN_PASSIVE = 0,
  PIN_INPUT,
  PIN_OUTPUT,
  PIN_BIDIR,
  PIN_POWER_IN,
  PIN_POWER_OUT,
  PIN_GND,
  PIN_ANODE,
  PIN_CATHODE
} PinElectrical;

typedef struct {
  const char *name; /* canonical pin name stored in connections */
  const char *const *aliases;
  int alias_count;
  PinElectrical electrical;
} PartLibPin;

typedef struct {
  const char *type_name; /* IR part_type / parts[].type */
  PartTypes db_type;
  int pin_count;
  const PartLibPin *pins;
  const char *kicad_lib_id;
  const char *value_unit; /* "ohm", "farad", "henry", "volt", "none" */
  double default_target;
  int compile_ok; /* 0 = validate/bind only with clear reject */
} PartLibEntry;

const PartLibEntry *part_lib_find(const char *type_name);
PartTypes part_lib_db_type(const char *type_name);
int part_lib_supported(const char *type_name);
int part_lib_normalize_pin(const char *type_name, const char *pin,
                           char *out, size_t out_len);
int part_lib_required_pins(const char *type_name, const char **out_names,
                           int max_names);
const char *part_lib_kicad_id(PartTypes type);
const char *part_lib_type_label(PartTypes type);

#ifdef __cplusplus
}
#endif

#endif
