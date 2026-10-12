/* salvo-libc <stdio.h>: stream objects over file descriptors.
 *
 * v0.1 is unbuffered: every fgetc/fwrite reaches read(2)/write(2)
 * directly. The FILE layout is private so buffering can arrive without
 * an ABI break. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include "../unistd/syscall.h"

struct __salvo_FILE
{
  int fd;
  int eof;
  int err;
  int has_pushback;
  unsigned char pushback;
};

static FILE salvo_stdin_obj = { 0, 0, 0, 0, 0 };
static FILE salvo_stdout_obj = { 1, 0, 0, 0, 0 };
static FILE salvo_stderr_obj = { 2, 0, 0, 0, 0 };

FILE *stdin = &salvo_stdin_obj;
FILE *stdout = &salvo_stdout_obj;
FILE *stderr = &salvo_stderr_obj;

static int
salvo_is_builtin_stream (FILE *stream)
{
  return stream == stdin || stream == stdout || stream == stderr;
}

FILE *
fopen (const char *path, const char *mode)
{
  int flags;
  int plus;
  int fd;
  FILE *stream;

  if (path == NULL || mode == NULL || mode[0] == '\0')
    {
      errno = EINVAL;
      return NULL;
    }
  plus = (strchr (mode, '+') != NULL);
  switch (mode[0])
    {
    case 'r':
      flags = plus ? O_RDWR : O_RDONLY;
      break;
    case 'w':
      flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
      break;
    case 'a':
      flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
      break;
    default:
      errno = EINVAL;
      return NULL;
    }
  fd = open (path, flags, 0666);
  if (fd < 0)
    return NULL;
  stream = (FILE *)malloc (sizeof (*stream));
  if (stream == NULL)
    {
      close (fd);
      errno = ENOMEM;
      return NULL;
    }
  stream->fd = fd;
  stream->eof = 0;
  stream->err = 0;
  stream->has_pushback = 0;
  stream->pushback = 0;
  return stream;
}

int
fclose (FILE *stream)
{
  int rc;

  if (stream == NULL)
    return EOF;
  rc = close (stream->fd);
  if (salvo_is_builtin_stream (stream))
    {
      stream->fd = -1;
      stream->eof = 0;
      stream->err = 0;
      stream->has_pushback = 0;
    }
  else
    {
      free (stream);
    }
  return rc == 0 ? 0 : EOF;
}

int
fflush (FILE *stream)
{
  /* Unbuffered: nothing to drain. NULL flushes "all streams", which is
   * likewise a no-op here. */
  (void)stream;
  return 0;
}

size_t
fread (void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  size_t total;
  size_t done = 0;
  unsigned char *out = (unsigned char *)ptr;

  if (size == 0 || nmemb == 0)
    return 0;
  if (ptr == NULL || stream == NULL)
    return 0;
  if (nmemb > (size_t)-1 / size)
    {
      stream->err = 1;
      return 0;
    }
  total = size * nmemb;
  while (done < total)
    {
      ssize_t n = read (stream->fd, out + done, total - done);
      if (n < 0)
        {
          if (errno == EINTR)
            continue;
          stream->err = 1;
          break;
        }
      if (n == 0)
        {
          stream->eof = 1;
          break;
        }
      done += (size_t)n;
    }
  return done / size;
}

size_t
fwrite (const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
  size_t total;
  size_t done = 0;
  const unsigned char *in = (const unsigned char *)ptr;

  if (size == 0 || nmemb == 0)
    return 0;
  if (ptr == NULL || stream == NULL)
    return 0;
  if (nmemb > (size_t)-1 / size)
    {
      stream->err = 1;
      return 0;
    }
  total = size * nmemb;
  while (done < total)
    {
      ssize_t n = write (stream->fd, in + done, total - done);
      if (n < 0)
        {
          if (errno == EINTR)
            continue;
          stream->err = 1;
          break;
        }
      done += (size_t)n;
    }
  return done / size;
}

int
fseek (FILE *stream, long offset, int whence)
{
  if (stream == NULL)
    return -1;
  if (lseek (stream->fd, (off_t)offset, whence) == (off_t)-1)
    {
      stream->err = 1;
      return -1;
    }
  stream->eof = 0;
  return 0;
}

long
ftell (FILE *stream)
{
  off_t pos;

  if (stream == NULL)
    return -1;
  pos = lseek (stream->fd, 0, SEEK_CUR);
  if (pos == (off_t)-1)
    {
      stream->err = 1;
      return -1;
    }
  return (long)pos;
}

int
feof (FILE *stream)
{
  return stream != NULL && stream->eof;
}

int
ferror (FILE *stream)
{
  return stream != NULL && stream->err;
}

int
fgetc (FILE *stream)
{
  unsigned char c;
  if (stream == NULL)
    return EOF;
  if (stream->has_pushback) { stream->has_pushback=0; return (int)stream->pushback; }
  for (;;)
    {
      ssize_t n = read (stream->fd, &c, 1);
      if (n == 1)
        return (int)c;
      if (n == 0)
        {
          stream->eof = 1;
          return EOF;
        }
      if (errno == EINTR)
        continue;
      stream->err = 1;
      return EOF;
    }
}

int
fputc (int c, FILE *stream)
{
  unsigned char byte = (unsigned char)c;

  if (stream == NULL)
    return EOF;
  for (;;)
    {
      ssize_t n = write (stream->fd, &byte, 1);
      if (n == 1)
        return (int)byte;
      if (errno == EINTR)
        continue;
      stream->err = 1;
      return EOF;
    }
}

