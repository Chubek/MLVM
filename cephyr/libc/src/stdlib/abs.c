/* cephyr-libc <stdlib.h> numeric helpers — C11 7.22.1. */
#include <stdlib.h>

int abs(int value) { return value < 0 ? -value : value; }

long labs(long value) { return value < 0 ? -value : value; }
long long llabs(long long value) { return value < 0 ? -value : value; }
