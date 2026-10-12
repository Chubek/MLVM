/* salvo-libc <strings.h> — legacy/POSIX string operations. */

#ifndef SALVO_STRINGS_H
#define SALVO_STRINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  int strcasecmp (const char *a, const char *b);
  int strncasecmp (const char *a, const char *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* SALVO_STRINGS_H */
