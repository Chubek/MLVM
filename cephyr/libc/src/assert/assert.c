/* cephyr-libc <assert.h> — C11 7.2. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void cephyr_assert_failed(const char *expression, const char *file, unsigned long line) {
    char digits[24];
    unsigned long position = 24;
    do {
        --position;
        digits[position] = (char)('0' + line % 10UL);
        line = line / 10UL;
    } while (line != 0);
    write(STDERR_FILENO, file, strlen(file));
    write(STDERR_FILENO, ":", 1);
    write(STDERR_FILENO, digits + position, 24UL - position);
    write(STDERR_FILENO, ": assertion failed: ", 20);
    write(STDERR_FILENO, expression, strlen(expression));
    write(STDERR_FILENO, "\n", 1);
    abort();
}
