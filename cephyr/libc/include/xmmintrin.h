/* salvo-libc <xmmintrin.h> — portable SSE-compatible intrinsic surface.
 *
 * The value types are deliberately fixed-width and implementation-neutral.
 * Cephyr's vector kernels may recognize these canonical operations and lower
 * them through the CCWeave Glue/SIMDe layer; the scalar bodies keep the
 * header usable by freestanding fallback builds and tests.
 */
#ifndef SALVO_XMMINTRIN_H
#define SALVO_XMMINTRIN_H

#include <stdint.h>

typedef struct
{
  float v[4];
} __m128;
typedef struct
{
  double v[2];
} __m128d;

static inline __m128
_mm_setzero_ps (void)
{
  __m128 r = { { 0.0f, 0.0f, 0.0f, 0.0f } };
  return r;
}
static inline __m128
_mm_set1_ps (float x)
{
  __m128 r = { { x, x, x, x } };
  return r;
}
static inline __m128
_mm_set_ps (float e3, float e2, float e1, float e0)
{
  __m128 r = { { e0, e1, e2, e3 } };
  return r;
}
static inline __m128
_mm_loadu_ps (const float *p)
{
  __m128 r = { { p[0], p[1], p[2], p[3] } };
  return r;
}
static inline __m128
_mm_load_ps (const float *p)
{
  return _mm_loadu_ps (p);
}
static inline void
_mm_storeu_ps (float *p, __m128 a)
{
  p[0] = a.v[0];
  p[1] = a.v[1];
  p[2] = a.v[2];
  p[3] = a.v[3];
}
static inline void
_mm_store_ps (float *p, __m128 a)
{
  _mm_storeu_ps (p, a);
}

#define SALVO_M128_BINOP(name, expr)                                          \
  static inline __m128 name (__m128 a, __m128 b)                              \
  {                                                                           \
    __m128 r;                                                                 \
    for (int i = 0; i < 4; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_M128_BINOP (_mm_add_ps, a.v[i] + b.v[i])
SALVO_M128_BINOP (_mm_sub_ps, a.v[i] - b.v[i])
SALVO_M128_BINOP (_mm_mul_ps, a.v[i] * b.v[i])
SALVO_M128_BINOP (_mm_div_ps, a.v[i] / b.v[i])
SALVO_M128_BINOP (_mm_min_ps, a.v[i] < b.v[i] ? a.v[i] : b.v[i])
SALVO_M128_BINOP (_mm_max_ps, a.v[i] > b.v[i] ? a.v[i] : b.v[i])
#undef SALVO_M128_BINOP

static inline __m128
_mm_and_ps (__m128 a, __m128 b)
{
  __m128 r;
  for (int i = 0; i < 4; ++i)
    {
      union
      {
        float f;
        uint32_t u;
      } x = { a.v[i] }, y = { b.v[i] }, z;
      z.u = x.u & y.u;
      r.v[i] = z.f;
    }
  return r;
}
static inline __m128
_mm_or_ps (__m128 a, __m128 b)
{
  __m128 r;
  for (int i = 0; i < 4; ++i)
    {
      union
      {
        float f;
        uint32_t u;
      } x = { a.v[i] }, y = { b.v[i] }, z;
      z.u = x.u | y.u;
      r.v[i] = z.f;
    }
  return r;
}
static inline __m128
_mm_xor_ps (__m128 a, __m128 b)
{
  __m128 r;
  for (int i = 0; i < 4; ++i)
    {
      union
      {
        float f;
        uint32_t u;
      } x = { a.v[i] }, y = { b.v[i] }, z;
      z.u = x.u ^ y.u;
      r.v[i] = z.f;
    }
  return r;
}
static inline __m128
_mm_andnot_ps (__m128 a, __m128 b)
{
  __m128 r;
  for (int i = 0; i < 4; ++i)
    {
      union
      {
        float f;
        uint32_t u;
      } x = { a.v[i] }, y = { b.v[i] }, z;
      z.u = (~x.u) & y.u;
      r.v[i] = z.f;
    }
  return r;
}
static inline __m128
_mm_shuffle_ps (__m128 a, __m128 b, int imm8)
{
  __m128 r = { { a.v[imm8 & 3], a.v[(imm8 >> 2) & 3], b.v[(imm8 >> 4) & 3],
                 b.v[(imm8 >> 6) & 3] } };
  return r;
}
static inline float
_mm_cvtss_f32 (__m128 a)
{
  return a.v[0];
}

static inline __m128d
_mm_setzero_pd (void)
{
  __m128d r = { { 0.0, 0.0 } };
  return r;
}
static inline __m128d
_mm_set1_pd (double x)
{
  __m128d r = { { x, x } };
  return r;
}
static inline __m128d
_mm_set_pd (double e1, double e0)
{
  __m128d r = { { e0, e1 } };
  return r;
}
static inline __m128d
_mm_loadu_pd (const double *p)
{
  __m128d r = { { p[0], p[1] } };
  return r;
}
static inline __m128d
_mm_load_pd (const double *p)
{
  return _mm_loadu_pd (p);
}
static inline void
_mm_storeu_pd (double *p, __m128d a)
{
  p[0] = a.v[0];
  p[1] = a.v[1];
}
static inline void
_mm_store_pd (double *p, __m128d a)
{
  _mm_storeu_pd (p, a);
}

#define SALVO_M128D_BINOP(name, expr)                                         \
  static inline __m128d name (__m128d a, __m128d b)                           \
  {                                                                           \
    __m128d r;                                                                \
    for (int i = 0; i < 2; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_M128D_BINOP (_mm_add_pd, a.v[i] + b.v[i])
SALVO_M128D_BINOP (_mm_sub_pd, a.v[i] - b.v[i])
SALVO_M128D_BINOP (_mm_mul_pd, a.v[i] * b.v[i])
SALVO_M128D_BINOP (_mm_div_pd, a.v[i] / b.v[i])
#undef SALVO_M128D_BINOP

#endif /* SALVO_XMMINTRIN_H */
