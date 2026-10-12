/* salvo-libc crt0 — RISC-V 64 Linux ELF entry point.
 *
 * Same initial-stack contract as the other ports. sp is realigned to the
 * ABI's 16-byte boundary before the call. */

    .text
    .globl  _start
    .type   _start, @function
_start:
    ld      a0, 0(sp)               /* argc */
    addi    a1, sp, 8               /* argv */
    slli    t0, a0, 3
    add     a2, a1, t0              /* &argv[argc] */
    addi    a2, a2, 8               /* envp */
    and     sp, sp, -16
    call    __libc_start_main
    unimp                           /* unreachable */
    .size   _start, .-_start

    .section .note.GNU-stack,"",@progbits
