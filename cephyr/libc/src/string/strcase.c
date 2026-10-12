/* salvo-libc: case-insensitive comparison (<strings.h>). */

#include <ctype.h>
#include <strings.h>

int
strcasecmp (const char *a, const char *b)
{
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  for (;; ++pa, ++pb)
    {
      int ca = tolower (*pa);
      int cb = tolower (*pb);
      if (ca != cb)
        return ca - cb;
      if (ca == 0)
        return 0;
    }
}

int
strncasecmp (const char *a, const char *b, size_t n)
{
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  while (n != 0)
    {
      int ca = tolower (*pa);
      int cb = tolower (*pb);
      if (ca != cb)
        return ca - cb;
      if (ca == 0)
        return 0;
      ++pa;
      ++pb;
      --n;
    }
  return 0;
}
