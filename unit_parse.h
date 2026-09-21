#ifndef UNIT_PARSE_H
#define UNIT_PARSE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse engineering values to SI units.
 * Examples: "1k"→1000, "4.7kΩ"→4700, "100nF"→1e-7, "2.1V"→2.1,
 * "10u"→1e-5 (Farad if context capacitor), bare number → as-is.
 * Returns 0 on success, 1 on failure.
 */
int unit_parse_si(const char *text, double *out);

/* Same, but treat trailing R/ohm as resistance and F as farad. */
int unit_parse_number_or_string(const void *cjson_item, double *out);

#ifdef __cplusplus
}
#endif

#endif
