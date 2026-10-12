/* salvo-libc <emmintrin.h> — portable SSE2 integer intrinsic subset. */
#ifndef SALVO_EMMINTRIN_H
#define SALVO_EMMINTRIN_H

#include "xmmintrin.h"

typedef struct
{
  int32_t v[4];
} __m128i;

static inline __m128i
_mm_setzero_si128 (void)
{
  __m128i r = { { 0, 0, 0, 0 } };
  return r;
}
static inline __m128i
_mm_set1_epi32 (int32_t x)
{
  __m128i r = { { x, x, x, x } };
  return r;
}
static inline __m128i
_mm_set_epi32 (int e3, int e2, int e1, int e0)
{
  __m128i r = { { e0, e1, e2, e3 } };
  return r;
}
static inline __m128i
_mm_loadu_si128 (const __m128i *p)
{
  return *p;
}
static inline __m128i
_mm_load_si128 (const __m128i *p)
{
  return *p;
}
static inline void
_mm_storeu_si128 (__m128i *p, __m128i a)
{
  *p = a;
}
static inline void
_mm_store_si128 (__m128i *p, __m128i a)
{
  *p = a;
}

#define SALVO_M128I_BINOP(name, expr)                                         \
  static inline __m128i name (__m128i a, __m128i b)                           \
  {                                                                           \
    __m128i r;                                                                \
    for (int i = 0; i < 4; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_M128I_BINOP (_mm_add_epi32, a.v[i] + b.v[i])
SALVO_M128I_BINOP (_mm_sub_epi32, a.v[i] - b.v[i])
SALVO_M128I_BINOP (_mm_mullo_epi32, a.v[i] * b.v[i])
SALVO_M128I_BINOP (_mm_and_si128, a.v[i] & b.v[i])
SALVO_M128I_BINOP (_mm_or_si128, a.v[i] | b.v[i])
SALVO_M128I_BINOP (_mm_xor_si128, a.v[i] ^ b.v[i])
#undef SALVO_M128I_BINOP

static inline __m128i
_mm_andnot_si128 (__m128i a, __m128i b)
{
  __m128i r;
  for (int i = 0; i < 4; ++i)
    r.v[i] = (~a.v[i]) & b.v[i];
  return r;
}
static inline __m128i
_mm_slli_epi32 (__m128i a, int imm)
{
  __m128i r;
  for (int i = 0; i < 4; ++i)
    r.v[i] = (int32_t)((uint32_t)a.v[i] << imm);
  return r;
}
static inline __m128i
_mm_srli_epi32 (__m128i a, int imm)
{
  __m128i r;
  for (int i = 0; i < 4; ++i)
    r.v[i] = (int32_t)((uint32_t)a.v[i] >> imm);
  return r;
}

#endif /* SALVO_EMMINTRIN_H */
