// A first window for the Linux host (rc_main_linux.c --window): the game on the screen, with the
// keyboard and the mouse. It is the check's window, not the shell yet: no controller, no window modes,
// no cursor policy.
//
// SDL makes a full-screen window with a compatibility-profile OpenGL context; the engine draws at the
// window's size. Keys go to the engine as the Mac shell sends them: a Mac virtual key code and a
// character. Ctrl+Q, or closing the window, quits.
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "host_gl_linux.h"
#include "rsp_platform.h"
#include "seam.h"
#include <string.h>

static SDL_Window *g_win;
static int g_w, g_h;

int host_window_open(int *width, int *height) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "[window] SDL: %s\n", SDL_GetError());
    return -1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  g_win = SDL_CreateWindow("Rocksmith 2014", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
  if (!g_win || !SDL_GL_CreateContext(g_win)) {
    fprintf(stderr, "[window] no OpenGL window: %s\n", SDL_GetError());
    return -1;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_GL_GetDrawableSize(g_win, &g_w, &g_h);
  typedef const unsigned char *(*get_string)(unsigned);
  get_string gs = (get_string)SDL_GL_GetProcAddress("glGetString");
  fprintf(stderr, "[window] %dx%d, %s, OpenGL %s\n", g_w, g_h, gs ? (const char *)gs(0x1F01) : "?", gs ? (const char *)gs(0x1F02) : "?");
  *width = g_w;
  *height = g_h;
  return 0;
}

// An SDL key as the Mac shell's (virtual key code, character); 0 when the game has no use for it.
static int mac_key(SDL_Keycode k, int *code, int *ch) {
  static const struct { SDL_Keycode k; int code, ch; } map[] = {
      {SDLK_RETURN, 36, 13},   {SDLK_KP_ENTER, 76, 3}, {SDLK_ESCAPE, 53, 27},    {SDLK_SPACE, 49, 32},
      {SDLK_TAB, 48, 9},       {SDLK_BACKSPACE, 51, 127}, {SDLK_DELETE, 117, 0xF728},
      {SDLK_UP, 126, 0xF700},  {SDLK_DOWN, 125, 0xF701}, {SDLK_LEFT, 123, 0xF702}, {SDLK_RIGHT, 124, 0xF703},
      {SDLK_HOME, 115, 0xF729}, {SDLK_END, 119, 0xF72B}, {SDLK_PAGEUP, 116, 0xF72C}, {SDLK_PAGEDOWN, 121, 0xF72D},
  };
  for (size_t i = 0; i < sizeof map / sizeof *map; i++)
    if (map[i].k == k) { *code = map[i].code; *ch = map[i].ch; return 1; }
  // Letters and digits: the ANSI layout's key codes.
  static const char ansi[] = "asdfhgzxcv\0bqweryt123465=97-80]ou[ip\0lj'k;\\,/nm.";
  if (k >= 32 && k < 127)
    for (int i = 0; i < (int)sizeof ansi - 1; i++)
      if (ansi[i] == (char)k) { *code = i; *ch = (int)k; return 1; }
  return 0;
}

// RS_KEYS_RECORD=FILE: every key sent to the engine, with its time in milliseconds since the loop began,
// for --boot to play again (RS_BOOT_KEYS_FILE).
static FILE *g_keys;
static Uint32 g_t0, g_menu_t;  // the loop's start, and when the current menu came up
static char g_menu[64] = "(none)";
static void key(int type, int code, int ch) {
  if (g_keys) {
    // time, key, and the menu it was pressed on with the time since that menu came up
    fprintf(g_keys, "%u %s %d %d %s %u\n", SDL_GetTicks() - g_t0, type == RSP_EV_KEYDOWN ? "down" : "up", code, ch,
            g_menu, SDL_GetTicks() - g_menu_t);
    fflush(g_keys);
  }
  rsp_post_input(type, code, ch);
}
// The current menu, noted when it changes (a line in the log).
static void watch_menu(void) {
  const char *m = rcapp_seam_menu_name();
  if (!m) m = "(none)";
  if (strcmp(m, g_menu)) {
    snprintf(g_menu, sizeof g_menu, "%s", m);
    g_menu_t = SDL_GetTicks();
    fprintf(stderr, "menu: %s  (%.1f s)\n", g_menu, (g_menu_t - g_t0) / 1000.0);
  }
}

// The frame loop: events in, a frame, a swap. Returns when the engine or the player ends it.
void host_window_run(void) {
  const char *kr = getenv("RS_KEYS_RECORD");
  g_keys = kr && *kr ? fopen(kr, "w") : 0;
  g_t0 = g_menu_t = SDL_GetTicks();
  rsp_set_focus(1);
  for (;;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      int code, ch;
      switch (e.type) {
        case SDL_QUIT: rsp_request_quit(); break;
        case SDL_KEYDOWN:
          if (e.key.keysym.sym == SDLK_q && (e.key.keysym.mod & KMOD_CTRL)) { rsp_request_quit(); break; }
          if (mac_key(e.key.keysym.sym, &code, &ch) && !e.key.repeat) key(RSP_EV_KEYDOWN, code, ch);
          break;
        case SDL_KEYUP:
          if (mac_key(e.key.keysym.sym, &code, &ch)) key(RSP_EV_KEYUP, code, ch);
          break;
        case SDL_MOUSEMOTION:  // the engine's origin is the bottom left
          rsp_post_input(RSP_EV_MOUSEMOVE, e.motion.x, g_h - 1 - e.motion.y);
          break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
          if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
            rsp_post_input(e.type == SDL_MOUSEBUTTONDOWN ? RSP_EV_MOUSEDOWN : RSP_EV_MOUSEUP,
                           e.button.button == SDL_BUTTON_RIGHT, 0);
          break;
        case SDL_MOUSEWHEEL:
          if (e.wheel.y) rsp_post_input(RSP_EV_SCROLL, e.wheel.y > 0, 1);
          break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) rsp_set_focus(1);
          else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) rsp_set_focus(0);
          break;
      }
    }
    if (rsp_game_frame()) break;
    SDL_GL_SwapWindow(g_win);
    static unsigned frame;
    if (frame++ % 15 == 0) watch_menu();
  }
}
