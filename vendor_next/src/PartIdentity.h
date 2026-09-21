#include "range.h"

typedef struct {
  const char *manufacturer;
  const char *manufacturer_part_number;
} PartIdentity;

typedef struct {
  const PartIdentity *exact;
  const char *role;
  Range value;
  const char *required_package;
  double min_voltage_v;
  double min_current_a;
  double min_power_w;
} PartRequest;
