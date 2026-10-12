/* cephyr-libc <errno.h> — C11 7.5, freestanding slice.
 *
 * errno is per-thread state; Cephyr has no threads, so one global is enough
 * and is what the C standard requires for a single-threaded program. */
#ifndef CEPHYR_ERRNO_H
#define CEPHYR_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif
int *cephyr_errno_location(void);
#ifdef __cplusplus
}
#endif
#define errno (*cephyr_errno_location())

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define ERANGE 34
#define EDOM 33
#define EILSEQ 84
#define EOVERFLOW 75
#define ENOSYS 38
#define ENOTSUP 95

#endif /* CEPHYR_ERRNO_H */
