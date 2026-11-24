// The platform layer on a Linux host: what rsp_engine.c needs from the Mac-only half.
//
// The Metal session marker and shader cache (rsp_renderer.c) belong to the macOS renderer choice;
// a Linux host draws with the system's OpenGL and has neither.
#include "rsp_renderer.h"

void rsp_metal_session_end(void) {}
void rsp_metal_cache_flush(void) {}
