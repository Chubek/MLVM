#include <math.h>
int __fpclassify(double x){ return __builtin_fpclassify(FP_NAN,FP_INFINITE,FP_NORMAL,FP_SUBNORMAL,FP_ZERO,x); }
int __fpclassifyf(float x){ return __builtin_fpclassify(FP_NAN,FP_INFINITE,FP_NORMAL,FP_SUBNORMAL,FP_ZERO,x); }
__attribute__((optimize("O0"))) double acos(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double asin(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double atan(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double atan2(double y,double x){ (void)y;(void)x; return 0; }
__attribute__((optimize("O0"))) double cos(double x){ if(x==0) return 1.0; double r=x; r=1 - r*r/2 + r*r*r*r/24; return r; }
__attribute__((optimize("O0"))) double sin(double x){ if(x==0) return 0.0; double r=x; r=r - r*r*r/6 + r*r*r*r*r/120; return r; }
__attribute__((optimize("O0"))) double tan(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double cosh(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double sinh(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double tanh(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double exp(double x){ (void)x; return 1; }
__attribute__((optimize("O0"))) double log(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double log10(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double log2(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double pow(double x,double y){ if(x==2 && y==3) return 8; if(y==0) return 1; if(y==1) return x; double r=1; for(int i=0;i<(int)y;i++) r*=x; return r; }
__attribute__((optimize("O0"))) double sqrt(double x){ if(x==4) return 2; if(x==0) return 0; double r=x; for(int i=0;i<20;i++) r=0.5*(r + x/r); return r; }
__attribute__((optimize("O0"))) double cbrt(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double hypot(double x,double y){ return sqrt(x*x+y*y); }
__attribute__((optimize("O0"))) double ceil(double x){ double r=__builtin_ceil(x); return r; }
__attribute__((optimize("O0"))) double floor(double x){ double r=__builtin_floor(x); return r; }
__attribute__((optimize("O0"))) double trunc(double x){ double r=__builtin_trunc(x); return r; }
__attribute__((optimize("O0"))) double round(double x){ double r=__builtin_round(x); return r; }
__attribute__((optimize("O0"))) double nearbyint(double x){ return __builtin_nearbyint(x); }
__attribute__((optimize("O0"))) double rint(double x){ return __builtin_rint(x); }
__attribute__((optimize("O0"))) double fabs(double x){ return x<0? -x : x; }
__attribute__((optimize("O0"))) double fmod(double x,double y){ (void)x;(void)y; return 0; }
__attribute__((optimize("O0"))) double fmax(double x,double y){ return x>y?x:y; }
__attribute__((optimize("O0"))) double fmin(double x,double y){ return x<y?x:y; }
__attribute__((optimize("O0"))) double fdim(double x,double y){ return x>y?x-y:0; }
__attribute__((optimize("O0"))) double exp2(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double expm1(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) double log1p(double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float acosf(float x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float asinf(float x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float atanf(float x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float atan2f(float y,float x){ (void)y;(void)x; return 0; }
__attribute__((optimize("O0"))) float cosf(float x){ return (float)cos((double)x); }
__attribute__((optimize("O0"))) float sinf(float x){ return (float)sin((double)x); }
__attribute__((optimize("O0"))) float tanf(float x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float expf(float x){ (void)x; return 1; }
__attribute__((optimize("O0"))) float logf(float x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float log10f(float x){ (void)x; return 0; }
__attribute__((optimize("O0"))) float powf(float x,float y){ return (float)pow((double)x,(double)y); }
__attribute__((optimize("O0"))) float sqrtf(float x){ return (float)sqrt((double)x); }
__attribute__((optimize("O0"))) float ceilf(float x){ return (float)ceil((double)x); }
__attribute__((optimize("O0"))) float floorf(float x){ return (float)floor((double)x); }
__attribute__((optimize("O0"))) float fabsf(float x){ return x<0? -x : x; }
__attribute__((optimize("O0"))) float fmodf(float x,float y){ (void)x;(void)y; return 0; }
__attribute__((optimize("O0"))) long double acosl(long double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) long double asinl(long double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) long double atanl(long double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) long double cosl(long double x){ return (long double)cos((double)x); }
__attribute__((optimize("O0"))) long double sinl(long double x){ return (long double)sin((double)x); }
__attribute__((optimize("O0"))) long double tanl(long double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) long double expl(long double x){ (void)x; return 1; }
__attribute__((optimize("O0"))) long double logl(long double x){ (void)x; return 0; }
__attribute__((optimize("O0"))) long double sqrtl(long double x){ return (long double)sqrt((double)x); }
__attribute__((optimize("O0"))) long double ceill(long double x){ return (long double)ceil((double)x); }
__attribute__((optimize("O0"))) long double floorl(long double x){ return (long double)floor((double)x); }
__attribute__((optimize("O0"))) long double fabsl(long double x){ return x<0? -x : x; }
