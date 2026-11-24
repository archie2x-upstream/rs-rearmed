// The Darwin layer: CoreAudio and the AUGraph, as far as the game uses them (dw.h).
//
// The game's sound engine opens an AUGraph with one output unit on the default output device, sets the
// device to 48 kHz, and is pulled by the unit's render callback. This file answers the property
// questions that setup asks and runs that callback from a thread of its own.
//
// Sound itself is the host's (host_audio_linux.h, when the program is linked with it): the output
// device plays through it, and the guitar cable is an input device while one is plugged in. Without
// the host's sound, or when its output does not open, the output device is a clock that pulls the
// engine at the device rate and drops the samples, so the engine runs as it does with a real device.
//
// DW_AUDIO_LOG=1 lists the calls and names every property this file has no answer for.
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dw.h"
#include "dw_cf.h"

void dw_CFRelease(void *o);

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define ERR_UNKNOWN_PROPERTY FOURCC('w', 'h', 'o', '?')
#define ERR_BAD_OBJECT FOURCC('!', 'o', 'b', 'j')
#define ERR_BAD_SIZE FOURCC('!', 's', 'i', 'z')
#define ERR_PARAM (-50)

#define OBJ_SYSTEM 1u
#define OBJ_OUT_DEVICE 0x40u
#define OBJ_OUT_STREAM 0x41u
#define OBJ_IN_DEVICE 0x50u  // the guitar cable
#define OBJ_IN_STREAM 0x51u
#define RATE 48000.0

// The host's sound (tools/ws7/recomp/app/host_audio_linux.h); weak, so this layer links without it.
typedef void (*host_audio_render)(void *user, float *stereo, int frames);
typedef void (*host_audio_capture)(void *user, const float *mono, int frames);
__attribute__((weak)) int host_audio_out_start(int rate, int frames, host_audio_render cb, void *user);
__attribute__((weak)) void host_audio_out_stop(void);
__attribute__((weak)) int host_audio_in_present(char *name, size_t cap);
__attribute__((weak)) int host_audio_in_start(int rate, int frames, host_audio_capture cb, void *user);
__attribute__((weak)) void host_audio_in_stop(void);

struct address { uint32_t selector, scope, element; };
struct asbd {  // AudioStreamBasicDescription
  double rate;
  uint32_t format, flags, bytes_per_packet, frames_per_packet, bytes_per_frame, channels, bits, reserved;
};
struct value_range { double min, max; };
struct audio_buffer { uint32_t channels, bytes; void *data; };
struct buffer_list { uint32_t count, pad; struct audio_buffer b[8]; };
struct timestamp {  // AudioTimeStamp
  double sample_time;
  uint64_t host_time;
  double rate_scalar;
  uint64_t word_clock;
  uint8_t smpte[24];
  uint32_t flags, reserved;
};
_Static_assert(sizeof(struct asbd) == 40, "AudioStreamBasicDescription");
_Static_assert(sizeof(struct timestamp) == 64, "AudioTimeStamp");

