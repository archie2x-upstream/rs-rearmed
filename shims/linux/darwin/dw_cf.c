// The Darwin layer: the part of CoreFoundation the game uses (dw.h).
//
// The game holds CoreFoundation objects only as opaque references, except its own constant strings,
// which the compiler laid out in the image: { isa, flags, characters, length }. Strings made here have
// that layout too, so one reader serves both. Every object starts with a pointer to its type's tag;
// the image's constant strings get the string tag through ___CFConstantStringClassReference.
#define _GNU_SOURCE
#include <locale.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dw.h"
#include "dw_cf.h"

char dw_cf_string_tag, dw_cf_array_tag, dw_cf_data_tag, dw_cf_dict_tag, dw_cf_uuid_tag, dw_cf_locale_tag,
    dw_cf_boolean_tag;

// --- strings ---------------------------------------------------------------------------------------
#define CFS_UTF16 0x10u  // flags bit: the characters are UTF-16 code units, else 8-bit

struct dw_cfstring {
  void *isa;
  uint64_t flags;
  const void *chars;
  uint64_t length;  // in characters
  _Atomic int refs;  // 0 in an image constant: never freed
};

static int is_string(const void *o) { return o && *(void *const *)o == &dw_cf_string_tag; }

void *dw_cf_string(const char *utf8) {
  if (!utf8) return 0;
  size_t n = strlen(utf8);
  struct dw_cfstring *s = malloc(sizeof *s + n + 1);
  if (!s) return 0;
  memcpy(s + 1, utf8, n + 1);
  s->isa = &dw_cf_string_tag;
  s->flags = 0x7c8;
  s->chars = s + 1;
  s->length = n;
  atomic_init(&s->refs, 1);
  return s;
}

// The string as UTF-8 in buf (cut at cap - 1 bytes, always terminated); the byte count it needs.
size_t dw_cf_utf8(const void *str, char *buf, size_t cap) {
  size_t need = 0, w = 0;
  if (cap) buf[0] = 0;
  if (!is_string(str)) return 0;
  const struct dw_cfstring *s = str;
  if (!(s->flags & CFS_UTF16)) {
    need = s->length;
    if (cap) {
      w = need < cap - 1 ? need : cap - 1;
      memcpy(buf, s->chars, w);
      buf[w] = 0;
    }
    return need;
  }
  const uint16_t *u = s->chars;
  for (uint64_t i = 0; i < s->length; i++) {
    uint32_t c = u[i];
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < s->length && u[i + 1] >= 0xdc00 && u[i + 1] < 0xe000)
      c = 0x10000 + ((c - 0xd800) << 10) + (u[++i] - 0xdc00);
    char e[4];
    int k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (k == 1) e[0] = (char)c;
    else if (k == 2) { e[0] = (char)(0xc0 | c >> 6); e[1] = (char)(0x80 | (c & 63)); }
    else if (k == 3) { e[0] = (char)(0xe0 | c >> 12); e[1] = (char)(0x80 | (c >> 6 & 63)); e[2] = (char)(0x80 | (c & 63)); }
    else { e[0] = (char)(0xf0 | c >> 18); e[1] = (char)(0x80 | (c >> 12 & 63)); e[2] = (char)(0x80 | (c >> 6 & 63)); e[3] = (char)(0x80 | (c & 63)); }
    if (cap && w + (size_t)k <= cap - 1) { memcpy(buf + w, e, (size_t)k); w += (size_t)k; buf[w] = 0; }
    need += (size_t)k;
  }
  return need;
}

#define ENC_MACROMAN 0u
#define ENC_ASCII 0x600u
#define ENC_UTF8 0x08000100u
#define ENC_UTF16 0x100u

