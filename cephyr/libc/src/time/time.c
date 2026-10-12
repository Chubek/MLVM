/* salvo-libc <time.h>: clock_gettime(2) and the time(3) shorthand. */

#include <stdint.h>
#include <time.h>

#include "../unistd/syscall.h"

int
clock_gettime (clockid_t clock_id, struct timespec *ts)
{
  return (int)__salvo_syscall_ret (
      __syscall2 (__NR_clock_gettime, (long)clock_id, (long)(intptr_t)ts));
}

time_t
time (time_t *tloc)
{
  struct timespec ts;

  if (clock_gettime (CLOCK_REALTIME, &ts) != 0)
    {
      if (tloc != NULL)
        *tloc = (time_t)-1;
      return (time_t)-1;
    }
  if (tloc != NULL)
    *tloc = ts.tv_sec;
  return ts.tv_sec;
}
