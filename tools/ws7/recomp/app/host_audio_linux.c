// Sound on a Linux host (host_audio_linux.h).
//
// Output goes through SDL, which finds the desktop's sound server (PipeWire, PulseAudio) or ALSA: the
// game shares the output with everything else. RS_AUDIO_OUT names an SDL output device.
// The cable is read from ALSA directly, the card itself (hw), for the smallest delay: it is the one
// capture device with "Rocksmith" in its name. RS_AUDIO_IN names another ALSA capture device.
//
// For checks without a guitar: RS_AUDIO_IN_RECORD=FILE writes what the cable gives (raw 16-bit mono at
// the capture rate), and RS_AUDIO_IN=file:FILE plays such a file as the cable, at its own pace, then
// silence.
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
  if (e && !strncmp(e, "file:", 5)) {
    snprintf(dev, cap, "%s", e);
    if (name) snprintf(name, ncap, "Rocksmith USB Guitar Adapter (a recording)");
    return 1;
  }
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
  char dev[1100];
  return cable_device(dev, sizeof dev, name, cap);
}

static pthread_t g_in_thread;
static _Atomic int g_in_run;
static snd_pcm_t *g_in;
static host_audio_capture g_capture;
static void *g_capture_user;
static int g_in_frames;

static FILE *g_in_file;  // RS_AUDIO_IN=file:...: the recording played as the cable

// The recording at the capture's pace: a period every period's time, silence after its end.
static void *file_run(void *arg) {
  (void)arg;
  pthread_setname_np(pthread_self(), "rs-cable-file");
  int16_t raw[4096];
  float mono[4096];
  int n = g_in_frames > 4096 ? 4096 : g_in_frames;
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  while (atomic_load(&g_in_run)) {
    size_t got = g_in_file ? fread(raw, 2, (size_t)n, g_in_file) : 0;
    for (int i = 0; i < n; i++) mono[i] = (size_t)i < got ? (float)raw[i] * (1.0f / 32768.0f) : 0.0f;
    g_capture(g_capture_user, mono, n);
    next.tv_nsec += (long)(1e9 * n / 48000.0);
    if (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, 0);
  }
  return 0;
}

static void *in_run(void *arg) {
  (void)arg;
  pthread_setname_np(pthread_self(), "rs-cable-in");
  const char *rec = getenv("RS_AUDIO_IN_RECORD");
  FILE *record = rec && *rec ? fopen(rec, "wb") : 0;
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
    if (record && got > 0) fwrite(raw, 2, (size_t)got, record);
    if (got > 0) g_capture(g_capture_user, mono, (int)got);
  }
  if (record) fclose(record);
  return 0;
}

int host_audio_in_start(int rate, int frames, host_audio_capture cb, void *user) {
  if (g_in || atomic_load(&g_in_run)) return -1;
  char dev[1100], name[128];
  if (!cable_device(dev, sizeof dev, name, sizeof name)) return -1;
  if (!strncmp(dev, "file:", 5)) {
    g_in_file = fopen(dev + 5, "rb");
    if (!g_in_file) fprintf(stderr, "[audio] %s: no such recording, the cable is silent\n", dev + 5);
    g_capture = cb;
    g_capture_user = user;
    g_in_frames = frames;
    atomic_store(&g_in_run, 1);
    if (pthread_create(&g_in_thread, 0, file_run, 0)) {
      atomic_store(&g_in_run, 0);
      return -1;
    }
    fprintf(stderr, "[audio] cable: %s, %d frames a period\n", dev, frames);
    return 0;
  }
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
  if (!g_in && atomic_load(&g_in_run)) {  // a recording
    atomic_store(&g_in_run, 0);
    pthread_join(g_in_thread, 0);
    if (g_in_file) fclose(g_in_file);
    g_in_file = 0;
    return;
  }
  if (!g_in) return;
  atomic_store(&g_in_run, 0);
  snd_pcm_drop(g_in);
  pthread_join(g_in_thread, 0);
  snd_pcm_close(g_in);
  g_in = 0;
}

// --- the cable's gain: its capture volume control in the card's mixer --------------------------------
// Opens the mixer of the cable's card and finds its capture volume; NULL when there is none.
static snd_mixer_elem_t *gain_control(snd_mixer_t **mixer) {
  char dev[1100], card[32];
  int n;
  *mixer = 0;
  if (!cable_device(dev, sizeof dev, 0, 0) || sscanf(dev, "hw:%d", &n) != 1) return 0;
  snprintf(card, sizeof card, "hw:%d", n);
  if (snd_mixer_open(mixer, 0) < 0) return 0;
  if (snd_mixer_attach(*mixer, card) < 0 || snd_mixer_selem_register(*mixer, 0, 0) < 0 || snd_mixer_load(*mixer) < 0) {
    snd_mixer_close(*mixer);
    *mixer = 0;
    return 0;
  }
  for (snd_mixer_elem_t *e = snd_mixer_first_elem(*mixer); e; e = snd_mixer_elem_next(e))
    if (snd_mixer_selem_is_active(e) && snd_mixer_selem_has_capture_volume(e)) return e;
  snd_mixer_close(*mixer);
  *mixer = 0;
  return 0;
}

int host_audio_in_gain_set(float gain) {
  snd_mixer_t *m;
  snd_mixer_elem_t *e = gain_control(&m);
  if (!e) return -1;
  long lo, hi;
  snd_mixer_selem_get_capture_volume_range(e, &lo, &hi);
  if (gain < 0) gain = 0;
  if (gain > 1) gain = 1;
  long v = lo + (long)((float)(hi - lo) * gain + 0.5f);
  int r = snd_mixer_selem_set_capture_volume_all(e, v);
  if (snd_mixer_selem_has_capture_switch(e)) snd_mixer_selem_set_capture_switch_all(e, 1);
  long cdb = 0;
  snd_mixer_selem_get_capture_dB(e, SND_MIXER_SCHN_MONO, &cdb);
  fprintf(stderr, "[audio] cable gain %.3f: step %ld of %ld..%ld (%.1f dB)\n", gain, v, lo, hi, cdb / 100.0);
  snd_mixer_close(m);
  return r < 0 ? -1 : 0;
}

float host_audio_in_gain_get(void) {
  snd_mixer_t *m;
  snd_mixer_elem_t *e = gain_control(&m);
  if (!e) return -1;
  long lo, hi, v = 0;
  snd_mixer_selem_get_capture_volume_range(e, &lo, &hi);
  snd_mixer_selem_get_capture_volume(e, SND_MIXER_SCHN_MONO, &v);
  snd_mixer_close(m);
  return hi > lo ? (float)(v - lo) / (float)(hi - lo) : -1;
}
