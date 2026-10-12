/* cephyr-libc <ctype.h> — C11 7.4, the C locale only.
 *
 * Values are ASCII and are stable across hosts so that a program built by
 * Cephyr and one built by the host toolchain agree. */
#ifndef CEPHYR_CTYPE_H
#define CEPHYR_CTYPE_H
#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int value);
int isalpha(int value);
int isblank(int value);
int iscntrl(int value);
int isdigit(int value);
int isgraph(int value);
int islower(int value);
int isprint(int value);
int ispunct(int value);
int isspace(int value);
int isupper(int value);
int isxdigit(int value);
int tolower(int value);
int toupper(int value);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_CTYPE_H */
