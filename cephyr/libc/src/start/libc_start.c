/* salvo-libc: C runtime start shim.
 *
 * crt0 hands control here with the kernel's initial stack decoded into
 * argc/argv/envp. Responsibilities for v0.1 are intentionally minimal:
 * publish environ, invoke main, forward its status to exit(3) so atexit
 * handlers run. */

#include <stdlib.h>

extern int main (int argc, char **argv, char **envp);

void
__libc_start_main (long argc, char **argv, char **envp)
{
  environ = envp;
  exit (main ((int)argc, argv, envp));
}
