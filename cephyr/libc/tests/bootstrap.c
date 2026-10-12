/* cephyr-libc self-test — every function here is compiled by Cephyr itself.
 *
 * The program returns the first failed check through the exit status so
 * the harness needs no I/O. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

static int failures;

static void check(int condition, int id) {
    if (!condition && failures == 0) failures = id;
    (void)id;
}

static char *mirror(char *destination, const char *source) {
    memcpy(destination, source, strlen(source) + 1);
    return destination;
}

int cephyr_selftest(void) {
    char buffer[32];
    char overlapping[16];

    check(strlen("cephyr") == 6, 1);
    check(strlen("") == 0, 2);
    mirror(buffer, "hello");
    check(strcmp(buffer, "hello") == 0, 3);
    strcat(buffer, " world");
    check(strcmp(buffer, "hello world") == 0, 4);
    check(strlen(buffer) == 11, 5);
    check(strncmp(buffer, "hello", 5) == 0, 6);
    check(strncmp(buffer, "hellp", 5) != 0, 7);

    strcpy(overlapping, "abcde");
    memmove(overlapping + 1, overlapping, 6);
    check(strcmp(overlapping, "aabcde") == 0, 8);

    memset(buffer, 'x', 4);
    check(buffer[0] == 'x' && buffer[3] == 'x' && buffer[4] == 'o', 9);

    check(memcmp("abc", "abc", 3) == 0, 10);
    check(memcmp("abc", "abd", 3) < 0, 11);
    check(memchr(buffer, 'x', 4) == buffer, 12);
    check(memchr("hello", 'z', 5) == NULL, 13);

    check(strchr("hello", 'l') != NULL, 14);
    check(strchr("hello", 'z') == NULL, 15);
    check(strchr("hello", '\0')[0] == '\0', 16);
    check(strrchr("hello", 'l')[1] == 'o', 17);
    check(strstr("hello world", "wor") != NULL, 18);
    check(strstr("hello", "zz") == NULL, 19);
    check(strspn("abcde", "abc") == 3, 20);
    check(strcspn("abcde", "de") == 3, 21);
    check(strnlen("abcdef", 3) == 3, 22);

    check(isalpha('a') && isalpha('Z') && !isalpha('1'), 23);
    check(isdigit('7') && !isdigit('x'), 24);
    check(isspace(' ') && isspace('\t') && isspace('\n'), 25);
    check(ispunct('!') && !ispunct('a') && !ispunct(' '), 26);
    check(isxdigit('f') && isxdigit('F') && !isxdigit('g'), 27);
    check(toupper('a') == 'A' && tolower('Z') == 'z', 28);
    check(toupper('1') == '1', 29);

    check(abs(-7) == 7 && abs(7) == 7, 30);
    check(labs(-9L) == 9L, 31);
    check(atoi("1234") == 1234 && atoi("-42") == -42, 32);
    check(atol("100000") == 100000L, 33);

    check(strtol("ff", NULL, 16) == 255L, 34);
    check(strtol("0x10", NULL, 0) == 16L, 35);
    check(strtol("0777", NULL, 0) == 511L, 36);
    check(strtoul("4000000000", NULL, 10) == 4000000000UL, 37);
    {
        char *stop = NULL;
        const long value = strtol("42abc", &stop, 10);
        check(value == 42L && *stop == 'a', 38);
    }
    check(strtol("99999999999999999999", NULL, 10) == LONG_MAX && errno == ERANGE, 39);

    srand(1);
    {
        const int first = rand();
        srand(1);
        check(rand() == first, 40);
        check(first >= 0 && first <= RAND_MAX, 41);
    }

    check(sizeof(int) == 4 && sizeof(long) == 8 && sizeof(void *) == 8, 42);
    check((size_t)-1 == SIZE_MAX, 43);
    check(INT_MAX == 2147483647 && LONG_MAX == 9223372036854775807L, 44);
    check(NULL == (void *)0, 45);
    {
        bool flag = true;
        check(flag && !false, 46);
    }

    check(failures == 0, 47);
    {
        char *stop;
        char empty[4] = " +";
        errno = 0;
        check(strtoul("18446744073709551615!", &stop, 10) == ULONG_MAX && *stop == '!' && errno == 0, 48);
        check(strtoul("18446744073709551616!", &stop, 10) == ULONG_MAX && *stop == '!' && errno == ERANGE, 49);
        errno = 0;
        check(strtoul("-1", &stop, 10) == ULONG_MAX && errno == 0, 50);
        check(strtol("-9223372036854775808", &stop, 10) == LONG_MIN && errno == 0, 51);
        check(strtol("-9223372036854775809x", &stop, 10) == LONG_MIN && *stop == 'x' && errno == ERANGE, 52);
        errno = 0;
        check(strtol(empty, &stop, 0) == 0 && stop == empty && errno == 0, 53);
        check(strtol("0x", &stop, 0) == 0 && *stop == 'x', 54);
        check(strtol("1", &stop, 1) == 0 && *stop == '1' && errno == EINVAL, 55);
        check(strtoll("-9223372036854775808", NULL, 10) == LLONG_MIN, 56);
        check(strtoull("ffffffffffffffff", NULL, 16) == ULLONG_MAX, 57);
    }
    strcpy(buffer, ",a,,b,");
    check(strcmp(strtok(buffer, ","), "a") == 0, 58);
    check(strcmp(strtok(NULL, ","), "b") == 0, 59);
    check(strtok(NULL, ",") == NULL, 60);
    check(strcasecmp("aBc", "AbC") == 0 && strncasecmp("aBz", "Abx", 2) == 0, 61);
    check(strcoll("abc", "abd") < 0 && strxfrm(buffer, "abc", 32) == 3 && strcmp(buffer, "abc") == 0, 62);
    check(imaxabs(-123) == 123 && strtoumax("18446744073709551615", NULL, 10) == UINTMAX_MAX, 63);
    return failures;

}
