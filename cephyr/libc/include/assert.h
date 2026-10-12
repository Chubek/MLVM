/* cephyr-libc <assert.h> — reevaluate NDEBUG on every inclusion. */
#ifndef CEPHYR_ASSERT_H
#define CEPHYR_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif
void cephyr_assert_failed(const char *expression, const char *file, unsigned long line);
#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_ASSERT_H */

#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) \
    ((expression) ? (void)0 : cephyr_assert_failed(#expression, __FILE__, __LINE__))
#endif

#ifndef __cplusplus
#define static_assert _Static_assert
#endif
