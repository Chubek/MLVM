/* salvo-libc crt0 — AArch64 Linux ELF entry point.
 *
 * Same initial-stack contract as x86-64: [sp] is argc, argv and envp
 * follow. x29/x30 are zeroed to terminate the frame chain. */

    .text
    .globl  _start
    .type   _start, @function
_start:
    mov     x29, xzr
    mov     x30, xzr
    mov     x3, sp                  /* preserve the kernel's stack pointer */
    and     sp, x3, #-16            /* AAPCS64 stack alignment */
    ldr     x0, [x3]                /* argc */
    add     x1, x3, #8              /* argv */
    add     x2, x1, x0, lsl #3      /* &argv[argc] */
    add     x2, x2, #8              /* envp */
    bl      __libc_start_main
    brk     #0                      /* unreachable */
    .size   _start, .-_start

    .section .note.GNU-stack,"",@progbits
