/* cephyr-libc <stdlib.h> pseudo-random sequence — C11 7.22.2.1.
 *
 * A linear congruential generator with the constants from the C standard's
 * own example, so a Cephyr-built program and a host build agree. */
#include <stdlib.h>

static unsigned long next = 1;

int rand(void) {
    next = next * 1103515245UL + 12345UL;
    return (int)((next / 65536UL) % 2147483648UL);
}

void srand(unsigned int seed) { next = seed; }

int rand_r(unsigned int *seed) {
    *seed = *seed * 1103515245U + 12345U;
    return (int)(*seed & 2147483647U);
}
