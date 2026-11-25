// main() of the game on a Linux host: so far the checks, no shell yet.
//
//   rs-rearmed --bind-only IMAGE         map, rebase and bind the image; print the bind statistics
//   rs-rearmed --init-only IMAGE         and run its __mod_init_func initializers, translated
//   rs-rearmed --boot GAMEDIR IMAGE      and start the engine in GAMEDIR (the folder with the .psarc
//                                        files and Rocksmith.ini) without a window: GameBootstrap,
//                                        then RS_BOOT_FRAMES engine frames (default 0), drawn to an
//                                        offscreen OpenGL context of RS_DISPLAY (WIDTHxHEIGHT, default
//                                        1280x720); RS_BOOT_SHOT=FILE.ppm saves the last frame
//                                        RS_BOOT_SHOT_EVERY=N saves every Nth frame beside it, numbered;
//                                        RS_BOOT_KEYS="FRAME:KEY ..." presses keys at those frames (KEY:
//                                        enter esc up down left right space tab back del, or one character)
//   rs-rearmed --window GAMEDIR IMAGE    the same start, in a full-screen window with the keyboard and
//                                        the mouse, until the game ends or Ctrl+Q
//
// IMAGE is the decrypted x86-64 Mach-O (RS_RC_IMAGE when it is not given). The imports are bound by the
// Darwin layer (shims/linux/darwin); RC_LOG_UNRESOLVED lists the ones it has nothing for, and
// RC_TRACE_INITS=1 prints each initializer before it runs. The macOS app's main is rc_main.c.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rc_rt.h"
#include "host_gl_linux.h"
#include "rsp_platform.h"
#include "seam.h"

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

// --boot's scripted keys (RS_BOOT_KEYS): the Mac virtual key code and character the shell would send.
static int script_key(const char *name, int *code, int *ch) {
  static const struct { const char *n; int code, ch; } named[] = {
      {"enter", 36, 13}, {"esc", 53, 27}, {"space", 49, 32}, {"tab", 48, 9}, {"back", 51, 127}, {"del", 117, 0xF728},
      {"up", 126, 0xF700}, {"down", 125, 0xF701}, {"left", 123, 0xF702}, {"right", 124, 0xF703}};
  for (size_t i = 0; i < sizeof named / sizeof *named; i++)
    if (!strcmp(named[i].n, name)) { *code = named[i].code; *ch = named[i].ch; return 1; }
  static const char ansi[] = "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.";
  if (name[0] && !name[1])
    for (int i = 0; i < (int)sizeof ansi - 1; i++)
      if (ansi[i] && ansi[i] == name[0]) { *code = i; *ch = name[0]; return 1; }
  return 0;
}
// The game's current menu, looked at every frame: its name, when it came up, and how many times the menu
// has changed. A "menu: NAME" line is logged at each change.
static char g_menu[64] = "";
static double g_menu_ms;
static long g_menu_changes;
static void watch_menu(long frame, double ms) {
  const char *m = rcapp_seam_menu_name();
  if (!m) m = "(none)";
  if (strcmp(m, g_menu)) {
    snprintf(g_menu, sizeof g_menu, "%s", m);
    g_menu_ms = ms;
    g_menu_changes++;
    fprintf(stderr, "menu: %s  (frame %ld)\n", g_menu, frame);
  }
}

