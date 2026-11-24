// The Darwin layer: the display questions the game asks CoreGraphics, and Gestalt (dw.h).
//
// There is one display, the host shell's: its size is what the shell says (dw_display_set), or
// RS_DISPLAY=WIDTHxHEIGHT, or 1920x1080. Its one mode is that size in 32-bit colour.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dw.h"
#include "dw_cf.h"

#define MAIN_DISPLAY 1u

static int g_w, g_h;
void dw_display_set(int width, int height) {
  g_w = width;
  g_h = height;
}
static void size(int *w, int *h) {
  if (g_w <= 0 || g_h <= 0) {
    const char *e = getenv("RS_DISPLAY");
    if (!e || sscanf(e, "%dx%d", &g_w, &g_h) != 2 || g_w <= 0 || g_h <= 0) {
      g_w = 1920;
      g_h = 1080;
    }
  }
  *w = g_w;
  *h = g_h;
}

struct dw_cgrect { double x, y, w, h; };
struct mode { void *isa; int w, h; };
static char mode_tag;

uint32_t dw_CGMainDisplayID(void) { return MAIN_DISPLAY; }
struct dw_cgrect dw_CGDisplayBounds(uint32_t display) {
  (void)display;
  int w, h;
  size(&w, &h);
  return (struct dw_cgrect){0, 0, w, h};
}
double dw_CGRectGetWidth(struct dw_cgrect r) { return r.w < 0 ? -r.w : r.w; }
double dw_CGRectGetHeight(struct dw_cgrect r) { return r.h < 0 ? -r.h : r.h; }
int32_t dw_CGGetActiveDisplayList(uint32_t max, uint32_t *displays, uint32_t *count) {
  if (displays && max) displays[0] = MAIN_DISPLAY;
  if (count) *count = 1;
  return 0;
}
void *dw_CGDisplayCopyAllDisplayModes(uint32_t display, void *options) {
  (void)display; (void)options;
  static struct mode m;
  int w, h;
  size(&w, &h);
  m = (struct mode){&mode_tag, w, h};
  void *one = &m;
  return dw_cf_array(&one, 1);
}
static const struct mode *mode_of(const void *m) { return m && *(void *const *)m == &mode_tag ? m : 0; }
uint64_t dw_CGDisplayModeGetWidth(const void *m) { return mode_of(m) ? (uint64_t)mode_of(m)->w : 0; }
uint64_t dw_CGDisplayModeGetHeight(const void *m) { return mode_of(m) ? (uint64_t)mode_of(m)->h : 0; }
void *dw_CGDisplayModeCopyPixelEncoding(const void *m) {
  (void)m;
  return dw_cf_string("--------RRRRRRRRGGGGGGGGBBBBBBBB");  // IO32BitDirectPixels
}
uint32_t dw_CGDisplayIDToOpenGLDisplayMask(uint32_t display) { (void)display; return 1; }
// The pointer belongs to the host shell.
int32_t dw_CGDisplayHideCursor(uint32_t display) { (void)display; return 0; }
int32_t dw_CGDisplayShowCursor(uint32_t display) { (void)display; return 0; }
int32_t dw_CGWindowLevelForKey(uint32_t key) { (void)key; return 0; }

// Gestalt: the system version selectors, answered as OS X 10.10; anything else is undefined (-5551).
int16_t dw_Gestalt(uint32_t selector, int32_t *response) {
  int32_t v;
  switch (selector) {
    case 0x73797376: v = 0x10a0; break;  // 'sysv'
    case 0x73797331: v = 10; break;      // 'sys1'
    case 0x73797332: v = 10; break;      // 'sys2'
    case 0x73797333: v = 0; break;       // 'sys3'
    default: return -5551;
  }
  if (response) *response = v;
  return 0;
}
