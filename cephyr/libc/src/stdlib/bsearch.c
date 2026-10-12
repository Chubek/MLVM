/* salvo-libc: bsearch */
#include <stdlib.h>
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    size_t lo = 0, hi = nmemb;
    const unsigned char *b = (const unsigned char *)base;
    if (!key || !base || !compar || size==0) return NULL;
    while (lo < hi) {
        size_t mid = lo + (hi - lo)/2;
        const void *p = b + mid*size;
        int r = compar(key, p);
        if (r==0) return (void*)p;
        if (r < 0) hi = mid;
        else lo = mid+1;
    }
    return NULL;
}
