/* salvo-libc: div/ldiv/lldiv */
#include <stdlib.h>
div_t div(int n, int d){ div_t r; r.quot = n/d; r.rem = n%d; return r; }
ldiv_t ldiv(long n, long d){ ldiv_t r; r.quot = n/d; r.rem = n%d; return r; }
lldiv_t lldiv(long long n, long long d){ lldiv_t r; r.quot=n/d; r.rem=n%d; return r; }
#include <inttypes.h>

imaxdiv_t imaxdiv(intmax_t numerator, intmax_t denominator) {
    imaxdiv_t result = {numerator / denominator, numerator % denominator};
    return result;
}
