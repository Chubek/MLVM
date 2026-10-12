/* salvo-libc <avxintrin.h> — portable AVX-width intrinsic subset. */
#ifndef SALVO_AVXINTRIN_H
#define SALVO_AVXINTRIN_H

#include "xmmintrin.h"

typedef struct
{
  float v[8];
} __m256;
typedef struct
{
  double v[4];
} __m256d;

static inline __m256
_mm256_setzero_ps (void)
{
  __m256 r = { { 0, 0, 0, 0, 0, 0, 0, 0 } };
  return r;
}
static inline __m256
_mm256_set1_ps (float x)
{
  __m256 r = { { x, x, x, x, x, x, x, x } };
  return r;
}
static inline __m256
_mm256_loadu_ps (const float *p)
{
  __m256 r;
  for (int i = 0; i < 8; ++i)
    r.v[i] = p[i];
  return r;
}
static inline __m256
_mm256_load_ps (const float *p)
{
  return _mm256_loadu_ps (p);
}
static inline void
_mm256_storeu_ps (float *p, __m256 a)
{
  for (int i = 0; i < 8; ++i)
    p[i] = a.v[i];
}
static inline void
_mm256_store_ps (float *p, __m256 a)
{
  _mm256_storeu_ps (p, a);
}

#define SALVO_M256_BINOP(name, expr)                                          \
  static inline __m256 name (__m256 a, __m256 b)                              \
  {                                                                           \
    __m256 r;                                                                 \
    for (int i = 0; i < 8; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_M256_BINOP (_mm256_add_ps, a.v[i] + b.v[i])
SALVO_M256_BINOP (_mm256_sub_ps, a.v[i] - b.v[i])
SALVO_M256_BINOP (_mm256_mul_ps, a.v[i] * b.v[i])
SALVO_M256_BINOP (_mm256_div_ps, a.v[i] / b.v[i])
#undef SALVO_M256_BINOP

static inline __m256d
_mm256_setzero_pd (void)
{
  __m256d r = { { 0, 0, 0, 0 } };
  return r;
}
static inline __m256d
_mm256_set1_pd (double x)
{
  __m256d r = { { x, x, x, x } };
  return r;
}
static inline __m256d
_mm256_loadu_pd (const double *p)
{
  __m256d r;
  for (int i = 0; i < 4; ++i)
    r.v[i] = p[i];
  return r;
}
static inline __m256d
_mm256_load_pd (const double *p)
{
  return _mm256_loadu_pd (p);
}
static inline void
_mm256_storeu_pd (double *p, __m256d a)
{
  for (int i = 0; i < 4; ++i)
    p[i] = a.v[i];
}
static inline void
_mm256_store_pd (double *p, __m256d a)
{
  _mm256_storeu_pd (p, a);
}

#define SALVO_M256D_BINOP(name, expr)                                         \
  static inline __m256d name (__m256d a, __m256d b)                           \
  {                                                                           \
    __m256d r;                                                                \
    for (int i = 0; i < 4; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_M256D_BINOP (_mm256_add_pd, a.v[i] + b.v[i])
SALVO_M256D_BINOP (_mm256_sub_pd, a.v[i] - b.v[i])
SALVO_M256D_BINOP (_mm256_mul_pd, a.v[i] * b.v[i])
SALVO_M256D_BINOP (_mm256_div_pd, a.v[i] / b.v[i])
#undef SALVO_M256D_BINOP

#endif /* SALVO_AVXINTRIN_H */
