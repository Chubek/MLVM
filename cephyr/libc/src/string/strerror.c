#include <string.h>
#include <errno.h>
struct salvo_err { int code; const char *msg; };
static const struct salvo_err table[] = {
 {1,"Operation not permitted"}, {2,"No such file or directory"}, {3,"No such process"},
 {4,"Interrupted system call"}, {5,"Input/output error"}, {12,"Cannot allocate memory"},
 {13,"Permission denied"}, {17,"File exists"}, {22,"Invalid argument"}, {28,"No space left on device"},
 {33,"Numerical argument out of domain"}, {34,"Numerical result out of range"}, {75,"Value too large for defined data type"},
 {38,"Function not implemented"}, {84,"Invalid or incomplete multibyte or wide character"}, {95,"Operation not supported"},
 {0,NULL}
};
char *strerror(int errnum){
    for(int i=0; table[i].msg;i++) if(table[i].code==errnum) return (char*)table[i].msg;
    return (char*)"Unknown error";
}
int strerror_r(int errnum, char *buf, size_t buflen){
    const char *m=strerror(errnum);
    size_t n=strlen(m);
    if(buflen==0) return 34;
    if(n >= buflen){ memcpy(buf,m,buflen-1); buf[buflen-1]='\0'; return 34; }
    memcpy(buf,m,n+1); return 0;
}
size_t strlcpy(char *dst, const char *src, size_t size){
    size_t sl=strlen(src);
    if(size!=0){ size_t n= sl < size-1 ? sl : size-1; memcpy(dst,src,n); dst[n]='\0'; }
    return sl;
}
size_t strlcat(char *dst, const char *src, size_t size){
    size_t dl=strnlen(dst,size);
    size_t sl=strlen(src);
    if(dl < size){ size_t avail=size-dl-1; size_t n= sl < avail ? sl : avail; memcpy(dst+dl,src,n); dst[dl+n]='\0'; }
    return dl+sl;
}
