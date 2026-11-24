// main() of the game on a Linux host: so far the two checks, no shell yet.
//
//   rs-rearmed --bind-only IMAGE   map, rebase and bind the image; print the bind statistics
//   rs-rearmed --init-only IMAGE   and run its __mod_init_func initializers, translated
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

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char **argv, char **envp) {
  int init_only = 0, bind_only = 0;
  const char *img = getenv("RS_RC_IMAGE");
  static char *gargv[64];
  int gargc = 0;
  gargv[gargc++] = argv[0];
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--init-only")) init_only = 1;
    else if (!strcmp(argv[i], "--bind-only")) bind_only = 1;
    else if (argv[i][0] != '-' && (!img || i == argc - 1)) img = argv[i];
    else if (gargc < 63) gargv[gargc++] = argv[i];
  }
  gargv[gargc] = 0;
  if (!img || !*img || !(init_only || bind_only)) {
    fprintf(stderr, "usage: %s --bind-only|--init-only IMAGE\n(there is no shell on this host yet)\n", argv[0]);
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
  printf("init-only: image loaded in %.3f s, %d initializers run in %.3f s; unresolved %llu slots / %llu symbols\n",
         t1 - t0, n, t2 - t1, (unsigned long long)rc_stats.unresolved, (unsigned long long)rc_stats.unresolved_syms);
  return 0;
}