char *
fgets (char *s, int size, FILE *stream)
{
  int i = 0;

  if (s == NULL || stream == NULL || size <= 0)
    return NULL;
  while (i + 1 < size)
    {
      int c = fgetc (stream);
      if (c == EOF)
        break;
      s[i++] = (char)c;
      if (c == '\n')
        break;
    }
  if (i == 0)
    return NULL;
  s[i] = '\0';
  return s;
}

int
fputs (const char *s, FILE *stream)
{
  size_t len;

  if (s == NULL || stream == NULL)
    return EOF;
  len = strlen (s);
  if (fwrite (s, 1, len, stream) != len)
    return EOF;
  return 0;
}

int
puts (const char *s)
{
  if (s == NULL)
    return EOF;
  if (fputs (s, stdout) == EOF)
    return EOF;
  if (fputc ('\n', stdout) == EOF)
    return EOF;
  return 0;
}

int
putchar (int c)
{
  return fputc (c, stdout);
}
int
getc (FILE *stream)
{
  return fgetc (stream);
}

int
getchar (void)
{
  return fgetc (stdin);
}

int
putc (int c, FILE *stream)
{
  return fputc (c, stream);
}

int
ungetc (int c, FILE *stream)
{
  if (c == EOF || stream == NULL || stream->has_pushback)
    return EOF;
  stream->has_pushback = 1;
  stream->pushback = (unsigned char)c;
  stream->eof = 0;
  return c;
}

void
clearerr (FILE *stream)
{
  if (stream) { stream->eof = 0; stream->err = 0; }
}

int
fileno (FILE *stream)
{
  if (!stream) { errno = EINVAL; return -1; }
  return stream->fd;
}

void
rewind (FILE *stream)
{
  if (stream) { fseek(stream,0,SEEK_SET); stream->err=0; stream->eof=0; stream->has_pushback=0; }
}

int
fgetpos (FILE *stream, long *pos)
{
  long p = ftell(stream);
  if (p==-1) return -1;
  *pos=p; return 0;
}
int
fsetpos (FILE *stream, const long *pos)
{
  return fseek(stream,*pos,SEEK_SET);
}

void
setbuf (FILE *stream, char *buf)
{
  (void)stream; (void)buf;
}
int
setvbuf (FILE *stream, char *buf, int mode, size_t size)
{
  (void)stream; (void)buf; (void)mode; (void)size; return 0;
}

FILE *
freopen (const char *path, const char *mode, FILE *stream)
{
  if (!stream) return fopen(path,mode);
  int nfd = -1;
  if (path) {
    FILE *n = fopen(path,mode);
    if (!n) return NULL;
    nfd = n->fd;
    free(n);
    if (stream->fd >=0) close(stream->fd);
    stream->fd = nfd;
    stream->eof=0; stream->err=0; stream->has_pushback=0;
    return stream;
  }
  return stream;
}

int
remove (const char *pathname)
{
  extern int unlink(const char *p);
  return unlink(pathname);
}
int
rename (const char *oldpath, const char *newpath)
{
#if defined(__x86_64__)
  long r = __syscall4(264, AT_FDCWD, (long)(intptr_t)oldpath, AT_FDCWD, (long)(intptr_t)newpath);
#else
  long r = __syscall4(38, AT_FDCWD, (long)(intptr_t)oldpath, AT_FDCWD, (long)(intptr_t)newpath);
#endif
  return (int)__salvo_syscall_ret(r);
}
FILE *
tmpfile (void)
{
  char tmpl[] = "/tmp/salvo-XXXXXX";
  // simple mkstemp via open with O_CREAT|O_EXCL ; fallback to tmpnam-like
  for(int i=0;i<100;i++){
    tmpl[12]='0'+(i/10)%10; tmpl[13]='0'+i%10;
    int fd=open(tmpl, O_RDWR|O_CREAT|O_EXCL, 0600);
    if(fd>=0){
      FILE *f=malloc(sizeof(FILE));
      if(!f){ close(fd); return NULL; }
      f->fd=fd; f->eof=0; f->err=0; f->has_pushback=0;
      // unlink so it is anonymous
      unlink(tmpl);
      return f;
    }
    if(errno!=EEXIST) break;
  }
  return NULL;
}
char *
tmpnam (char *s)
{
  static char buf[L_tmpnam];
  if(!s) s=buf;
  // simple: /tmp/salvoXXXXXX
  strcpy(s, "/tmp/salvoXXXXXX");
  return s;
}
void
perror (const char *s)
{
  const char *e = strerror(errno);
  if(s && *s){ write(STDERR_FILENO,s,strlen(s)); write(STDERR_FILENO,": ",2); }
  write(STDERR_FILENO,e,strlen(e)); write(STDERR_FILENO,"\n",1);
}
int
dprintf (int fd, const char *fmt, ...)
{
  va_list ap; va_start(ap,fmt);
  int r=vdprintf(fd,fmt,ap);
  va_end(ap); return r;
}
int
vdprintf (int fd, const char *fmt, va_list ap)
{
  FILE tmp; tmp.fd=fd; tmp.eof=0; tmp.err=0; tmp.has_pushback=0;
  int n = vfprintf(&tmp, fmt, ap);
  return tmp.err ? -1 : n;
}
