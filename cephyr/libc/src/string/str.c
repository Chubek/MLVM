/* cephyr-libc <string.h> string routines — C11 7.24.1. */
#include <string.h>
#include <stdint.h>

size_t strlen(const char *text) {
    const char *cursor = text;
    while (*cursor != '\0') ++cursor;
    return (size_t)(cursor - text);
}

size_t strnlen(const char *text, size_t bound) {
    size_t length = 0;
    while (length < bound && text[length] != '\0') ++length;
    return length;
}

char *strcpy(char *destination, const char *source) {
    char *to = destination;
    while (*source != '\0') {
        *to = *source;
        ++to;
        ++source;
    }
    *to = '\0';
    return destination;
}

char *strncpy(char *destination, const char *source, size_t bound) {
    size_t index = 0;
    while (index < bound && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    while (index < bound) {
        destination[index] = '\0';
        ++index;
    }
    return destination;
}

char *strcat(char *destination, const char *source) {
    strcpy(destination + strlen(destination), source);
    return destination;
}

char *strncat(char *destination, const char *source, size_t bound) {
    char *to = destination + strlen(destination);
    size_t index = 0;
    while (index < bound && source[index] != '\0') {
        to[index] = source[index];
        ++index;
    }
    to[index] = '\0';
    return destination;
}

int strcmp(const char *left, const char *right) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return (int)*a - (int)*b;
}

int strncmp(const char *left, const char *right, size_t bound) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    size_t index = 0;
    while (index < bound) {
        if (a[index] != b[index]) return (int)a[index] - (int)b[index];
        if (a[index] == '\0') return 0;
        ++index;
    }
    return 0;
}

char *strchr(const char *text, int value) {
    const char needle = (char)value;
    while (*text != '\0') {
        if (*text == needle) return (char *)text;
        ++text;
    }
    return needle == '\0' ? (char *)text : NULL;
}

char *strrchr(const char *text, int value) {
    const char needle = (char)value;
    const char *found = NULL;
    while (*text != '\0') {
        if (*text == needle) found = text;
        ++text;
    }
    if (needle == '\0') return (char *)text;
    return (char *)found;
}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char *)haystack;
    while (*haystack != '\0') {
        const char *a = haystack;
        const char *b = needle;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*b == '\0') return (char *)haystack;
        ++haystack;
    }
    return NULL;
}

size_t strspn(const char *text, const char *accept) {
    size_t length = 0;
    while (text[length] != '\0' && strchr(accept, text[length]) != NULL) ++length;
    return length;
}

size_t strcspn(const char *text, const char *reject) {
    size_t length = 0;
    while (text[length] != '\0' && strchr(reject, text[length]) == NULL) ++length;
    return length;
}

char *strpbrk(const char *text, const char *accept) {
    while (*text != '\0') {
        if (strchr(accept, *text) != NULL) return (char *)text;
        ++text;
    }
    return NULL;
}

/* The runtime currently supports the C locale. */
int strcoll(const char *left, const char *right) { return strcmp(left, right); }
size_t strxfrm(char *destination, const char *source, size_t size) {
    size_t length = strlen(source);
    if (length < size) memcpy(destination, source, length + 1);
    return length;
}
