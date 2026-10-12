/* salvo-libc internal: raw Linux syscall wrappers.
 *
 * One translation unit per wrapper family includes this header; the
 * functions are static inline so unused arities cost nothing. Kernel
 * error returns (-1..-4095) funnel through __salvo_syscall_ret, which
 * materializes errno and returns -1, matching the ABI contracts of the
 * public wrappers. */

#ifndef SALVO_INTERNAL_SYSCALL_H
#define SALVO_INTERNAL_SYSCALL_H

#include <errno.h>

#if defined(__x86_64__)

#define __NR_read 0
#define __NR_write 1
#define __NR_open 2
#define __NR_close 3
#define __NR_lseek 8
#define __NR_brk 12
#define __NR_getpid 39
#define __NR_exit_group 231
#define __NR_clock_gettime 228

static inline long
__syscall0 (long n)
{
  long ret;
  __asm__ volatile ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
  return ret;
}

static inline long
__syscall1 (long n, long a1)
{
  long ret;
  __asm__ volatile ("syscall"
                    : "=a"(ret)
                    : "a"(n), "D"(a1)
                    : "rcx", "r11", "memory");
  return ret;
}

static inline long
__syscall2 (long n, long a1, long a2)
{
  long ret;
  __asm__ volatile ("syscall"
                    : "=a"(ret)
                    : "a"(n), "D"(a1), "S"(a2)
                    : "rcx", "r11", "memory");
  return ret;
}

static inline long
__syscall3 (long n, long a1, long a2, long a3)
{
  long ret;
  __asm__ volatile ("syscall"
                    : "=a"(ret)
                    : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                    : "rcx", "r11", "memory");
  return ret;
}

static inline long
__syscall4 (long n, long a1, long a2, long a3, long a4)
{
  long ret;
  register long r10 __asm__ ("r10") = a4;
  __asm__ volatile ("syscall"
                    : "=a"(ret)
                    : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                    : "rcx", "r11", "memory");
  return ret;
}

#elif defined(__aarch64__)

/* asm-generic numbering: openat(56) replaces open(2). */
#define __NR_read 63
#define __NR_write 64
#define __NR_openat 56
#define __NR_close 57
#define __NR_lseek 62
#define __NR_brk 214
#define __NR_getpid 172
#define __NR_exit_group 94
#define __NR_clock_gettime 113

static inline long
__syscall0 (long n)
{
  register long x8 __asm__ ("x8") = n;
  register long x0 __asm__ ("x0");
  __asm__ volatile ("svc #0" : "=r"(x0) : "r"(x8) : "memory", "cc");
  return x0;
}

static inline long
__syscall1 (long n, long a1)
{
  register long x8 __asm__ ("x8") = n;
  register long x0 __asm__ ("x0") = a1;
  __asm__ volatile ("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");
  return x0;
}

static inline long
__syscall2 (long n, long a1, long a2)
{
  register long x8 __asm__ ("x8") = n;
  register long x0 __asm__ ("x0") = a1;
  register long x1 __asm__ ("x1") = a2;
  __asm__ volatile ("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory", "cc");
  return x0;
}

static inline long
__syscall3 (long n, long a1, long a2, long a3)
{
  register long x8 __asm__ ("x8") = n;
  register long x0 __asm__ ("x0") = a1;
  register long x1 __asm__ ("x1") = a2;
  register long x2 __asm__ ("x2") = a3;
  __asm__ volatile ("svc #0"
                    : "+r"(x0)
                    : "r"(x8), "r"(x1), "r"(x2)
                    : "memory", "cc");
  return x0;
}

static inline long
__syscall4 (long n, long a1, long a2, long a3, long a4)
{
  register long x8 __asm__ ("x8") = n;
  register long x0 __asm__ ("x0") = a1;
  register long x1 __asm__ ("x1") = a2;
  register long x2 __asm__ ("x2") = a3;
  register long x3 __asm__ ("x3") = a4;
  __asm__ volatile ("svc #0"
                    : "+r"(x0)
                    : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
                    : "memory", "cc");
  return x0;
}

#elif defined(__riscv) && (__riscv_xlen == 64)

/* asm-generic numbering, identical to aarch64. */
#define __NR_read 63
#define __NR_write 64
#define __NR_openat 56
#define __NR_close 57
#define __NR_lseek 62
#define __NR_brk 214
#define __NR_getpid 172
#define __NR_exit_group 94
#define __NR_clock_gettime 113

static inline long
__syscall0 (long n)
{
  register long a7 __asm__ ("a7") = n;
  register long a0 __asm__ ("a0");
  __asm__ volatile ("ecall" : "=r"(a0) : "r"(a7) : "memory", "cc");
  return a0;
}

static inline long
__syscall1 (long n, long a1)
{
  register long a7 __asm__ ("a7") = n;
  register long a0 __asm__ ("a0") = a1;
  __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7) : "memory", "cc");
  return a0;
}

static inline long
__syscall2 (long n, long a1, long a2)
{
  register long a7 __asm__ ("a7") = n;
  register long a0 __asm__ ("a0") = a1;
  register long a1_ __asm__ ("a1") = a2;
  __asm__ volatile ("ecall" : "+r"(a0) : "r"(a7), "r"(a1_) : "memory", "cc");
  return a0;
}

static inline long
__syscall3 (long n, long a1, long a2, long a3)
{
  register long a7 __asm__ ("a7") = n;
  register long a0 __asm__ ("a0") = a1;
  register long a1_ __asm__ ("a1") = a2;
  register long a2_ __asm__ ("a2") = a3;
  __asm__ volatile ("ecall"
                    : "+r"(a0)
                    : "r"(a7), "r"(a1_), "r"(a2_)
                    : "memory", "cc");
  return a0;
}

static inline long
__syscall4 (long n, long a1, long a2, long a3, long a4)
{
  register long a7 __asm__ ("a7") = n;
  register long a0 __asm__ ("a0") = a1;
  register long a1_ __asm__ ("a1") = a2;
  register long a2_ __asm__ ("a2") = a3;
  register long a3_ __asm__ ("a3") = a4;
  __asm__ volatile ("ecall"
                    : "+r"(a0)
                    : "r"(a7), "r"(a1_), "r"(a2_), "r"(a3_)
                    : "memory", "cc");
  return a0;
}

#else
#error "salvo-libc: unsupported host architecture"
#endif

/* Kernel error convention: return values in [-4095, -1] are -errno. */
static inline long
__salvo_syscall_ret (long ret)
{
  if ((unsigned long)ret >= (unsigned long)-4095L)
    {
      errno = (int)-ret;
      return -1;
    }
  return ret;
}

#endif /* SALVO_INTERNAL_SYSCALL_H */
