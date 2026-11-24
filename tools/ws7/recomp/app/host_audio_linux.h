// Sound on a Linux host (host_audio_linux.c): one stereo output and the guitar cable's mono input, both
// 32-bit float. The Darwin layer's CoreAudio (shims/linux/darwin/dw_coreaudio.c) runs on these when the
// program has them; nothing here knows about the game.
#ifndef HOST_AUDIO_LINUX_H
#define HOST_AUDIO_LINUX_H

#include <stddef.h>

// Fill `frames` frames of interleaved stereo. Called on the output's own thread.
typedef void (*host_audio_render)(void *user, float *stereo, int frames);
// Start the output at `rate` Hz asking for buffers of `frames` frames. Returns the buffer size the
// device took, or -1 when there is no output (the caller then runs without sound).
int host_audio_out_start(int rate, int frames, host_audio_render cb, void *user);
void host_audio_out_stop(void);

// The Rocksmith Real Tone Cable: 1 and its card's name when one is plugged in (or RS_AUDIO_IN names an
// ALSA capture device to use in its place).
int host_audio_in_present(char *name, size_t cap);
// `frames` mono frames from the cable. Called on the input's own thread.
typedef void (*host_audio_capture)(void *user, const float *mono, int frames);
// Start capturing at `rate` Hz in periods of `frames`. 0 on success.
int host_audio_in_start(int rate, int frames, host_audio_capture cb, void *user);
void host_audio_in_stop(void);

#endif
