#include <inttypes.h>
#include <stdlib.h>

intmax_t imaxabs(intmax_t value) { return labs(value); }
intmax_t strtoimax(const char *text, char **end, int base) { return strtol(text, end, base); }
uintmax_t strtoumax(const char *text, char **end, int base) { return strtoul(text, end, base); }
