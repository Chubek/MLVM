/* salvo-libc <stdio.h> — unbuffered v0.1 streams over file descriptors.
 *
 * Streams are thin wrappers around read(2)/write(2); there is no user-space
 * buffering yet, so fflush is a successful no-op. Formatting supports the
 * integer/string/pointer conversions and basic floating conversions (%f/%F/%e/%E/%g/%G/%a via dtoa). */

#ifndef SALVO_STDIO_H
#define SALVO_STDIO_H

#ifndef __CEPHYR__
#include <stdarg.h>
#endif
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define EOF (-1)
#define BUFSIZ 8192
#define FOPEN_MAX 16
#define FILENAME_MAX 4096
#define L_tmpnam 20

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifdef __CEPHYR__
  /* FILE is opaque; Cephyr currently passes stream pointers only. */
  typedef void FILE;
#else
  typedef struct __salvo_FILE FILE;
#endif

  extern FILE *stdin;
  extern FILE *stdout;
  extern FILE *stderr;

  FILE *fopen (const char *path, const char *mode);
  FILE *freopen (const char *path, const char *mode, FILE *stream);
  int fclose (FILE *stream);
  int fflush (FILE *stream);
  void setbuf (FILE *stream, char *buf);
  int setvbuf (FILE *stream, char *buf, int mode, size_t size);

  size_t fread (void *ptr, size_t size, size_t nmemb, FILE *stream);
  size_t fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream);

  int fseek (FILE *stream, long offset, int whence);
  long ftell (FILE *stream);
  void rewind (FILE *stream);
  int fgetpos (FILE *stream, long *pos);
  int fsetpos (FILE *stream, const long *pos);
  int feof (FILE *stream);
  int ferror (FILE *stream);
  void clearerr (FILE *stream);
  int fileno (FILE *stream);

  int fgetc (FILE *stream);
  int getc (FILE *stream);
  int getchar (void);
  char *fgets (char *s, int size, FILE *stream);
  int fputs (const char *s, FILE *stream);
  int puts (const char *s);
  int putchar (int c);
  int fputc (int c, FILE *stream);
  int putc (int c, FILE *stream);
  int ungetc (int c, FILE *stream);
  int getchar (void);
  int getc (FILE *stream);

  int remove (const char *pathname);
  int rename (const char *oldpath, const char *newpath);
  FILE *tmpfile (void);
  char *tmpnam (char *s);
  void perror (const char *s);

#ifndef __CEPHYR__
  int printf (const char *fmt, ...);
  int fprintf (FILE *stream, const char *fmt, ...);
  int sprintf (char *str, const char *fmt, ...);
  int snprintf (char *str, size_t size, const char *fmt, ...);
  int vprintf (const char *fmt, va_list ap);
  int vfprintf (FILE *stream, const char *fmt, va_list ap);
  int vsprintf (char *str, const char *fmt, va_list ap);
  int vsnprintf (char *str, size_t size, const char *fmt, va_list ap);
  int dprintf (int fd, const char *fmt, ...);
  int vdprintf (int fd, const char *fmt, va_list ap);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SALVO_STDIO_H */