int64_t dw_CFStringGetLength(const void *s) { return is_string(s) ? (int64_t)((const struct dw_cfstring *)s)->length : 0; }
uint32_t dw_CFStringGetSystemEncoding(void) { return ENC_UTF8; }
uint32_t dw_CFStringGetFastestEncoding(const void *s) {
  return is_string(s) && (((const struct dw_cfstring *)s)->flags & CFS_UTF16) ? ENC_UTF16 : ENC_UTF8;
}
int64_t dw_CFStringGetMaximumSizeForEncoding(int64_t length, uint32_t enc) {
  return enc == ENC_UTF16 ? length * 2 : enc == ENC_UTF8 ? length * 3 : length;
}
const char *dw_CFStringGetCStringPtr(const void *s, uint32_t enc) {
  (void)enc;
  // An 8-bit string made here or laid out by the compiler is terminated; a UTF-16 one has no C form.
  return is_string(s) && !(((const struct dw_cfstring *)s)->flags & CFS_UTF16) ? ((const struct dw_cfstring *)s)->chars : 0;
}
uint8_t dw_CFStringGetCString(const void *s, char *buf, int64_t size, uint32_t enc) {
  (void)enc;
  if (!is_string(s) || !buf || size <= 0) return 0;
  return dw_cf_utf8(s, buf, (size_t)size) < (size_t)size;
}
void *dw_CFStringCreateWithBytes(void *alloc, const uint8_t *bytes, int64_t n, uint32_t enc, uint8_t external) {
  (void)alloc; (void)external;
  if (!bytes || n < 0) return 0;
  if (enc == ENC_UTF16) {
    struct dw_cfstring *s = malloc(sizeof *s + (size_t)n + 2);
    if (!s) return 0;
    memcpy(s + 1, bytes, (size_t)n);
    memset((char *)(s + 1) + n, 0, 2);
    s->isa = &dw_cf_string_tag;
    s->flags = 0x7d0;
    s->chars = s + 1;
    s->length = (uint64_t)n / 2;
    atomic_init(&s->refs, 1);
    return s;
  }
  char *t = malloc((size_t)n + 1);
  if (!t) return 0;
  memcpy(t, bytes, (size_t)n);
  t[n] = 0;
  void *r = dw_cf_string(t);
  free(t);
  return r;
}
// CFStringGetBytes(string, {location, length}, encoding, lossByte, isExternal, buffer, maxLen, &usedLen):
// the number of characters converted.
int64_t dw_CFStringGetBytes(const void *str, int64_t loc, int64_t len, uint32_t enc, uint8_t loss, uint8_t ext,
                            uint8_t *buf, int64_t max, int64_t *used) {
  (void)loss; (void)ext;
  if (used) *used = 0;
  if (!is_string(str) || loc < 0 || len < 0) return 0;
  const struct dw_cfstring *s = str;
  if ((uint64_t)(loc + len) > s->length) len = (int64_t)s->length - loc;
  if (len <= 0) return 0;
  int wide = (s->flags & CFS_UTF16) != 0;
  int64_t done = 0, w = 0;
  for (; done < len; done++) {
    uint32_t c = wide ? ((const uint16_t *)s->chars)[loc + done] : ((const uint8_t *)s->chars)[loc + done];
    uint8_t e[3];
    int k;
    if (enc == ENC_UTF16) { e[0] = (uint8_t)c; e[1] = (uint8_t)(c >> 8); k = 2; }
    else if (!wide || c < 0x80) { e[0] = (uint8_t)c; k = 1; }  // 8-bit data passes through as it is
    else if (enc != ENC_UTF8) { e[0] = '?'; k = 1; }
    else if (c < 0x800) { e[0] = (uint8_t)(0xc0 | c >> 6); e[1] = (uint8_t)(0x80 | (c & 63)); k = 2; }
    else { e[0] = (uint8_t)(0xe0 | c >> 12); e[1] = (uint8_t)(0x80 | (c >> 6 & 63)); e[2] = (uint8_t)(0x80 | (c & 63)); k = 3; }
    if (buf) {
      if (w + k > max) break;
      memcpy(buf + w, e, (size_t)k);
    }
    w += k;
  }
  if (used) *used = w;
  return done;
}
// kCFCompareLessThan -1, EqualTo 0, GreaterThan 1; option 1 ignores letter case.
int64_t dw_CFStringCompare(const void *a, const void *b, uint64_t options) {
  char x[1024], y[1024];
  dw_cf_utf8(a, x, sizeof x);
  dw_cf_utf8(b, y, sizeof y);
  int r = options & 1 ? strcasecmp(x, y) : strcmp(x, y);
  return r < 0 ? -1 : r > 0;
}

