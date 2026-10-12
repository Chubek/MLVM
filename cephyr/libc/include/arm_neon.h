/* salvo-libc <arm_neon.h> — portable NEON-compatible intrinsic subset. */
#ifndef SALVO_ARM_NEON_H
#define SALVO_ARM_NEON_H

#include <stdint.h>

typedef struct
{
  float v[4];
} float32x4_t;
typedef struct
{
  int32_t v[4];
} int32x4_t;

static inline float32x4_t
vdupq_n_f32 (float x)
{
  float32x4_t r = { { x, x, x, x } };
  return r;
}
static inline int32x4_t
vdupq_n_s32 (int32_t x)
{
  int32x4_t r = { { x, x, x, x } };
  return r;
}
static inline float32x4_t
vld1q_f32 (const float *p)
{
  float32x4_t r = { { p[0], p[1], p[2], p[3] } };
  return r;
}
static inline int32x4_t
vld1q_s32 (const int32_t *p)
{
  int32x4_t r = { { p[0], p[1], p[2], p[3] } };
  return r;
}
static inline void
vst1q_f32 (float *p, float32x4_t a)
{
  p[0] = a.v[0];
  p[1] = a.v[1];
  p[2] = a.v[2];
  p[3] = a.v[3];
}
static inline void
vst1q_s32 (int32_t *p, int32x4_t a)
{
  p[0] = a.v[0];
  p[1] = a.v[1];
  p[2] = a.v[2];
  p[3] = a.v[3];
}

#define SALVO_NEON_F32_BINOP(name, expr)                                      \
  static inline float32x4_t name (float32x4_t a, float32x4_t b)               \
  {                                                                           \
    float32x4_t r;                                                            \
    for (int i = 0; i < 4; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_NEON_F32_BINOP (vaddq_f32, a.v[i] + b.v[i])
SALVO_NEON_F32_BINOP (vsubq_f32, a.v[i] - b.v[i])
SALVO_NEON_F32_BINOP (vmulq_f32, a.v[i] * b.v[i])
#undef SALVO_NEON_F32_BINOP

#define SALVO_NEON_I32_BINOP(name, expr)                                      \
  static inline int32x4_t name (int32x4_t a, int32x4_t b)                     \
  {                                                                           \
    int32x4_t r;                                                              \
    for (int i = 0; i < 4; ++i)                                               \
      r.v[i] = (expr);                                                        \
    return r;                                                                 \
  }
SALVO_NEON_I32_BINOP (vaddq_s32, a.v[i] + b.v[i])
SALVO_NEON_I32_BINOP (vsubq_s32, a.v[i] - b.v[i])
SALVO_NEON_I32_BINOP (vmulq_s32, a.v[i] * b.v[i])
#undef SALVO_NEON_I32_BINOP

#endif /* SALVO_ARM_NEON_H */
