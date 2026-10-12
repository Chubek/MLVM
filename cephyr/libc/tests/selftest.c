/* salvo-libc freestanding self-test.
 *
 * Runs without any host libc: crt0 enters __libc_start_main, which calls
 * this main. Exit status is 0 on success; a failing check exits with its
 * own small code so the ctest output identifies the area. */

#include <arm_neon.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int
int_cmp (const void *a, const void *b)
{
  int x = *(const int *)a;
  int y = *(const int *)b;
  return (x > y) - (x < y);
}

int
main (void)
{
  char buf[128];
  char *end;
  long v;
  int sorted[] = { 9, -3, 5, 0, 42, -17, 5 };
  char *dup;

  /* string */
  if (strlen ("salvo") != 5)
    return 1;
  if (strcmp ("abc", "abd") >= 0)
    return 2;
  if (strncmp ("abc", "abd", 2) != 0)
    return 3;
  if (strchr ("hello", 'l') != NULL && *strchr ("hello", 'l') != 'l')
    return 4;
  if (strstr ("ccweave", "weave") == NULL)
    return 5;
  memcpy (buf, "0123456789", 11);
  if (memmove (buf + 2, buf, 9) != buf + 2)
    return 6;
  if (memcmp (buf, "0101234567", 10) != 0)
    return 7;
  memset (buf, 'x', 4);
  if (buf[0] != 'x' || buf[3] != 'x')
    return 8;
  {
    char tok[32];
    char *t;
    strcpy (tok, "a,b,,c");
    t = strtok (tok, ",");
    if (t == NULL || strcmp (t, "a") != 0)
      return 9;
    t = strtok (NULL, ",");
    if (t == NULL || strcmp (t, "b") != 0)
      return 10;
    t = strtok (NULL, ",");
    if (t == NULL || strcmp (t, "c") != 0)
      return 11;
    if (strtok (NULL, ",") != NULL)
      return 12;
  }

  /* ctype */
  if (!isdigit ('7') || isdigit ('x'))
    return 13;
  if (tolower ('Q') != 'q' || toupper ('q') != 'Q')
    return 14;
  if (!isspace ('\n') || !isxdigit ('F'))
    return 15;
  if (strcasecmp ("WeAvE", "weave") != 0)
    return 16;

  /* stdlib conversion */
  errno = 0;
  v = strtol ("  -42rest", &end, 10);
  if (v != -42 || *end != 'r')
    return 17;
  v = strtol ("0x1F", &end, 0);
  if (v != 31 || *end != '\0')
    return 18;
  v = strtol ("9999999999999999999999999", &end, 10);
  if (v != LONG_MAX || errno != ERANGE)
    return 19;
  if (strtoul ("077", NULL, 0) != 63)
    return 20;
  if (atoi ("-7") != -7)
    return 21;

  /* qsort */
  qsort (sorted, sizeof (sorted) / sizeof (sorted[0]), sizeof (int), int_cmp);
  for (size_t i = 1; i < sizeof (sorted) / sizeof (sorted[0]); ++i)
    if (sorted[i - 1] > sorted[i])
      return 22;

  /* malloc */
  dup = strdup ("arena");
  if (dup == NULL || strcmp (dup, "arena") != 0)
    return 23;
  {
    char *grown = realloc (dup, 64);
    if (grown == NULL || strcmp (grown, "arena") != 0)
      return 24;
    dup = grown;
  }
  free (dup);
  {
    int *zeroed = calloc (8, sizeof (int));
    if (zeroed == NULL)
      return 25;
    for (int i = 0; i < 8; ++i)
      if (zeroed[i] != 0)
        return 26;
    free (zeroed);
  }

  /* stdio formatting (memory side; no fd output needed to validate) */
  {
    int n = snprintf (buf, sizeof (buf), "%d|%05u|%x|%s|%c|%%", -42, 37u,
                      0xBEEFu, "ok", 'z');
    if (n < 0 || strcmp (buf, "-42|00037|beef|ok|z|%") != 0)
      return 27;
  }
  {
    int n = snprintf (buf, sizeof (buf), "%8.3s|%-5d|%+d|%#x", "truncate", 7,
                      7, 255u);
    if (n < 0 || strcmp (buf, "     tru|7    |+7|0xff") != 0)
      return 28;
  }
  {
    int n = snprintf (buf, 8, "0123456789abcdef");
    if (n != 16 || strlen (buf) != 7)
      return 29;
  }

  /* stdio over a real fd: /dev/null accepts writes */
  {
    FILE *devnull = fopen ("/dev/null", "w");
    if (devnull == NULL)
      return 30;
    if (fprintf (devnull, "discard %d\n", 1) < 0)
      return 31;
    if (fclose (devnull) != 0)
      return 32;
  }

  /* portable SIMD intrinsic surface */
  {
    float a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 }, out[4];
    __m128 x = _mm_add_ps (_mm_loadu_ps (a), _mm_loadu_ps (b));
    _mm_storeu_ps (out, x);
    if (out[0] != 6.0f || out[3] != 12.0f)
      return 33;
    {
      float32x4_t n = vaddq_f32 (vld1q_f32 (a), vdupq_n_f32 (1.0f));
      vst1q_f32 (out, n);
      if (out[0] != 2.0f || out[3] != 5.0f)
        return 34;
    }
  }

  return 0;
}
