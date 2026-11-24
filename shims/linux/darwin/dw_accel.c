// The Darwin layer: the vector and signal-processing functions the game takes from libsystem_m and
// Accelerate (dw.h): the 4x4 matrix inverse, the 4-wide cosf and powf, and vDSP's complex FFT.
#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef float v4 __attribute__((ext_vector_type(4)));
typedef struct { v4 col[4]; } m4;

// simd_float4x4 __invert_f4(simd_float4x4): the runtime looks this name up (rc_native.c). A singular
// matrix gives non-finite elements, as the division does there.
__attribute__((visibility("default"))) m4 __invert_f4(m4 in) {
  float m[16], inv[16];
  memcpy(m, &in, sizeof m);  // column-major; the cofactor expansion holds for either order
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  float s = 1.0f / det;
  for (int i = 0; i < 16; i++) inv[i] *= s;
  m4 out;
  memcpy(&out, inv, sizeof out);
  return out;
}

// matrix_identity_float4x4 (simd/matrix.h)
const m4 dw_matrix_identity_float4x4 = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

// vForce's 4-wide forms.
v4 dw_vcosf(v4 x) { return (v4){cosf(x[0]), cosf(x[1]), cosf(x[2]), cosf(x[3])}; }
v4 dw_vpowf(v4 x, v4 y) { return (v4){powf(x[0], y[0]), powf(x[1], y[1]), powf(x[2], y[2]), powf(x[3], y[3])}; }

// --- vDSP: complex FFT, out of place, single precision ------------------------------------------------
// The setup holds nothing the transform needs here; it is a token the game hands back.
struct split { float *re, *im; };
void *dw_vDSP_create_fftsetup(uint64_t log2n, int32_t radix) {
  (void)log2n; (void)radix;
  return malloc(8);
}
void dw_vDSP_destroy_fftsetup(void *setup) { free(setup); }
// vDSP_fft_zop(setup, A, strideA, C, strideC, log2n, direction): direction +1 forward, -1 inverse; neither
// is scaled, as in vDSP.
void dw_vDSP_fft_zop(void *setup, const struct split *a, int64_t ia, const struct split *c, int64_t ic, uint64_t log2n,
                     int32_t direction) {
  (void)setup;
  if (!a || !c || log2n > 24) return;
  size_t n = (size_t)1 << log2n;
  float *re = malloc(2 * n * sizeof *re);
  if (!re) return;
  float *im = re + n;
  for (size_t i = 0; i < n; i++) {  // bit-reversed copy in
    size_t j = 0;
    for (uint64_t b = 0; b < log2n; b++) j |= (i >> b & 1) << (log2n - 1 - b);
    re[j] = a->re[(int64_t)i * ia];
    im[j] = a->im[(int64_t)i * ia];
  }
  double sign = direction >= 0 ? -1.0 : 1.0;
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = sign * 2.0 * M_PI / (double)len;
    float wr = (float)cos(ang), wi = (float)sin(ang);
    for (size_t i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (size_t k = 0; k < len / 2; k++) {
        size_t p = i + k, q = p + len / 2;
        float tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
        re[q] = re[p] - tr;
        im[q] = im[p] - ti;
        re[p] += tr;
        im[p] += ti;
        float nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
  for (size_t i = 0; i < n; i++) {
    c->re[(int64_t)i * ic] = re[i];
    c->im[(int64_t)i * ic] = im[i];
  }
  free(re);
}
