/* cephyr-libc <string.h> — byte strings and memory. */
#ifndef CEPHYR_STRING_H
#define CEPHYR_STRING_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memchr(const void *block, int value, size_t count);

size_t strlen(const char *text);
size_t strnlen(const char *text, size_t bound);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t bound);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t bound);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t bound);
char *strchr(const char *text, int value);
char *strrchr(const char *text, int value);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strpbrk(const char *text, const char *accept);
char *strtok(char *text, const char *delimiters);
char *strtok_r(char *text, const char *delimiters, char **save);
char *strdup(const char *text);
char *strndup(const char *text, size_t count);
char *strerror(int error);
int strerror_r(int error, char *buffer, size_t size);
size_t strlcpy(char *destination, const char *source, size_t size);
size_t strlcat(char *destination, const char *source, size_t size);
int strcoll(const char *left, const char *right);
size_t strxfrm(char *destination, const char *source, size_t size);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_STRING_H */
