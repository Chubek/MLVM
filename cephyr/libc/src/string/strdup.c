/* salvo-libc: string duplication. */

#include <stdlib.h>
#include <string.h>

char *
strdup (const char *s)
{
  size_t n = strlen (s) + 1;
  char *copy = (char *)malloc (n);
  if (copy != NULL)
    memcpy (copy, s, n);
  return copy;
}

char *
strndup (const char *s, size_t n)
{
  size_t len = strnlen (s, n);
  char *copy = (char *)malloc (len + 1);
  if (copy == NULL)
    return NULL;
  memcpy (copy, s, len);
  copy[len] = '\0';
  return copy;
}
