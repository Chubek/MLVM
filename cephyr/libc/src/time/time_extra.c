#include <stdio.h>
#include <time.h>
#include <string.h>
#include "../unistd/syscall.h"
#include <stdint.h>
int nanosleep(const struct timespec *req, struct timespec *rem){
#if defined(__x86_64__)
 return (int)__salvo_syscall_ret(__syscall2(35,(long)(intptr_t)req,(long)(intptr_t)rem));
#else
 return (int)__salvo_syscall_ret(__syscall2(101,(long)(intptr_t)req,(long)(intptr_t)rem));
#endif
}
int gettimeofday(struct timeval *tv, void *tz){ if(tz) {} if(!tv) return 0; struct timespec ts; if(clock_gettime(CLOCK_REALTIME,&ts)!=0) return -1; tv->tv_sec=ts.tv_sec; tv->tv_usec=ts.tv_nsec/1000; return 0;}
static struct tm salvo_tm_buf;
static int is_leap(int y){ return (y%4==0 && y%100!=0) || (y%400==0); }
struct tm *gmtime_r(const time_t *t, struct tm *tm){ if(!t||!tm) return NULL; long sec=*t; int days= sec/86400; int rem= sec%86400; if(rem<0){rem+=86400; days--;} tm->tm_hour=rem/3600; tm->tm_min=(rem%3600)/60; tm->tm_sec=rem%60; int y=1970; while(1){ int yd=is_leap(y)?366:365; if(days>=yd){days-=yd; y++;} else break; } tm->tm_year=y-1900; static int mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31}; if(is_leap(y)) mdays[1]=29; int m=0; while(m<12 && days>=mdays[m]){days-=mdays[m]; m++;} tm->tm_mon=m; tm->tm_mday=days+1; tm->tm_wday=(4 + (int)(*t/86400))%7; if(tm->tm_wday<0) tm->tm_wday+=7; tm->tm_yday=0; for(int i=0;i<m;i++) tm->tm_yday+=mdays[i]; tm->tm_yday+=days; tm->tm_isdst=0; return tm; }
struct tm *gmtime(const time_t *t){ return gmtime_r(t,&salvo_tm_buf); }
struct tm *localtime_r(const time_t *t, struct tm *tm){ return gmtime_r(t,tm); }
struct tm *localtime(const time_t *t){ return gmtime_r(t,&salvo_tm_buf); }
time_t mktime(struct tm *tm){ if(!tm) return -1; int y=tm->tm_year+1900; int m=tm->tm_mon; int d=tm->tm_mday; long days=0; for(int yy=1970; yy<y; yy++) days+= is_leap(yy)?366:365; static int md[12]={31,28,31,30,31,30,31,31,30,31,30,31}; if(is_leap(y)) md[1]=29; for(int i=0;i<m;i++) days+=md[i]; days+=d-1; return days*86400 + tm->tm_hour*3600 + tm->tm_min*60 + tm->tm_sec; }
double difftime(time_t t1, time_t t0){ return (double)(t1-t0); }
clock_t clock(void){ struct timespec ts; clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts); return (clock_t)(ts.tv_sec*1000000 + ts.tv_nsec/1000); }
size_t strftime(char *s,size_t max,const char *fmt,const struct tm *tm){ if(!s||!fmt||!tm||max==0) return 0; size_t o=0; for(const char *p=fmt;*p && o+1<max;p++){ if(*p!='%'){ s[o++]=*p; continue;} p++; char buf[32]; const char *src=""; int n=0; switch(*p){case 'Y': n=snprintf(buf,sizeof(buf),"%d",tm->tm_year+1900); src=buf; break; case 'm': n=snprintf(buf,sizeof(buf),"%02d",tm->tm_mon+1); src=buf; break; case 'd': n=snprintf(buf,sizeof(buf),"%02d",tm->tm_mday); src=buf; break; case 'H': n=snprintf(buf,sizeof(buf),"%02d",tm->tm_hour); src=buf; break; case 'M': n=snprintf(buf,sizeof(buf),"%02d",tm->tm_min); src=buf; break; case 'S': n=snprintf(buf,sizeof(buf),"%02d",tm->tm_sec); src=buf; break; case 'j': n=snprintf(buf,sizeof(buf),"%03d",tm->tm_yday+1); src=buf; break; case '%': s[o++]='%'; continue; default: s[o++]='%'; if(o<max) s[o++]=*p; continue; } for(int i=0;i<n && o+1<max;i++) s[o++]=src[i]; } s[o]='\0'; return o; }
char *asctime_r(const struct tm *tm,char *buf){ static const char *wd[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}; static const char *mo[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; snprintf(buf,26,"%s %s %2d %02d:%02d:%02d %d\n",wd[tm->tm_wday%7],mo[tm->tm_mon%12],tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec,tm->tm_year+1900); return buf; }
char *asctime(const struct tm *tm){ static char b[32]; return asctime_r(tm,b); }
char *ctime(const time_t *t){ struct tm *tm=localtime(t); return asctime(tm); }
