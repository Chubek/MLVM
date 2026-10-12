/* salvo-libc <fcntl.h> — file control options (Linux values). */

#ifndef SALVO_FCNTL_H
#define SALVO_FCNTL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000

#define AT_FDCWD (-100)

  int open (const char *path, int flags, ...);

#ifdef __cplusplus
}
#endif

#endif /* SALVO_FCNTL_H */
