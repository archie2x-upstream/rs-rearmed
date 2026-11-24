// main() of the game on a Linux host: so far the checks, no shell yet.
//
//   rs-rearmed --bind-only IMAGE         map, rebase and bind the image; print the bind statistics
//   rs-rearmed --init-only IMAGE         and run its __mod_init_func initializers, translated
//   rs-rearmed --boot GAMEDIR IMAGE      and start the engine in GAMEDIR (the folder with the .psarc
//                                        files and Rocksmith.ini) without a window: GameBootstrap,
//                                        then RS_BOOT_FRAMES engine frames (default 0)
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
#include "rsp_platform.h"
#include "seam.h"

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv, char **envp) {
  int init_only = 0, bind_only = 0;
  const char *boot_dir = 0;
  const char *img = getenv("RS_RC_IMAGE");
  static char *gargv[64];
  int gargc = 0;
  gargv[gargc++] = argv[0];
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--init-only")) init_only = 1;
    else if (!strcmp(argv[i], "--bind-only")) bind_only = 1;
    else if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot_dir = argv[++i];
    else if (argv[i][0] != '-' && (!img || i == argc - 1)) img = argv[i];
    else if (gargc < 63) gargv[gargc++] = argv[i];
  }
  gargv[gargc] = 0;
  if (!img || !*img || !(init_only || bind_only || boot_dir)) {
    fprintf(stderr, "usage: %s --bind-only|--init-only|--boot GAMEDIR IMAGE\n(there is no shell on this host yet)\n",
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
    const rsp_engine_hooks *h = rcapp_seam_hooks();
    rsp_set_hooks(h);
    h->gl_context(h->ctx, NULL);
    rsp_boot boot = {boot_dir, boot_dir, 0, NULL};
    int r = rsp_game_bootstrap(&boot);
    double t3 = now_s();
    printf("boot: %d initializers in %.3f s, GameBootstrap returned %d after %.3f s\n", n, t2 - t1, r, t3 - t2);
    if (r) return 1;
    const char *fe = getenv("RS_BOOT_FRAMES");
    long frames = fe && *fe ? atol(fe) : 0, f = 0;
    for (; f < frames; f++)
      if (rsp_game_frame()) break;
    if (frames) printf("boot: %ld of %ld engine frames run in %.3f s\n", f, frames, now_s() - t3);
    return 0;
  }
  printf("init-only: image loaded in %.3f s, %d initializers run in %.3f s; unresolved %llu slots / %llu symbols\n",
         t1 - t0, n, t2 - t1, (unsigned long long)rc_stats.unresolved, (unsigned long long)rc_stats.unresolved_syms);
  return 0;
}
