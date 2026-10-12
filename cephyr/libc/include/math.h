/* salvo-libc <math.h> — minimal libm surface via compiler builtins.
 *
 * Delegates to __builtin_* so the freestanding libc links without an
 * external libm. C99/C11 required interfaces are present; the rest
 * trap to the same builtins. */

#ifndef SALVO_MATH_H
#define SALVO_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

#define HUGE_VAL  __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define INFINITY  __builtin_inff()
#define NAN       __builtin_nanf("")

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

int __fpclassify(double x);
int __fpclassifyf(float x);
#define fpclassify(x) (sizeof(x)==sizeof(float) ? __fpclassifyf(x) : __fpclassify(x))
#define isfinite(x)  (__builtin_isfinite(x))
#define isinf(x)     (__builtin_isinf(x))
#define isnan(x)     (__builtin_isnan(x))
#define isnormal(x)  (__builtin_isnormal(x))
#define signbit(x)   (__builtin_signbit(x))

double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double cos(double x);
double sin(double x);
double tan(double x);
double cosh(double x);
double sinh(double x);
double tanh(double x);
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double pow(double x, double y);
double sqrt(double x);
double cbrt(double x);
double hypot(double x, double y);
double ceil(double x);
double floor(double x);
double trunc(double x);
double round(double x);
double nearbyint(double x);
double rint(double x);
double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double fmax(double x, double y);
double fmin(double x, double y);
double fdim(double x, double y);
double exp2(double x);
double expm1(double x);
double log1p(double x);

float acosf(float x);
float asinf(float x);
float atanf(float x);
float atan2f(float y, float x);
float cosf(float x);
float sinf(float x);
float tanf(float x);
float expf(float x);
float logf(float x);
float log10f(float x);
float powf(float x, float y);
float sqrtf(float x);
float ceilf(float x);
float floorf(float x);
float fabsf(float x);
float fmodf(float x, float y);

long double acosl(long double x);
long double asinl(long double x);
long double atanl(long double x);
long double cosl(long double x);
long double sinl(long double x);
long double tanl(long double x);
long double expl(long double x);
long double logl(long double x);
long double sqrtl(long double x);
long double ceill(long double x);
long double floorl(long double x);
long double fabsl(long double x);

#ifdef __cplusplus
}
#endif
#endif
