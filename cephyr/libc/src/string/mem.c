/* cephyr-libc <string.h> memory routines — C11 7.24.2.
 *
 * Written in the subset Cephyr compiles so that cephyr-libc can build its own
 * objects with Cephyr (see tests/bootstrap.c). */
#include <string.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    size_t index = 0;
    while (index < count) {
        to[index] = from[index];
        ++index;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    size_t index = 0;
    if (to == from || count == 0) return destination;
    /* Compare as addresses so that an overlapping copy is detected. */
    if ((uintptr_t)to < (uintptr_t)from) {
        while (index < count) {
            to[index] = from[index];
            ++index;
        }
        return destination;
    }
    index = count;
    while (index > 0) {
        --index;
        to[index] = from[index];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    unsigned char *to = destination;
    const unsigned char byte = (unsigned char)value;
    size_t index = 0;
    while (index < count) {
        to[index] = byte;
        ++index;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    size_t index = 0;
    while (index < count) {
        if (a[index] != b[index]) return (int)a[index] - (int)b[index];
        ++index;
    }
    return 0;
}

void *memchr(const void *block, int value, size_t count) {
    const unsigned char *bytes = block;
    const unsigned char byte = (unsigned char)value;
    size_t index = 0;
    while (index < count) {
        if (bytes[index] == byte) return (void *)(bytes + index);
        ++index;
    }
    return NULL;
}
