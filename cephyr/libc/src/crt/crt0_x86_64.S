/* salvo-libc crt0 — x86-64 Linux ELF entry point.
 *
 * The kernel enters with rsp pointing at argc, followed by argv[0..]
 * (NULL-terminated) and envp[0..] (NULL-terminated). We decode that
 * layout, align the stack to the SysV ABI requirement, and hand off to
 * __libc_start_main, which never returns. */

    .text
    .globl  _start
    .type   _start, @function
_start:
    xor     %ebp, %ebp              /* outermost frame: no return address */
    mov     (%rsp), %rdi            /* argc */
    lea     8(%rsp), %rsi           /* argv = &rsp[1] */
    lea     16(%rsp,%rdi,8), %rdx   /* envp = &argv[argc + 1] */
    and     $-16, %rsp
    call    __libc_start_main
    hlt                             /* unreachable; keeps the decoder honest */
    .size   _start, .-_start

    .section .note.GNU-stack,"",@progbits
