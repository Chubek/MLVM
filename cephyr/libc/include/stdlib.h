/* cephyr-libc <stdlib.h> — LP64 runtime. Unsupported frontend declarations
 * remain available to host compilers that build the full runtime. */
#ifndef CEPHYR_STDLIB_H
#define CEPHYR_STDLIB_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
#define SALVO_MALLOC_MAX ((size_t)1 << 40)

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);

int abs(int value);
long labs(long value);
long long llabs(long long value);
int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);
unsigned long long strtoull(const char *text, char **end, int base);
long long atoll(const char *text);
int rand(void);
void srand(unsigned int seed);
int rand_r(unsigned int *seed);
void exit(int status);
void _Exit(int status);
void abort(void);
char *getenv(const char *name);
int system(const char *command);
extern char **environ;

#ifndef __CEPHYR__
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);
double atof(const char *text);
double strtod(const char *text, char **end);
float strtof(const char *text, char **end);
long double strtold(const char *text, char **end);
int atexit(void (*function)(void));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
#endif

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_STDLIB_H */
