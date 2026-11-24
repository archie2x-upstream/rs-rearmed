// An OpenGL context without a window on a Linux host (host_gl_linux.c).
#ifndef HOST_GL_LINUX_H
#define HOST_GL_LINUX_H

// Make an offscreen compatibility-profile context of this size current on the calling thread. 0 on success.
int host_gl_offscreen(int width, int height);
// End of a frame.
void host_gl_swap(void);
// Write the default framebuffer to a PPM file. 0 on success.
int host_gl_save_ppm(const char *path);

#endif