// --- arrays, data, dictionaries -----------------------------------------------------------------------
struct dw_cfarray { void *isa; _Atomic int refs; int64_t n; void *v[]; };
struct dw_cfdata { void *isa; _Atomic int refs; int64_t n; uint8_t b[]; };
struct dw_cfdict { void *isa; _Atomic int refs; int n; const void *k[32], *v[32]; };

void *dw_cf_array(void *const *items, int64_t n) {
  struct dw_cfarray *a = malloc(sizeof *a + (size_t)n * sizeof(void *));
  if (!a) return 0;
  a->isa = &dw_cf_array_tag;
  atomic_init(&a->refs, 1);
  a->n = n;
  if (n) memcpy(a->v, items, (size_t)n * sizeof(void *));
  return a;
}
void *dw_cf_data(const void *bytes, int64_t n) {
  struct dw_cfdata *d = malloc(sizeof *d + (size_t)n);
  if (!d) return 0;
  d->isa = &dw_cf_data_tag;
  atomic_init(&d->refs, 1);
  d->n = n;
  if (n) memcpy(d->b, bytes, (size_t)n);
  return d;
}
int64_t dw_CFArrayGetCount(const void *a) { return a && *(void *const *)a == &dw_cf_array_tag ? ((const struct dw_cfarray *)a)->n : 0; }
const void *dw_CFArrayGetValueAtIndex(const void *a, int64_t i) {
  return i >= 0 && i < dw_CFArrayGetCount(a) ? ((const struct dw_cfarray *)a)->v[i] : 0;
}
void dw_CFDataGetBytes(const void *d, int64_t loc, int64_t len, uint8_t *out) {
  if (!d || *(void *const *)d != &dw_cf_data_tag) return;
  const struct dw_cfdata *x = d;
  if (loc < 0 || len < 0 || loc + len > x->n) return;
  memcpy(out, x->b + loc, (size_t)len);
}
void *dw_CFDictionaryCreateMutable(void *alloc, int64_t cap, const void *kcb, const void *vcb) {
  (void)alloc; (void)cap; (void)kcb; (void)vcb;
  struct dw_cfdict *d = calloc(1, sizeof *d);
  if (!d) return 0;
  d->isa = &dw_cf_dict_tag;
  atomic_init(&d->refs, 1);
  return d;
}
void dw_CFDictionarySetValue(void *dict, const void *key, const void *value) {
  struct dw_cfdict *d = dict;
  if (!d || d->isa != &dw_cf_dict_tag) return;
  for (int i = 0; i < d->n; i++)
    if (d->k[i] == key) { d->v[i] = value; return; }
  if (d->n < 32) { d->k[d->n] = key; d->v[d->n++] = value; }
}

