/* cephyr-libc <stddef.h> — C11 7.19, freestanding slice.
 *
 * Every definition is inside the subset Cephyr compiles, so this header is
 * usable from a Cephyr-built program as well as from a host build. */
#ifndef CEPHYR_STDDEF_H
#define CEPHYR_STDDEF_H

typedef unsigned long size_t;
typedef long ptrdiff_t;
#ifndef __cplusplus
typedef int wchar_t;
#endif
#ifndef __CEPHYR__
typedef struct { long long integer; long double floating; } max_align_t;
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* CEPHYR_STDDEF_H */