// --boot's recorded keys (RS_BOOT_KEYS_FILE, written by --window with RS_KEYS_RECORD). A line is
//   TIME down|up CODE CHAR [MENU TIME-ON-MENU]
// With a menu, the key is sent when the game is on that menu and has been for that long, and, when the
// recording changed menus since the key before, only after the game has too: the keys follow the game's
// screens, not the clock. Without one, the key is sent at its time.
static void recorded_keys(double ms) {
  static FILE *f;
  static int opened, have, fields;
  static unsigned at, on_menu;
  static char what[8], menu[64];
  static int code, ch;
  static double last_start = -1e18;
  static long changes_at_last = -1;
  static double waiting_since = -1;
  if (!opened) {
    opened = 1;
    const char *p = getenv("RS_BOOT_KEYS_FILE");
    f = p && *p ? fopen(p, "r") : 0;
  }
  while (f) {
    if (!have) {
      char line[256];
      if (!fgets(line, sizeof line, f)) {
        fclose(f);
        f = 0;
        break;
      }
      fields = sscanf(line, "%u %7s %d %d %63s %u", &at, what, &code, &ch, menu, &on_menu);
      if (fields < 4) continue;
      have = 1;
      waiting_since = ms;
    }
    static double down_sent_ms;  // when the last key went down here, and in the recording
    static unsigned down_at;
    if (fields >= 6 && what[0] == 'u') {
      // a key comes up as long after it went down as it did then, whatever the menu has become
      if (ms - down_sent_ms < (double)(at - down_at)) break;
    } else if (fields >= 6) {
      double start = (double)at - (double)on_menu;  // when that menu came up in the recording
      int same_visit = start - last_start < 60 && start - last_start > -60;
      if (strcmp(menu, g_menu) || ms - g_menu_ms < (double)on_menu || (!same_visit && g_menu_changes <= changes_at_last)) {
        if (ms - waiting_since > 180000) {
          fprintf(stderr, "[rc] recorded keys: the game is on %s, not %s, for 3 minutes: stopping the keys\n", g_menu, menu);
          fclose(f);
          f = 0;
        }
        break;
      }
      last_start = start;
      changes_at_last = g_menu_changes;
    } else if ((double)at > ms) break;
    if (what[0] == 'd') {
      down_sent_ms = ms;
      down_at = at;
    }
    rsp_post_input(what[0] == 'd' ? RSP_EV_KEYDOWN : RSP_EV_KEYUP, code, ch);
    have = 0;
  }
}

static void script_keys(long frame) {
  static const char *keys;
  static int up_code = -1, up_ch;
  if (up_code >= 0) {  // the key pressed on the frame before
    rsp_post_input(RSP_EV_KEYUP, up_code, up_ch);
    up_code = -1;
  }
  if (!keys) keys = getenv("RS_BOOT_KEYS") ? getenv("RS_BOOT_KEYS") : "";
  for (const char *p = keys; *p;) {
    char name[16];
    long at;
    int n = 0;
    if (sscanf(p, " %ld:%15s%n", &at, name, &n) != 2) break;
    int code, ch;
    if (at == frame && script_key(name, &code, &ch)) {
      rsp_post_input(RSP_EV_KEYDOWN, code, ch);
      up_code = code;
      up_ch = ch;
    }
    p += n;
  }
}

