// Sound on a Linux host (host_audio_linux.h).
//
// Output goes through SDL, which finds the desktop's sound server (PipeWire, PulseAudio) or ALSA: the
// game shares the output with everything else. RS_AUDIO_OUT names an SDL output device.
// The cable is read from ALSA directly, the card itself (hw), for the smallest delay: it is the one
// capture device with "Rocksmith" in its name. RS_AUDIO_IN names another ALSA capture device.
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_audio_linux.h"

// --- output ----------------------------------------------------------------------------------------
static SDL_AudioDeviceID g_out;
static host_audio_render g_render;
static void *g_render_user;

static void sdl_fill(void *user, Uint8 *stream, int len) {
  (void)user;
  g_render(g_render_user, (float *)stream, len / (int)(2 * sizeof(float)));
}

int host_audio_out_start(int rate, int frames, host_audio_render cb, void *user) {
  if (g_out) return -1;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    fprintf(stderr, "[audio] no sound output: %s\n", SDL_GetError());
    return -1;
  }
  SDL_AudioSpec want = {0}, have;
  want.freq = rate;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = (Uint16)frames;
  want.callback = sdl_fill;
  g_render = cb;
  g_render_user = user;
  const char *dev = getenv("RS_AUDIO_OUT");
  g_out = SDL_OpenAudioDevice(dev && *dev ? dev : NULL, 0, &want, &have, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  if (!g_out) {
    fprintf(stderr, "[audio] no sound output: %s\n", SDL_GetError());
    return -1;
  }
  fprintf(stderr, "[audio] output: %s, %d Hz, %d frames a buffer\n", SDL_GetCurrentAudioDriver(), have.freq, have.samples);
  SDL_PauseAudioDevice(g_out, 0);
  return have.samples;
}

void host_audio_out_stop(void) {
  if (!g_out) return;
  SDL_CloseAudioDevice(g_out);
  g_out = 0;
}

// --- the cable -------------------------------------------------------------------------------------
// The ALSA device of the cable: "hw:N,0" of the card whose name has "Rocksmith", or RS_AUDIO_IN.
static int cable_device(char *dev, size_t cap, char *name, size_t ncap) {
  const char *e = getenv("RS_AUDIO_IN");
  if (e && *e) {
    snprintf(dev, cap, "%s", e);
    if (name) snprintf(name, ncap, "%s", e);
    return 1;
  }
  for (int card = -1; snd_card_next(&card) == 0 && card >= 0;) {
    char *n = 0;
    if (snd_card_get_name(card, &n) || !n) continue;
    int is = strstr(n, "Rocksmith") != 0;
    if (is) {
      snprintf(dev, cap, "hw:%d,0", card);
      if (name) snprintf(name, ncap, "%s", n);
    }
    free(n);
    if (is) return 1;
  }
  return 0;
}

int host_audio_in_present(char *name, size_t cap) {
  char dev[64];
  return cable_device(dev, sizeof dev, name, cap);
}

static pthread_t g_in_thread;
static _Atomic int g_in_run;
static snd_pcm_t *g_in;
static host_audio_capture g_capture;
static void *g_capture_user;
static int g_in_frames;

static void *in_run(void *arg) {
  (void)arg;
  pthread_setname_np(pthread_self(), "rs-cable-in");
  struct sched_param sp = {.sched_priority = 20};
  pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);  // when the system allows it
  int16_t raw[4096];
  float mono[4096];
  int n = g_in_frames > 4096 ? 4096 : g_in_frames;
  while (atomic_load(&g_in_run)) {
    snd_pcm_sframes_t got = snd_pcm_readi(g_in, raw, (snd_pcm_uframes_t)n);
    if (got < 0) {
      if (snd_pcm_recover(g_in, (int)got, 1) < 0) {
        fprintf(stderr, "[audio] the cable stopped: %s\n", snd_strerror((int)got));
        break;
      }
      continue;
    }
    for (snd_pcm_sframes_t i = 0; i < got; i++) mono[i] = (float)raw[i] * (1.0f / 32768.0f);
    if (got > 0) g_capture(g_capture_user, mono, (int)got);
  }
  return 0;
}

int host_audio_in_start(int rate, int frames, host_audio_capture cb, void *user) {
  if (g_in) return -1;
  char dev[64], name[128];
  if (!cable_device(dev, sizeof dev, name, sizeof name)) return -1;
  int err = snd_pcm_open(&g_in, dev, SND_PCM_STREAM_CAPTURE, 0);
  if (err < 0) {
    fprintf(stderr, "[audio] the cable (%s) did not open: %s\n", dev, snd_strerror(err));
    g_in = 0;
    return -1;
  }
  // mono 16-bit at the game's rate, in periods of `frames` with two periods of buffer
  snd_pcm_hw_params_t *hw;
  snd_pcm_hw_params_alloca(&hw);
  snd_pcm_hw_params_any(g_in, hw);
  unsigned r = (unsigned)rate;
  snd_pcm_uframes_t period = (snd_pcm_uframes_t)frames, buffer = period * 4;
  if ((err = snd_pcm_hw_params_set_access(g_in, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
      (err = snd_pcm_hw_params_set_format(g_in, hw, SND_PCM_FORMAT_S16_LE)) < 0 ||
      (err = snd_pcm_hw_params_set_channels(g_in, hw, 1)) < 0 ||
      (err = snd_pcm_hw_params_set_rate_near(g_in, hw, &r, 0)) < 0 ||
      (err = snd_pcm_hw_params_set_period_size_near(g_in, hw, &period, 0)) < 0 ||
      (err = snd_pcm_hw_params_set_buffer_size_near(g_in, hw, &buffer)) < 0 || (err = snd_pcm_hw_params(g_in, hw)) < 0) {
    fprintf(stderr, "[audio] the cable (%s) did not take 48 kHz mono: %s\n", dev, snd_strerror(err));
    snd_pcm_close(g_in);
    g_in = 0;
    return -1;
  }
  g_capture = cb;
  g_capture_user = user;
  g_in_frames = (int)period;
  atomic_store(&g_in_run, 1);
  if (pthread_create(&g_in_thread, 0, in_run, 0)) {
    atomic_store(&g_in_run, 0);
    snd_pcm_close(g_in);
    g_in = 0;
    return -1;
  }
  fprintf(stderr, "[audio] cable: %s (%s), %u Hz, %lu frames a period\n", name, dev, r, (unsigned long)period);
  return 0;
}

void host_audio_in_stop(void) {
  if (!g_in) return;
  atomic_store(&g_in_run, 0);
  snd_pcm_drop(g_in);
  pthread_join(g_in_thread, 0);
  snd_pcm_close(g_in);
  g_in = 0;
}
