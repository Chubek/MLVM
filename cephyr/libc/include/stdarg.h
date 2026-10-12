/* salvo-libc <stdarg.h> — variable argument lists.
 *
 * Variadic support is delegated to the host compiler's builtins; Cephyr's
 * own variadic lowering is staged, so for now the GCC/Clang builtin
 * spellings are required (§DECISIONS 2026-08-19, salvo-libc boundary). */

#ifndef SALVO_STDARG_H
#define SALVO_STDARG_H

#if defined(__GNUC__) || defined(__clang__)
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start (ap, last)
#define va_arg(ap, type) __builtin_va_arg (ap, type)
#define va_end(ap) __builtin_va_end (ap)
#define va_copy(dst, src) __builtin_va_copy (dst, src)
#else
#error "salvo-libc <stdarg.h> requires compiler va_list builtins"
#endif

#endif /* SALVO_STDARG_H */