// --- UUIDs -------------------------------------------------------------------------------------------
struct dw_cfuuid { void *isa; _Atomic int refs; uint8_t b[16]; };
void *dw_CFUUIDCreateWithBytes(void *alloc, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5,
                               uint8_t b6, uint8_t b7, uint8_t b8, uint8_t b9, uint8_t b10, uint8_t b11, uint8_t b12,
                               uint8_t b13, uint8_t b14, uint8_t b15) {
  (void)alloc;
  struct dw_cfuuid *u = malloc(sizeof *u);
  if (!u) return 0;
  u->isa = &dw_cf_uuid_tag;
  atomic_init(&u->refs, 1);
  const uint8_t b[16] = {b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15};
  memcpy(u->b, b, 16);
  return u;
}
void *dw_CFUUIDCreateString(void *alloc, const void *uuid) {
  (void)alloc;
  const struct dw_cfuuid *u = uuid;
  if (!u || u->isa != &dw_cf_uuid_tag) return 0;
  char t[40];
  const uint8_t *b = u->b;
  snprintf(t, sizeof t, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", b[0], b[1], b[2], b[3],
           b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return dw_cf_string(t);
}

// --- the locale: the host's LANG, as "en_US" gives language "en" and country "US" ----------------------
struct dw_cflocale { void *isa; _Atomic int refs; };
static struct dw_cfstring key_language = {&dw_cf_string_tag, 0x7c8, "kCFLocaleLanguageCodeKey", 24, 0};
static struct dw_cfstring key_country = {&dw_cf_string_tag, 0x7c8, "kCFLocaleCountryCodeKey", 23, 0};
static struct dw_cfstring key_identifier = {&dw_cf_string_tag, 0x7c8, "kCFLocaleIdentifierKey", 22, 0};
void *dw_kCFLocaleLanguageCode = &key_language, *dw_kCFLocaleCountryCode = &key_country,
     *dw_kCFLocaleIdentifier = &key_identifier;

void *dw_CFLocaleCopyCurrent(void) {
  struct dw_cflocale *l = malloc(sizeof *l);
  if (!l) return 0;
  l->isa = &dw_cf_locale_tag;
  atomic_init(&l->refs, 1);
  return l;
}
// The value is owned by the locale on Darwin (a Get); these few strings are kept for the process's life.
const void *dw_CFLocaleGetValue(const void *locale, const void *key) {
  (void)locale;
  static void *lang, *country, *ident;
  if (!ident) {
    const char *e = getenv("LC_ALL");
    if (!e || !*e) e = getenv("LC_MESSAGES");
    if (!e || !*e) e = getenv("LANG");
    char id[32] = "en_US", l[8] = "en", c[8] = "US";
    if (e && e[0] >= 'a' && e[0] <= 'z' && e[1] >= 'a' && e[1] <= 'z') {
      snprintf(l, sizeof l, "%.2s", e);
      if (e[2] == '_' && e[3] && e[4]) snprintf(c, sizeof c, "%.2s", e + 3);
      snprintf(id, sizeof id, "%s_%s", l, c);
    }
    lang = dw_cf_string(l);
    country = dw_cf_string(c);
    ident = dw_cf_string(id);
  }
  return key == &key_language ? lang : key == &key_country ? country : key == &key_identifier ? ident : 0;
}

// --- the rest ------------------------------------------------------------------------------------------
void *dw_kCFAllocatorDefault = 0;
static struct { void *isa; } boolean_true = {&dw_cf_boolean_tag};
void *dw_kCFBooleanTrue = &boolean_true;
double dw_kCFCoreFoundationVersionNumber = 1153.18;  // OS X 10.10, the newest the game's SDK names
// The callback tables are only handed back to CFDictionaryCreateMutable, which ignores them.
uint64_t dw_kCFTypeDictionaryKeyCallBacks[6], dw_kCFTypeDictionaryValueCallBacks[5];

// Seconds since 1 January 2001 00:00 GMT.
double dw_CFAbsoluteTimeGetCurrent(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (double)t.tv_sec - 978307200.0 + (double)t.tv_nsec * 1e-9;
}

void dw_CFRelease(void *o) {
  if (!o) return;
  void *isa = *(void **)o;
  _Atomic int *refs;
  if (isa == &dw_cf_string_tag) {
    refs = &((struct dw_cfstring *)o)->refs;
    if (atomic_load(refs) == 0) return;  // a constant
  } else if (isa == &dw_cf_array_tag) refs = &((struct dw_cfarray *)o)->refs;
  else if (isa == &dw_cf_data_tag) refs = &((struct dw_cfdata *)o)->refs;
  else if (isa == &dw_cf_dict_tag) refs = &((struct dw_cfdict *)o)->refs;
  else if (isa == &dw_cf_uuid_tag) refs = &((struct dw_cfuuid *)o)->refs;
  else if (isa == &dw_cf_locale_tag) refs = &((struct dw_cflocale *)o)->refs;
  else return;  // not an object of this layer: left alone
  if (atomic_fetch_sub(refs, 1) == 1) free(o);
}

void dw_CFShow(const void *o) {
  char t[512];
  if (is_string(o)) {
    dw_cf_utf8(o, t, sizeof t);
    fprintf(stderr, "%s\n", t);
  } else fprintf(stderr, "<CF object %p>\n", o);
}
