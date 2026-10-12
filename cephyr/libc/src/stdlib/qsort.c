/* salvo-libc: qsort.
 *
 * Shell sort with halving gaps: deterministic, O(n^(3/2)) worst case,
 * no recursion, no auxiliary storage. A comparison-sort upgrade (with
 * introspection) is staged for a later drop; the ABI is unaffected. */

#include <stdlib.h>
#include <string.h>

static void
salvo_swap (unsigned char *a, unsigned char *b, size_t size)
{
  while (size-- != 0)
    {
      unsigned char tmp = *a;
      *a++ = *b;
      *b++ = tmp;
    }
}

void
qsort (void *base, size_t nmemb, size_t size,
       int (*compar) (const void *, const void *))
{
  unsigned char *b = (unsigned char *)base;

  if (b == NULL || compar == NULL || nmemb < 2 || size == 0)
    return;
  for (size_t gap = nmemb / 2; gap != 0; gap /= 2)
    {
      for (size_t i = gap; i < nmemb; ++i)
        {
          for (size_t j = i; j >= gap; j -= gap)
            {
              unsigned char *hi = b + j * size;
              unsigned char *lo = b + (j - gap) * size;
              if (compar (lo, hi) <= 0)
                break;
              salvo_swap (lo, hi, size);
            }
        }
    }
}
