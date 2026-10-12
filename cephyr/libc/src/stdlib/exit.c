/* salvo-libc: process exit, atexit chain, environment lookup. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SALVO_ATEXIT_MAX 32

char **environ;

static void (*salvo_atexit_handlers[SALVO_ATEXIT_MAX]) (void);
static int salvo_atexit_count;

int
atexit (void (*function) (void))
{
  if (function == NULL || salvo_atexit_count >= SALVO_ATEXIT_MAX)
    return -1;
  salvo_atexit_handlers[salvo_atexit_count++] = function;
  return 0;
}

void
_Exit (int status)
{
  _exit (status);
}

void
exit (int status)
{
  while (salvo_atexit_count > 0)
    salvo_atexit_handlers[--salvo_atexit_count]();
  _exit (status);
}

void
abort (void)
{
  /* 128 + SIGABRT, the conventional shell-visible abort status. */
  _exit (134);
}

char *
getenv (const char *name)
{
  size_t len;

  if (name == NULL || environ == NULL)
    return NULL;
  len = strlen (name);
  for (char **entry = environ; *entry != NULL; ++entry)
    {
      if (strncmp (*entry, name, len) == 0 && (*entry)[len] == '=')
        return *entry + len + 1;
    }
  return NULL;
}
