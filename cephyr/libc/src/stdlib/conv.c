/* C11 integer conversion, LP64: retain end pointers even after overflow. */
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

static int digit_value(int value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'z') return value - 'a' + 10;
    if (value >= 'A' && value <= 'Z') return value - 'A' + 10;
    return -1;
}

/* mode: signed/unsigned. Six scalar arguments stay within Cephyr's ABI. */
static unsigned long convert(const char *text, char **end, int base,
                             int mode, int *negative, int *overflow) {
    const char *cursor = text;
    unsigned long value = 0;
    int any = 0;
    *negative = 0;
    *overflow = 0;
    if (end != NULL) *end = (char *)text;
    if (base != 0 && (base < 2 || base > 36)) { errno = EINVAL; return 0; }
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor == '+' || *cursor == '-') {
        *negative = *cursor == '-';
        ++cursor;
    }
    if ((base == 0 || base == 16) && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        digit_value(cursor[2]) >= 0 && digit_value(cursor[2]) < 16) {
        cursor += 2;
        base = 16;
    }
    if (base == 0) base = cursor[0] == '0' ? 8 : 10;
    unsigned long limit = ULONG_MAX;
    if (mode == 0) limit = (unsigned long)LONG_MAX + (unsigned long)*negative;
    unsigned long cutoff = limit / (unsigned long)base;
    unsigned long cutlim = limit % (unsigned long)base;
    for (;;) {
        int digit = digit_value((unsigned char)*cursor);
        if (digit < 0 || digit >= base) break;
        any = 1;
        if (value > cutoff || (value == cutoff && (unsigned long)digit > cutlim)) *overflow = 1;
        if (!*overflow) value = value * (unsigned long)base + (unsigned long)digit;
        ++cursor;
    }
    if (any && end != NULL) *end = (char *)cursor;
    if (*overflow) { errno = ERANGE; return limit; }
    return value;
}

long strtol(const char *text, char **end, int base) {
    int negative = 0;
    int overflow = 0;
    unsigned long value = convert(text, end, base, 0, &negative, &overflow);
    if (negative) {
        if (value == (unsigned long)LONG_MAX + 1UL) return LONG_MIN;
        return -(long)value;
    }
    return (long)value;
}

unsigned long strtoul(const char *text, char **end, int base) {
    int negative = 0;
    int overflow = 0;
    unsigned long value = convert(text, end, base, 1, &negative, &overflow);
    if (overflow) return ULONG_MAX;
    return negative ? 0UL - value : value;
}

long long strtoll(const char *text, char **end, int base) { return strtol(text, end, base); }
unsigned long long strtoull(const char *text, char **end, int base) { return strtoul(text, end, base); }
int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
long atol(const char *text) { return strtol(text, NULL, 10); }
long long atoll(const char *text) { return strtoll(text, NULL, 10); }
