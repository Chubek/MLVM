/* salvo-libc: strtod/strtof/strtold/atof */
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
static double salvo_strtod_core(const char *s, char **endptr, int *any){
    const char *p=s;
    while(isspace((unsigned char)*p)) p++;
    int neg=0;
    if(*p=='+'||*p=='-'){ neg=(*p=='-'); p++; }
    // handle inf/nan case-insensitively
    if((p[0]=='i'||p[0]=='I') && (p[1]=='n'||p[1]=='N') && (p[2]=='f'||p[2]=='F')){
        if(endptr) *endptr=(char*)p+3;
        if(any) *any=1;
        double v=__builtin_huge_val();
        return neg? -v : v;
    }
    if((p[0]=='n'||p[0]=='N') && (p[1]=='a'||p[1]=='A') && (p[2]=='n'||p[2]=='N')){
        if(endptr) *endptr=(char*)p+3;
        if(any) *any=1;
        double v=__builtin_nan("");
        return v;
    }
    double intpart=0.0;
    int digits=0;
    // integer part
    while(isdigit((unsigned char)*p)){ intpart=intpart*10 + (*p-'0'); p++; digits=1; }
    if(*p=='.'){
        p++;
        double frac=0, base=0.1;
        int fdigits=0;
        while(isdigit((unsigned char)*p)){ frac += (*p-'0')*base; base*=0.1; p++; fdigits=1; digits=1; }
        intpart += frac;
        (void)fdigits;
    }
    if(!digits){
        if(endptr) *endptr=(char*)s;
        if(any) *any=0;
        return 0.0;
    }
    // exponent
    int expneg=0;
    long ex=0;
    if(*p=='e'||*p=='E'){
        const char *epos=p;
        p++;
        if(*p=='+'||*p=='-'){ expneg=(*p=='-'); p++; }
        if(!isdigit((unsigned char)*p)){ p=epos; goto done; }
        while(isdigit((unsigned char)*p)){ ex = ex*10 + (*p-'0'); p++; }
        double pow10 = 1.0;
        // compute 10^ex via fast pow
        // limit ex to avoid overflow
        if(ex>308) ex=308;
        for(long i=0;i<ex;i++) pow10*=10.0;
        if(expneg) intpart /= pow10; else intpart *= pow10;
    }
done:
    if(endptr) *endptr=(char*)p;
    if(any) *any=1;
    return neg? -intpart : intpart;
}
double strtod(const char *nptr, char **endptr){ if(!nptr){ if(endptr)*endptr=NULL; return 0;} int any=0; double v=salvo_strtod_core(nptr,endptr,&any); if(!any){ if(endptr&&nptr) *endptr=(char*)nptr; } return v; }
float strtof(const char *nptr, char **endptr){ return (float)strtod(nptr,endptr); }
long double strtold(const char *nptr, char **endptr){ return (long double)strtod(nptr,endptr); }
double atof(const char *s){ return strtod(s,NULL); }
int system(const char *cmd){ (void)cmd; errno=38; return -1; }
