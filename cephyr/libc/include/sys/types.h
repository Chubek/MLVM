/* salvo-libc <sys/types.h> — LP64 system types. */

#ifndef SALVO_SYS_TYPES_H
#define SALVO_SYS_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

  typedef long ssize_t;
  typedef long off_t;
  typedef unsigned int mode_t;
  typedef int pid_t;
  typedef unsigned long ino_t;
  typedef unsigned long dev_t;
  typedef unsigned long nlink_t;
  typedef unsigned int uid_t;
  typedef unsigned int gid_t;
  typedef long time_t;
  typedef long clock_t;

#ifdef __cplusplus
}
#endif

#endif /* SALVO_SYS_TYPES_H */
