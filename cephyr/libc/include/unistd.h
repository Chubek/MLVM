/* salvo-libc <unistd.h> — POSIX system call wrappers (Linux). */

#ifndef SALVO_UNISTD_H
#define SALVO_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

  extern char **environ;

  ssize_t read (int fd, void *buf, size_t count);
  ssize_t write (int fd, const void *buf, size_t count);
  int close (int fd);
  off_t lseek (int fd, off_t offset, int whence);
  int unlink (const char *path);
  int unlinkat (int dirfd, const char *path, int flags);
  int access (const char *path, int mode);
  int isatty (int fd);
  int dup (int oldfd);
  int dup2 (int oldfd, int newfd);

  void _exit (int status);
  pid_t getpid (void);

  int brk (void *addr);
  void *sbrk (ptrdiff_t increment);

#ifdef __cplusplus
}
#endif

#endif /* SALVO_UNISTD_H */