int main(int argc, char **argv, char **envp) {
  int init_only = 0, bind_only = 0;
  const char *boot_dir = 0;
  int window = 0;
  const char *img = getenv("RS_RC_IMAGE");
  static char *gargv[64];
  int gargc = 0;
  gargv[gargc++] = argv[0];
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--init-only")) init_only = 1;
    else if (!strcmp(argv[i], "--bind-only")) bind_only = 1;
    else if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot_dir = argv[++i];
    else if (!strcmp(argv[i], "--window") && i + 1 < argc) boot_dir = argv[++i], window = 1;
    else if (argv[i][0] != '-' && (!img || i == argc - 1)) img = argv[i];
    else if (gargc < 63) gargv[gargc++] = argv[i];
  }
  gargv[gargc] = 0;
  if (!img || !*img || !(init_only || bind_only || boot_dir)) {
    fprintf(stderr, "usage: %s --bind-only|--init-only|--boot GAMEDIR|--window GAMEDIR IMAGE\n(there is no shell on this host yet)\n",
            argv[0]);
    return 2;
  }
  setenv("RC_LOG_UNRESOLVED", "1", 0);
  setenv("RS_STEAM_MODE", "stub", 0);  // offline: the game must never call home

  double t0 = now_s();
  if (rc_image_load(img)) {
    fprintf(stderr, "[rc] rc_image_load(%s) failed\n", img);
    return 2;
  }
  double t1 = now_s();
  if (bind_only) {
    printf("bind-only: image %s loaded in %.3f s; unresolved %llu slots / %llu symbols\n", img, t1 - t0,
           (unsigned long long)rc_stats.unresolved, (unsigned long long)rc_stats.unresolved_syms);
    return 0;
  }

  // The initializers, translated, on the main thread's guest context. The x86 ones take
  // (argc, argv, envp, apple).
  const uint64_t *inits;
  int n = rc_image_inits(&inits);
  const char *tr = getenv("RC_TRACE_INITS");
  int trace = tr && *tr && *tr != '0';
  static char *apple[2];
  apple[0] = argv[0];
  uint64_t args[4] = {(uint64_t)gargc, (uint64_t)(uintptr_t)gargv, (uint64_t)(uintptr_t)envp,
                      (uint64_t)(uintptr_t)apple};
  for (int i = 0; i < n; i++) {
    if (trace) fprintf(stderr, "[rc] init %d/%d at 0x%llx\n", i, n, (unsigned long long)(inits[i] - rc_slide));
    rc_call_guest(inits[i], args, 4, NULL, 0, NULL);
  }
  double t2 = now_s();
  if (boot_dir) {
    setenv("RS_GAME_DIR", boot_dir, 1);  // the offline Steam stand-in reads the game folder from it
    // The engine behind the platform hooks, as the macOS shell starts it, with no window and no
    // OpenGL context (the game takes a null one when the renderer owns the context).
    int w = 1280, hgt = 720;
    const char *de = getenv("RS_DISPLAY");
    if (!de || sscanf(de, "%dx%d", &w, &hgt) != 2 || w <= 0 || hgt <= 0) w = 1280, hgt = 720;
    char dbuf[32];
    snprintf(dbuf, sizeof dbuf, "%dx%d", w, hgt);
    setenv("RS_DISPLAY", dbuf, 1);  // the Darwin layer's one display is this size
    int have_gl;
    if (window) {
      if (host_window_open(&w, &hgt)) return 1;
      // RS_RENDER=WIDTHxHEIGHT: the engine draws at that size, in the window's lower left corner, instead
      // of the window's own (a check for screens of another shape than the game was laid out for).
      int rw, rh;
      const char *re = getenv("RS_RENDER");
      if (re && sscanf(re, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0 && rw <= w && rh <= hgt) w = rw, hgt = rh;
      snprintf(dbuf, sizeof dbuf, "%dx%d", w, hgt);
      setenv("RS_DISPLAY", dbuf, 1);
      have_gl = 1;
    } else {
      have_gl = host_gl_offscreen(w, hgt) == 0;
      if (!have_gl) fprintf(stderr, "[rc] no OpenGL context: the engine starts, but a frame cannot draw\n");
    }
    const rsp_engine_hooks *h = rcapp_seam_hooks();
    rsp_set_hooks(h);
    h->gl_context(h->ctx, NULL);
    rsp_boot boot = {boot_dir, boot_dir, 0, NULL};
    int r = rsp_game_bootstrap(&boot);
    double t3 = now_s();
    printf("boot: %d initializers in %.3f s, GameBootstrap returned %d after %.3f s\n", n, t2 - t1, r, t3 - t2);
    if (r) return 1;
    if (window) {
      rsp_set_display_mode(0, w, hgt);  // the engine draws at the window's size
      host_window_run();
      rsp_game_shutdown();
      return 0;
    }
    const char *fe = getenv("RS_BOOT_FRAMES");
    long frames = fe && *fe ? atol(fe) : 0, f = 0;
    const char *shot = getenv("RS_BOOT_SHOT"), *ee = getenv("RS_BOOT_SHOT_EVERY");
    long every = ee && *ee ? atol(ee) : 0;
    rsp_set_focus(1);  // input is only taken while the app has the focus
    // RS_BOOT_FPS=N holds the loop to N frames a second (a window's pace); 0, the default, runs free.
    const char *pe = getenv("RS_BOOT_FPS");
    double fps = pe && *pe ? atof(pe) : 0, loop0 = now_s();
    for (; f < frames; f++) {
      watch_menu(f, (now_s() - loop0) * 1000.0);
      script_keys(f);
      recorded_keys((now_s() - loop0) * 1000.0);
      if (fps > 0) {
        double due = loop0 + (double)(f + 1) / fps, left = due - now_s();
        if (left > 0) {
          struct timespec d = {(time_t)left, (long)((left - (double)(time_t)left) * 1e9)};
          nanosleep(&d, 0);
        }
      }
      if (rsp_game_frame()) break;
      if (have_gl && shot && *shot && f == frames - 1) host_gl_save_ppm(shot);
      if (have_gl && shot && *shot && every > 0 && f % every == every - 1) {
        char numbered[1100];
        snprintf(numbered, sizeof numbered, "%s.%05ld.ppm", shot, f + 1);
        host_gl_save_ppm(numbered);
      }
      if (have_gl) host_gl_swap();
    }
    if (frames) printf("boot: %ld of %ld engine frames run in %.3f s\n", f, frames, now_s() - t3);
    return 0;
  }
  printf("init-only: image loaded in %.3f s, %d initializers run in %.3f s; unresolved %llu slots / %llu symbols\n",
         t1 - t0, n, t2 - t1, (unsigned long long)rc_stats.unresolved, (unsigned long long)rc_stats.unresolved_syms);
  return 0;
}
