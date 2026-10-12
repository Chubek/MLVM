/* salvo-libc: tokenizing. */

#include <string.h>

char *
strtok_r (char *s, const char *delim, char **saveptr)
{
  char *token;

  if (s == NULL)
    s = *saveptr;
  s += strspn (s, delim);
  if (*s == '\0')
    {
      *saveptr = s;
      return NULL;
    }
  token = s;
  s = strpbrk (token, delim);
  if (s == NULL)
    {
      *saveptr = token + strlen (token);
    }
  else
    {
      *s = '\0';
      *saveptr = s + 1;
    }
  return token;
}

char *
strtok (char *s, const char *delim)
{
  static char *saved;
  return strtok_r (s, delim, &saved);
}
