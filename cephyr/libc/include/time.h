/* salvo-libc <time.h> — wall clock via clock_gettime(2). */

#ifndef SALVO_TIME_H
#define SALVO_TIME_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef int clockid_t;

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3

  struct timespec
  {
    time_t tv_sec;
    long tv_nsec;
  };

  struct timeval
  {
    time_t tv_sec;
    long tv_usec;
  };

  struct tm
  {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
  };

  time_t time (time_t *tloc);
  int clock_gettime (clockid_t clock_id, struct timespec *ts);
  int nanosleep (const struct timespec *req, struct timespec *rem);
  int gettimeofday (struct timeval *tv, void *tz);
  struct tm *gmtime (const time_t *timer);
  struct tm *gmtime_r (const time_t *timer, struct tm *result);
  struct tm *localtime (const time_t *timer);
  struct tm *localtime_r (const time_t *timer, struct tm *result);
  time_t mktime (struct tm *tm);
  size_t strftime (char *s, size_t max, const char *format, const struct tm *tm);
  char *asctime (const struct tm *tm);
  char *asctime_r (const struct tm *tm, char *buf);
  char *ctime (const time_t *timer);
  double difftime (time_t t1, time_t t0);
  clock_t clock (void);

#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000000L
#endif
  typedef long clock_t;

#ifdef __cplusplus
}
#endif

#endif /* SALVO_TIME_H */
