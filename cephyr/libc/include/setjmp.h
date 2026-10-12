#ifndef SALVO_SETJMP_H
#define SALVO_SETJMP_H
typedef struct { long __regs[8]; } jmp_buf[1];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