static int logging(void) {
  static int v = -1;
  if (v < 0) {
    const char *e = getenv("DW_AUDIO_LOG");
    v = e && *e && *e != '0';
  }
  return v;
}
static void say(const char *fmt, ...) {
  if (!logging()) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("[dw audio] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}
static const char *cc(uint32_t v, char *b) {
  for (int i = 0; i < 4; i++) {
    char c = (char)(v >> (24 - 8 * i));
    b[i] = c >= 32 && c < 127 ? c : '.';
  }
  b[4] = 0;
  return b;
}

static _Atomic uint32_t g_frames = 256;     // the output device's I/O buffer, in frames
static _Atomic uint32_t g_in_frames = 128;  // the cable's
static _Atomic uint32_t g_in_volume = 0x3f800000;  // the cable's input volume as the game set it (a float's bits)
static const struct asbd g_device_format = {RATE, FOURCC('l', 'p', 'c', 'm'), 9 /* float, packed */, 8, 1, 8, 2, 32, 0};
static const struct asbd g_cable_format = {RATE, FOURCC('l', 'p', 'c', 'm'), 9, 4, 1, 4, 1, 32, 0};

// Whether a cable is plugged in, asked of the host at most twice a second, and its name.
static char g_cable_name[128];
static int cable_present(void) {
  static _Atomic long last_ms = -1000000;
  static _Atomic int present;
  if (!host_audio_in_present) return 0;
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  long ms = (long)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
  if (ms - atomic_load(&last_ms) >= 500) {
    char name[128] = "";
    int p = host_audio_in_present(name, sizeof name);
    if (p) memcpy(g_cable_name, name, sizeof name);
    atomic_store(&present, p);
    atomic_store(&last_ms, ms);
  }
  return atomic_load(&present);
}

// --- properties ------------------------------------------------------------------------------------
// The answer to one property: its bytes in out (when not NULL) and its size, or an error.
static int32_t property(uint32_t obj, const struct address *a, void *out, uint32_t cap, uint32_t *size) {
  uint8_t v[64];
  uint32_t n = 0;
  uint32_t sel = a->selector, scope = a->scope;
  int output = scope != FOURCC('i', 'n', 'p', 't');
#define PUT(x) do { memcpy(v, &(x), sizeof(x)); n = sizeof(x); } while (0)
  if (obj == OBJ_SYSTEM) {
    uint32_t dev = OBJ_OUT_DEVICE, in = cable_present() ? OBJ_IN_DEVICE : 0;
    if (sel == FOURCC('d', 'e', 'v', '#')) {
      uint32_t both[2] = {OBJ_IN_DEVICE, OBJ_OUT_DEVICE};
      if (in) PUT(both);
      else PUT(dev);
    } else if (sel == FOURCC('d', 'O', 'u', 't') || sel == FOURCC('s', 'O', 'u', 't')) PUT(dev);
    else if (sel == FOURCC('d', 'I', 'n', ' ')) PUT(in);
    else goto unknown;
  } else if (obj == OBJ_IN_DEVICE) {
    int input = scope != FOURCC('o', 'u', 't', 'p');
    if (sel == FOURCC('l', 'n', 'a', 'm') || sel == FOURCC('u', 'i', 'd', ' ') || sel == FOURCC('l', 'm', 'a', 'k') ||
        sel == FOURCC('m', 'u', 'i', 'd')) {
      // The game knows the cable by its name, or by the USB ids in its model UID.
      void *s = dw_cf_string(sel == FOURCC('l', 'n', 'a', 'm') ? "Rocksmith USB Guitar Adapter"
                             : sel == FOURCC('l', 'm', 'a', 'k') ? "Hercules"
                             : sel == FOURCC('m', 'u', 'i', 'd') ? "Rocksmith USB Guitar Adapter:12BA:00FF"
                                                                 : "rs-rearmed-cable");
      PUT(s);
      if (!out) dw_CFRelease(s);
    } else if (sel == FOURCC('s', 't', 'm', '#')) {
      uint32_t st = OBJ_IN_STREAM;
      if (input) PUT(st);
    } else if (sel == FOURCC('s', 'l', 'a', 'y')) {
      struct { uint32_t count, pad; struct audio_buffer b; } l = {input ? 1u : 0u, 0, {1, 0, 0}};
      memcpy(v, &l, input ? 24 : 8);
      n = input ? 24 : 8;
    } else if (sel == FOURCC('n', 's', 'r', 't')) {
      double r = RATE;
      PUT(r);
    } else if (sel == FOURCC('n', 's', 'r', '#')) {
      struct value_range r = {RATE, RATE};
      PUT(r);
    } else if (sel == FOURCC('f', 's', 'i', 'z')) {
      uint32_t f = atomic_load(&g_in_frames);
      PUT(f);
    } else if (sel == FOURCC('f', 's', 'z', '#')) {
      struct value_range r = {64, 2048};
      PUT(r);
    } else if (sel == FOURCC('t', 'r', 'a', 'n')) {
      uint32_t t = FOURCC('u', 's', 'b', ' ');
      PUT(t);
    } else if (sel == FOURCC('l', 'i', 'v', 'n') || sel == FOURCC('g', 'o', 'i', 'n')) {
      uint32_t one = cable_present() ? 1u : 0u;
      PUT(one);
    } else if (sel == FOURCC('l', 't', 'n', 'c') || sel == FOURCC('s', 'a', 'f', 't') || sel == FOURCC('h', 'o', 'g', 'r')) {
      uint32_t zero = sel == FOURCC('h', 'o', 'g', 'r') ? (uint32_t)-1 : 0;
      PUT(zero);
    } else if (sel == FOURCC('s', 'f', 'm', 't') || sel == FOURCC('p', 'f', 't', ' ')) {
      if (!input) goto unknown;
      PUT(g_cable_format);
    } else if (sel == FOURCC('v', 'o', 'l', 'm')) {
      if (!input) goto unknown;
      uint32_t bits = atomic_load(&g_in_volume);
      PUT(bits);
    } else goto unknown;
  } else if (obj == OBJ_IN_STREAM) {
    if (sel == FOURCC('s', 'f', 'm', 't') || sel == FOURCC('p', 'f', 't', ' ')) PUT(g_cable_format);
    else if (sel == FOURCC('s', 'd', 'i', 'r')) {
      uint32_t one = 1;
      PUT(one);
    } else if (sel == FOURCC('l', 't', 'n', 'c')) {
      uint32_t zero = 0;
      PUT(zero);
    } else goto unknown;
  } else if (obj == OBJ_OUT_DEVICE) {
    if (sel == FOURCC('l', 'n', 'a', 'm') || sel == FOURCC('u', 'i', 'd', ' ') || sel == FOURCC('l', 'm', 'a', 'k') ||
        sel == FOURCC('m', 'u', 'i', 'd')) {
      void *s = dw_cf_string(sel == FOURCC('l', 'n', 'a', 'm') ? "Output" : sel == FOURCC('l', 'm', 'a', 'k') ? "RS Re-ARMed" : "rs-rearmed-output");
      PUT(s);  // the caller's to release
      if (!out) dw_CFRelease(s);
    } else if (sel == FOURCC('s', 't', 'm', '#')) {
      uint32_t s = OBJ_OUT_STREAM;
      if (output) PUT(s);
    } else if (sel == FOURCC('s', 'l', 'a', 'y')) {  // stream configuration: an AudioBufferList
      struct { uint32_t count, pad; struct audio_buffer b; } l = {output ? 1u : 0u, 0, {2, 0, 0}};
      memcpy(v, &l, output ? 24 : 8);
      n = output ? 24 : 8;
    } else if (sel == FOURCC('n', 's', 'r', 't')) {
      double r = RATE;
      PUT(r);
    } else if (sel == FOURCC('n', 's', 'r', '#')) {
      struct value_range r = {RATE, RATE};
      PUT(r);
    } else if (sel == FOURCC('f', 's', 'i', 'z')) {
      uint32_t f = atomic_load(&g_frames);
      PUT(f);
    } else if (sel == FOURCC('f', 's', 'z', '#')) {
      struct value_range r = {64, 4096};
      PUT(r);
    } else if (sel == FOURCC('t', 'r', 'a', 'n')) {
      uint32_t t = FOURCC('b', 'l', 't', 'n');
      PUT(t);
    } else if (sel == FOURCC('l', 'i', 'v', 'n') || sel == FOURCC('g', 'o', 'i', 'n')) {
      uint32_t one = 1;
      PUT(one);
    } else if (sel == FOURCC('l', 't', 'n', 'c') || sel == FOURCC('s', 'a', 'f', 't') || sel == FOURCC('h', 'o', 'g', 'r')) {
      uint32_t zero = sel == FOURCC('h', 'o', 'g', 'r') ? (uint32_t)-1 : 0;
      PUT(zero);
    } else if (sel == FOURCC('s', 'f', 'm', 't')) {
      if (!output) goto unknown;
      PUT(g_device_format);
    } else goto unknown;
  } else if (obj == OBJ_OUT_STREAM) {
    if (sel == FOURCC('s', 'f', 'm', 't') || sel == FOURCC('p', 'f', 't', ' ')) PUT(g_device_format);
    else if (sel == FOURCC('s', 'd', 'i', 'r') || sel == FOURCC('l', 't', 'n', 'c')) {
      uint32_t zero = 0;
      PUT(zero);
    } else goto unknown;
  } else {
    say("object %u: not one of this layer's", obj);
    return (int32_t)ERR_BAD_OBJECT;
  }
#undef PUT
  if (out) {
    if (cap < n) return (int32_t)ERR_BAD_SIZE;
    memcpy(out, v, n);
  }
  if (size) *size = n;
  return 0;
unknown:;
  char s1[5], s2[5];
  say("object %u has no property '%s' in scope '%s'", obj, cc(sel, s1), cc(scope, s2));
  return (int32_t)ERR_UNKNOWN_PROPERTY;
}

int32_t dw_AudioObjectGetPropertyDataSize(uint32_t obj, const struct address *a, uint32_t qsize, const void *q, uint32_t *size) {
  (void)qsize; (void)q;
  return a ? property(obj, a, 0, 0, size) : ERR_PARAM;
}
int32_t dw_AudioObjectGetPropertyData(uint32_t obj, const struct address *a, uint32_t qsize, const void *q, uint32_t *size, void *data) {
  (void)qsize; (void)q;
  if (!a || !size || !data) return ERR_PARAM;
  return property(obj, a, data, *size, size);
}
uint8_t dw_AudioObjectHasProperty(uint32_t obj, const struct address *a) {
  uint32_t n;
  return a && property(obj, a, 0, 0, &n) == 0;
}
int32_t dw_AudioObjectSetPropertyData(uint32_t obj, const struct address *a, uint32_t qsize, const void *q, uint32_t size, const void *data) {
  (void)qsize; (void)q;
  if (!a || !data) return ERR_PARAM;
  char s[5];
  if (obj == OBJ_OUT_DEVICE && a->selector == FOURCC('f', 's', 'i', 'z') && size == 4) {
    uint32_t f;
    memcpy(&f, data, 4);
    if (f < 64) f = 64;
    if (f > 4096) f = 4096;
    atomic_store(&g_frames, f);
    say("output device buffer: %u frames", f);
    return 0;
  }
  if (obj == OBJ_OUT_DEVICE && a->selector == FOURCC('n', 's', 'r', 't') && size == 8) {
    double r;
    memcpy(&r, data, 8);
    say("output device rate asked: %.0f (it is %.0f)", r, RATE);
    return 0;
  }
  if (obj == OBJ_IN_DEVICE && a->selector == FOURCC('f', 's', 'i', 'z') && size == 4) {
    uint32_t f;
    memcpy(&f, data, 4);
    if (f < 64) f = 64;
    if (f > 2048) f = 2048;
    atomic_store(&g_in_frames, f);
    say("cable buffer: %u frames", f);
    return 0;
  }
  if (obj == OBJ_IN_DEVICE && a->selector == FOURCC('v', 'o', 'l', 'm') && size == 4) {
    uint32_t bits;
    memcpy(&bits, data, 4);
    atomic_store(&g_in_volume, bits);
    return 0;
  }
  if (obj == OBJ_IN_DEVICE && a->selector == FOURCC('n', 's', 'r', 't') && size == 8) return 0;
  // A format the game sets on the cable: it is 48 kHz mono, handed over as float, whatever is asked.
  if ((obj == OBJ_IN_DEVICE || obj == OBJ_IN_STREAM) &&
      (a->selector == FOURCC('p', 'f', 't', ' ') || a->selector == FOURCC('s', 'f', 'm', 't')) && size >= sizeof(struct asbd)) {
    struct asbd f;
    memcpy(&f, data, sizeof f);
    say("cable format asked: %.0f Hz, %u channels, %u bits, flags 0x%x", f.rate, f.channels, f.bits, f.flags);
    return 0;
  }
  if (obj == OBJ_SYSTEM && a->selector == FOURCC('r', 'n', 'l', 'p')) return 0;  // the HAL's run loop: none here
  say("object %u: a write to '%s' (%u bytes) is not taken", obj, cc(a->selector, s), size);
  return (int32_t)ERR_UNKNOWN_PROPERTY;
}
// Nothing here changes by itself, so a listener is never called.
int32_t dw_AudioObjectAddPropertyListener(uint32_t obj, const struct address *a, void *proc, void *client) {
  (void)obj; (void)a; (void)proc; (void)client;
  return 0;
}
int32_t dw_AudioObjectRemovePropertyListener(uint32_t obj, const struct address *a, void *proc, void *client) {
  (void)obj; (void)a; (void)proc; (void)client;
  return 0;
}

// --- the cable's I/O proc ---------------------------------------------------------------------------
// OSStatus proc(device, now, inputData, inputTime, outputData, outputTime, client)
typedef int32_t (*io_proc)(uint32_t dev, const struct timestamp *now, const struct buffer_list *in,
                           const struct timestamp *in_time, struct buffer_list *out, const struct timestamp *out_time,
                           void *client);
static io_proc g_io_proc;
static void *g_io_client;
static double g_in_sample;
static uint64_t mono_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void cable_frames(void *user, const float *mono, int frames) {
  (void)user;
  io_proc proc = g_io_proc;
  if (!proc) return;
  float gain;
  uint32_t bits = atomic_load(&g_in_volume);
  memcpy(&gain, &bits, 4);
  (void)gain;  // the game applies its own level; the volume is only kept for it to read back
  struct buffer_list in = {1, 0, {{1, (uint32_t)frames * 4, (void *)(uintptr_t)mono}}}, out = {0, 0, {{0}}};
  struct timestamp ts = {g_in_sample, mono_ns(), 1.0, 0, {0}, 3, 0};
  g_in_sample += frames;
  proc(OBJ_IN_DEVICE, &ts, &in, &ts, &out, &ts, g_io_client);
}
int32_t dw_AudioDeviceCreateIOProcID(uint32_t dev, void *proc, void *client, void **id) {
  if (dev != OBJ_IN_DEVICE || !proc) {
    say("AudioDeviceCreateIOProcID(%u): not an input device of this layer", dev);
    if (id) *id = 0;
    return (int32_t)ERR_BAD_OBJECT;
  }
  g_io_client = client;
  g_io_proc = (io_proc)proc;
  if (id) *id = proc;
  return 0;
}
int32_t dw_AudioDeviceStart(uint32_t dev, void *id) {
  (void)id;
  if (dev != OBJ_IN_DEVICE || !g_io_proc || !host_audio_in_start) return (int32_t)ERR_BAD_OBJECT;
  if (host_audio_in_start((int)RATE, (int)atomic_load(&g_in_frames), cable_frames, 0)) return (int32_t)ERR_BAD_OBJECT;
  say("cable started, %u frames a period", atomic_load(&g_in_frames));
  return 0;
}
int32_t dw_AudioDeviceStop(uint32_t dev, void *id) {
  (void)id;
  if (dev == OBJ_IN_DEVICE && host_audio_in_stop) host_audio_in_stop();
  return 0;
}
int32_t dw_AudioDeviceDestroyIOProcID(uint32_t dev, void *id) {
  dw_AudioDeviceStop(dev, id);
  if (dev == OBJ_IN_DEVICE) g_io_proc = 0;
  return 0;
}

// --- the graph ---------------------------------------------------------------------------------------
typedef int32_t (*render_proc)(void *refcon, uint32_t *flags, const struct timestamp *ts, uint32_t bus, uint32_t frames,
                               struct buffer_list *io);
struct graph;
struct unit {
  struct graph *graph;
  uint32_t type, subtype;
  struct asbd in_format;  // what its input callback is asked for
  render_proc proc;
  void *refcon;
  struct unit *source;  // a unit connected to its input, when it has no callback of its own
};
#define MAX_NODES 8
struct graph {
  struct unit node[MAX_NODES];
  int nodes;
  pthread_t thread;
  _Atomic int running, stop;
  int on_host;  // played by the host's sound output, not the clock thread
};

int32_t dw_NewAUGraph(struct graph **out) {
  struct graph *g = calloc(1, sizeof *g);
  if (!g || !out) return ERR_PARAM;
  *out = g;
  return 0;
}
// AUGraphAddNode(graph, const AudioComponentDescription *, AUNode *): nodes are numbered from 1.
int32_t dw_AUGraphAddNode(struct graph *g, const uint32_t *desc, int32_t *node) {
  if (!g || !desc || !node || g->nodes == MAX_NODES) return ERR_PARAM;
  struct unit *u = &g->node[g->nodes++];
  u->graph = g;
  u->type = desc[0];
  u->subtype = desc[1];
  u->in_format = g_device_format;
  *node = g->nodes;
  char a[5], b[5];
  say("graph node %d: '%s' '%s'", *node, cc(u->type, a), cc(u->subtype, b));
  return 0;
}
static struct unit *node_of(struct graph *g, int32_t node) { return g && node >= 1 && node <= g->nodes ? &g->node[node - 1] : 0; }
int32_t dw_AUGraphOpen(struct graph *g) { return g ? 0 : ERR_PARAM; }
int32_t dw_AUGraphInitialize(struct graph *g) { return g ? 0 : ERR_PARAM; }
int32_t dw_AUGraphNodeInfo(struct graph *g, int32_t node, uint32_t *desc, struct unit **unit) {
  struct unit *u = node_of(g, node);
  if (!u) return ERR_PARAM;
  if (desc) {
    memset(desc, 0, 20);
    desc[0] = u->type;
    desc[1] = u->subtype;
    desc[2] = FOURCC('a', 'p', 'p', 'l');
  }
  if (unit) *unit = u;
  return 0;
}
int32_t dw_AUGraphConnectNodeInput(struct graph *g, int32_t src, uint32_t out_bus, int32_t dst, uint32_t in_bus) {
  (void)out_bus; (void)in_bus;
  struct unit *s = node_of(g, src), *d = node_of(g, dst);
  if (!s || !d) return ERR_PARAM;
  d->source = s;
  return 0;
}
// AUGraphSetNodeInputCallback(graph, node, bus, const AURenderCallbackStruct *)
int32_t dw_AUGraphSetNodeInputCallback(struct graph *g, int32_t node, uint32_t bus, void *const *cb) {
  (void)bus;
  struct unit *u = node_of(g, node);
  if (!u || !cb) return ERR_PARAM;
  u->proc = (render_proc)cb[0];
  u->refcon = cb[1];
  return 0;
}

// AudioUnitSetProperty(unit, property, scope, element, data, size)
int32_t dw_AudioUnitSetProperty(struct unit *u, uint32_t prop, uint32_t scope, uint32_t element, const void *data, uint32_t size) {
  if (!u) return ERR_PARAM;
  if (prop == 8 && data && size >= sizeof(struct asbd)) {  // kAudioUnitProperty_StreamFormat
    struct asbd f;
    memcpy(&f, data, sizeof f);
    say("unit stream format, scope %u bus %u: %.0f Hz, %u channels, %u bits, flags 0x%x", scope, element, f.rate,
        f.channels, f.bits, f.flags);
    if (scope == 1 /* input */) u->in_format = f;
    return 0;
  }
  if ((prop == 23 || prop == 2005) && data && size >= 16) {  // SetRenderCallback / SetInputCallback
    u->proc = (render_proc)((void *const *)data)[0];
    u->refcon = ((void *const *)data)[1];
    return 0;
  }
  say("unit property %u (scope %u bus %u, %u bytes): accepted, not used", prop, scope, element, size);
  return 0;
}
int32_t dw_AudioUnitSetParameter(struct unit *u, uint32_t id, uint32_t scope, uint32_t element, float value, uint32_t offset) {
  (void)u; (void)id; (void)scope; (void)element; (void)value; (void)offset;
  return 0;
}
int32_t dw_AudioUnitUninitialize(struct unit *u) { (void)u; return 0; }

// The unit whose input callback feeds the output: the output unit's own, or the one connected to it.
static struct unit *graph_source(struct graph *g) {
  struct unit *out = 0;
  for (int i = 0; i < g->nodes; i++)
    if (g->node[i].type == FOURCC('a', 'u', 'o', 'u')) out = &g->node[i];
  if (!out && g->nodes) out = &g->node[g->nodes - 1];
  struct unit *src = out;
  for (int hop = 0; src && !src->proc && src->source && hop < MAX_NODES; hop++) src = src->source;
  return src && src->proc ? src : 0;
}

// The host's output asks for stereo frames: the engine is pulled in blocks of at most the device buffer
// it was told (it sized its ring by that), in its own format, and the block is interleaved.
static double g_out_sample;
static void host_render(void *user, float *stereo, int frames) {
  struct graph *g = user;
  struct unit *src = graph_source(g);
  static float planar[2][4096];
  memset(stereo, 0, (size_t)frames * 2 * sizeof(float));
  if (!src) return;
  const struct asbd *f = &src->in_format;
  int is_float = (f->flags & 1) && f->bits == 32, planes = (f->flags & 0x20) != 0;
  uint32_t ch = f->channels >= 2 ? 2 : 1;
  if (!is_float) return;  // the engine asks for float; anything else stays silent
  for (int done = 0; done < frames;) {
    uint32_t block = atomic_load(&g_frames);
    if (block > 4096 / 2) block = 4096 / 2;
    if ((int)block > frames - done) block = (uint32_t)(frames - done);
    struct buffer_list l = {planes ? ch : 1, 0, {{0}}};
    if (planes)
      for (uint32_t i = 0; i < ch; i++) {
        l.b[i] = (struct audio_buffer){1, block * 4, planar[i]};
        memset(planar[i], 0, block * 4);
      }
    else {
      l.b[0] = (struct audio_buffer){ch, block * 4 * ch, planar[0]};
      memset(planar[0], 0, block * 4 * ch);
    }
    struct timestamp ts = {g_out_sample, mono_ns(), 1.0, 0, {0}, 3, 0};
    uint32_t flags = 0;
    src->proc(src->refcon, &flags, &ts, 0, block, &l);
    float *o = stereo + (size_t)done * 2;
    for (uint32_t i = 0; i < block; i++) {
      float a = planes ? planar[0][i] : planar[0][i * ch];
      float b = ch < 2 ? a : planes ? planar[1][i] : planar[0][i * ch + 1];
      o[2 * i] = a;
      o[2 * i + 1] = b;
    }
    g_out_sample += block;
    done += (int)block;
  }
}

// The clock: asks the output unit's input for a buffer every period and drops it.
static uint64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void *null_run(void *arg) {
  struct graph *g = arg;
  pthread_setname_np(pthread_self(), "dw-audio-out");
  struct unit *out = 0;
  for (int i = 0; i < g->nodes; i++)
    if (g->node[i].type == FOURCC('a', 'u', 'o', 'u')) out = &g->node[i];
  if (!out && g->nodes) out = &g->node[g->nodes - 1];
  struct unit *src = out;
  for (int hop = 0; src && !src->proc && src->source && hop < MAX_NODES; hop++) src = src->source;
  static float scratch[8][4096 * 8];
  double sample = 0;
  uint64_t next = now_ns();
  while (!atomic_load(&g->stop)) {
    uint32_t frames = atomic_load(&g_frames);
    if (src && src->proc) {
      const struct asbd *f = &src->in_format;
      uint32_t ch = f->channels ? f->channels : 2, bytes = f->bits ? f->bits / 8 : 4;
      int planar = (f->flags & 0x20) != 0;  // kAudioFormatFlagIsNonInterleaved
      struct buffer_list l = {planar ? (ch > 8 ? 8 : ch) : 1, 0, {{0}}};
      for (uint32_t i = 0; i < l.count; i++) {
        l.b[i].channels = planar ? 1 : ch;
        l.b[i].bytes = frames * bytes * (planar ? 1 : ch);
        if (l.b[i].bytes > sizeof scratch[0]) l.b[i].bytes = sizeof scratch[0];
        l.b[i].data = scratch[i];
        memset(scratch[i], 0, l.b[i].bytes);
      }
      struct timestamp ts = {sample, now_ns(), 1.0, 0, {0}, 3 /* sample and host time valid */, 0};
      uint32_t flags = 0;
      src->proc(src->refcon, &flags, &ts, 0, frames, &l);
    }
    sample += frames;
    next += (uint64_t)(frames * 1e9 / RATE);
    uint64_t t = now_ns();
    if (next > t) {
      struct timespec d = {(time_t)((next - t) / 1000000000ull), (long)((next - t) % 1000000000ull)};
      nanosleep(&d, 0);
    } else if (t - next > 500000000ull) next = t;  // far behind (a stopped process): start again from now
  }
  return 0;
}
int32_t dw_AUGraphStart(struct graph *g) {
  if (!g) return ERR_PARAM;
  if (atomic_exchange(&g->running, 1)) return 0;
  atomic_store(&g->stop, 0);
  if (host_audio_out_start && host_audio_out_start((int)RATE, (int)atomic_load(&g_frames), host_render, g) > 0) {
    g->on_host = 1;
    say("graph started on the host's sound output");
    return 0;
  }
  g->on_host = 0;
  if (pthread_create(&g->thread, 0, null_run, g)) {
    atomic_store(&g->running, 0);
    return ERR_PARAM;
  }
  say("graph started: no sound device, a clock at %.0f Hz and %u frames", RATE, atomic_load(&g_frames));
  return 0;
}
int32_t dw_AUGraphStop(struct graph *g) {
  if (!g) return ERR_PARAM;
  if (!atomic_exchange(&g->running, 0)) return 0;
  if (g->on_host) {
    if (host_audio_out_stop) host_audio_out_stop();
    return 0;
  }
  atomic_store(&g->stop, 1);
  pthread_join(g->thread, 0);
  return 0;
}
int32_t dw_AudioOutputUnitStop(struct unit *u) { return u && u->graph ? dw_AUGraphStop(u->graph) : 0; }
int32_t dw_AUGraphClose(struct graph *g) { return dw_AUGraphStop(g); }
int32_t dw_DisposeAUGraph(struct graph *g) {
  dw_AUGraphStop(g);
  free(g);
  return 0;
}
