/* cephyr-libc <errno.h> — one process-wide error slot.
 *
 * A hosted libc keeps errno per thread; Cephyr has no threads, so a single
 * static is the conforming choice for a single-threaded program. */
#include <errno.h>

static int cephyr_errno;

int *cephyr_errno_location(void) { return &cephyr_errno; }
