// The Darwin layer's CoreFoundation subset, for its other parts (dw_cf.c).
#ifndef DW_CF_H
#define DW_CF_H

#include <stddef.h>
#include <stdint.h>

void *dw_cf_string(const char *utf8);                        // a new string; the caller's to CFRelease
size_t dw_cf_utf8(const void *str, char *buf, size_t cap);  // any CFString as UTF-8
void *dw_cf_array(void *const *items, int64_t n);
void *dw_cf_data(const void *bytes, int64_t n);

#endif
