// An OpenGL context without a window, for the Linux host's checks (rc_main_linux.c --boot).
//
// EGL's device platform gives a context on a chosen graphics card with no display server; a pbuffer
// is its default framebuffer, so the game draws to "the window" as it always does and the picture is
// read back from there. The context is the compatibility profile: the game draws OpenGL 2.1.
//
// The card: RS_GL_DEVICE names its DRM node (/dev/dri/renderD129) or a part of its EGL device
// string; without it, the first device that is not software and gives a desktop OpenGL context.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_gl_linux.h"

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLSurface g_surf = EGL_NO_SURFACE;
static int g_w, g_h;

static int try_device(EGLDeviceEXT dev, const char *what, int w, int h) {
  EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, dev, NULL);
  EGLint major, minor;
  if (d == EGL_NO_DISPLAY || !eglInitialize(d, &major, &minor)) return 0;
  if (!eglBindAPI(EGL_OPENGL_API)) goto no;
  const EGLint want[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8,
                         EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                         EGL_NONE};
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, want, &cfg, 1, &n) || n < 1) goto no;
  const EGLint compat[] = {EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT, EGL_NONE};
  EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, compat);
  if (c == EGL_NO_CONTEXT) c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, NULL);
  if (c == EGL_NO_CONTEXT) goto no;
  const EGLint size[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
  EGLSurface s = eglCreatePbufferSurface(d, cfg, size);
  if (s == EGL_NO_SURFACE || !eglMakeCurrent(d, s, s, c)) {
    if (s != EGL_NO_SURFACE) eglDestroySurface(d, s);
    eglDestroyContext(d, c);
    goto no;
  }
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  int soft = !renderer || strstr(renderer, "llvmpipe") || strstr(renderer, "softpipe");
  const char *pick = getenv("RS_GL_DEVICE");
  if (soft && !(pick && *pick)) {  // a software renderer only when it is asked for by name
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(d, s);
    eglDestroyContext(d, c);
    goto no;
  }
  g_dpy = d;
  g_ctx = c;
  g_surf = s;
  g_w = w;
  g_h = h;
  fprintf(stderr, "[gl] %s: %s, OpenGL %s, %dx%d offscreen\n", what, renderer, (const char *)glGetString(GL_VERSION), w, h);
  return 1;
no:
  eglTerminate(d);
  return 0;
}

int host_gl_offscreen(int width, int height) {
  const char *ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (!ext || !strstr(ext, "EGL_EXT_device_enumeration") || !strstr(ext, "EGL_EXT_platform_device")) {
    fprintf(stderr, "[gl] this EGL cannot list devices (no EGL_EXT_device_enumeration)\n");
    return -1;
  }
  PFNEGLQUERYDEVICESEXTPROC query_devices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
  PFNEGLQUERYDEVICESTRINGEXTPROC device_string =
      (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
  EGLDeviceEXT devs[16];
  EGLint n = 0;
  if (!query_devices || !query_devices(16, devs, &n) || n < 1) {
    fprintf(stderr, "[gl] no EGL devices\n");
    return -1;
  }
  const char *pick = getenv("RS_GL_DEVICE");
  for (EGLint i = 0; i < n; i++) {
    char what[256] = "";
    const char *e = device_string ? device_string(devs[i], EGL_EXTENSIONS) : 0;
    const char *node = e && strstr(e, "EGL_EXT_device_drm_render_node") ? device_string(devs[i], EGL_DRM_RENDER_NODE_FILE_EXT) : 0;
    if (!node && e && strstr(e, "EGL_EXT_device_drm")) node = device_string(devs[i], EGL_DRM_DEVICE_FILE_EXT);
    snprintf(what, sizeof what, "device %d %s", (int)i, node ? node : "(no DRM node)");
    if (pick && *pick && !strstr(what, pick) && !(e && strstr(e, pick))) continue;
    if (try_device(devs[i], what, width, height)) return 0;
  }
  fprintf(stderr, "[gl] no device gave a desktop OpenGL context%s%s\n", pick && *pick ? " matching " : "", pick && *pick ? pick : "");
  return -1;
}

void host_gl_swap(void) {
  if (g_dpy != EGL_NO_DISPLAY) eglSwapBuffers(g_dpy, g_surf);
}

// The default framebuffer as a binary PPM, top row first.
int host_gl_save_ppm(const char *path) {
  if (g_dpy == EGL_NO_DISPLAY) return -1;
  size_t row = (size_t)g_w * 3;
  unsigned char *px = malloc(row * (size_t)g_h);
  if (!px) return -1;
  GLint fb = 0;
  glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &fb);
  void (*bind)(GLenum, GLuint) = (void (*)(GLenum, GLuint))eglGetProcAddress("glBindFramebuffer");
  if (bind) bind(0x8D40 /* GL_FRAMEBUFFER */, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, g_w, g_h, GL_RGB, GL_UNSIGNED_BYTE, px);
  if (bind) bind(0x8D40, (GLuint)fb);
  FILE *f = fopen(path, "wb");
  if (!f) {
    free(px);
    return -1;
  }
  fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
  for (int y = g_h - 1; y >= 0; y--) fwrite(px + (size_t)y * row, 1, row, f);
  fclose(f);
  free(px);
  return 0;
}
