// A first window for the Linux host (rc_main_linux.c --window): the game on the screen, with the
// keyboard, the mouse and game controllers. It is the check's window, not the shell yet: no window
// modes, no cursor policy.
//
// A controller plays the keyboard's part, since the game's menus are keyboard menus (buttons by
// position, as SDL names them on an Xbox pad):
//   D-pad or left stick   the arrow keys (held: repeats)
//   A (bottom)            Return: select            B (right)         Esc: back, pause
//   X (left)              Space: the key the screen names (Help, Tools, ...)
//   Y (top)               forward Delete: Skip Tuner, delete
//   Start                 Return                    Back/Select       Tab: sort
//   L1 / R1               Page Up / Page Down
//   Capture/Share (misc1) twice within 3 s: quit the game
// RS_PAD_SWAP=1 swaps A and B, for a pad whose accept button is on the right.
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
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER)) {
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
// The character a key gives with Shift held, on the US layout the key codes below are for.
static int shifted(int c) {
  static const char plain[] = "`1234567890-=[]\\;',./", with[] = "~!@#$%^&*()_+{}|:\"<>?";
  if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
  for (int i = 0; plain[i]; i++)
    if (plain[i] == c) return with[i];
  return c;
}

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

// --- controllers ---------------------------------------------------------------------------------------
// A button or direction as the key it stands for: (Mac key code, character), 0 when it has none.
static int pad_key(int button, int *code, int *ch) {
  static int swap = -1;
  if (swap < 0) {
    const char *e = getenv("RS_PAD_SWAP");
    swap = e && *e == '1';
  }
  if (swap && button == SDL_CONTROLLER_BUTTON_A) button = SDL_CONTROLLER_BUTTON_B;
  else if (swap && button == SDL_CONTROLLER_BUTTON_B) button = SDL_CONTROLLER_BUTTON_A;
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: *code = 36, *ch = 13; return 1;
    case SDL_CONTROLLER_BUTTON_B: *code = 53, *ch = 27; return 1;
    case SDL_CONTROLLER_BUTTON_X: *code = 49, *ch = 32; return 1;
    case SDL_CONTROLLER_BUTTON_Y: *code = 117, *ch = 0xF728; return 1;
    case SDL_CONTROLLER_BUTTON_BACK: *code = 48, *ch = 9; return 1;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: *code = 116, *ch = 0xF72C; return 1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: *code = 121, *ch = 0xF72D; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: *code = 126, *ch = 0xF700; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *code = 125, *ch = 0xF701; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *code = 123, *ch = 0xF702; return 1;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *code = 124, *ch = 0xF703; return 1;
  }
  return 0;
}
// The four directions as held now (D-pad or left stick of any controller), with the keyboard's repeat:
// a first press, then after 400 ms one every 110 ms.
static struct { int down; Uint32 next; } g_dir[4];
static const int dir_button[4] = {SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                  SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
static void pad_directions(void) {
  int held[4] = {0, 0, 0, 0};
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    SDL_GameController *c = SDL_IsGameController(i) ? SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i)) : 0;
    if (!c) continue;
    int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    held[0] |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP) || y < -20000;
    held[1] |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || y > 20000;
    held[2] |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || x < -20000;
    held[3] |= SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || x > 20000;
  }
  Uint32 now = SDL_GetTicks();
  for (int d = 0; d < 4; d++) {
    int code, ch;
    pad_key(dir_button[d], &code, &ch);
    if (held[d] && (!g_dir[d].down || (Sint32)(now - g_dir[d].next) >= 0)) {
      if (g_dir[d].down) rsp_post_input(RSP_EV_KEYUP, code, ch);
      rsp_post_input(RSP_EV_KEYDOWN, code, ch);
      g_dir[d].next = now + (g_dir[d].down ? 110 : 400);
      g_dir[d].down = 1;
    } else if (!held[d] && g_dir[d].down) {
      rsp_post_input(RSP_EV_KEYUP, code, ch);
      g_dir[d].down = 0;
    }
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
          if (mac_key(e.key.keysym.sym, &code, &ch) && !e.key.repeat) {
            // Shift, or Caps Lock on a letter, gives the upper character, as the Mac shell's key event has it
            int caps = (e.key.keysym.mod & KMOD_CAPS) && ch >= 'a' && ch <= 'z';
            if (((e.key.keysym.mod & KMOD_SHIFT) != 0) != caps) ch = shifted(ch);
            key(RSP_EV_KEYDOWN, code, ch);
          }
          break;
        case SDL_KEYUP:
          if (mac_key(e.key.keysym.sym, &code, &ch)) key(RSP_EV_KEYUP, code, ch);
          break;
        case SDL_CONTROLLERDEVICEADDED:
          if (SDL_GameControllerOpen(e.cdevice.which))
            fprintf(stderr, "[window] controller: %s\n", SDL_GameControllerNameForIndex(e.cdevice.which));
          break;
        case SDL_CONTROLLERDEVICEREMOVED: {
          SDL_GameController *c = SDL_GameControllerFromInstanceID(e.cdevice.which);
          if (c) SDL_GameControllerClose(c);
          break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
          int b = e.cbutton.button, down = e.type == SDL_CONTROLLERBUTTONDOWN;
          if (b == SDL_CONTROLLER_BUTTON_MISC1) {  // the appliance's quit button: twice within 3 s
            static Uint32 last;
            if (down) {
              if (last && SDL_GetTicks() - last < 3000) rsp_request_quit();
              last = SDL_GetTicks() ? SDL_GetTicks() : 1;
            }
            break;
          }
          if (b >= SDL_CONTROLLER_BUTTON_DPAD_UP && b <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) break;  // pad_directions
          if (pad_key(b, &code, &ch)) key(down ? RSP_EV_KEYDOWN : RSP_EV_KEYUP, code, ch);
          break;
        }
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
    pad_directions();
    if (rsp_game_frame()) break;
    SDL_GL_SwapWindow(g_win);
    static unsigned frame;
    if (frame++ % 15 == 0) watch_menu();
  }
}
