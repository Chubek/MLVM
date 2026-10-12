#include "internal.h"
#include "lexer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t bs_abi_version(void) { return BS_ABI_VERSION; }

BSStatus bs_error(BSError *error, BSStatus code, BSSpan span, const char *format, ...) {
    if (error) {
        size_t offset = 0;
        error->code = code;
        if (span.path && *span.path)
            offset = (size_t)snprintf(error->message, sizeof(error->message), "%s:%u:%u: ",
                                      span.path, span.line, span.column);
        if (offset >= sizeof(error->message)) offset = sizeof(error->message) - 1;
        va_list args;
        va_start(args, format);
        vsnprintf(error->message + offset, sizeof(error->message) - offset, format, args);
        va_end(args);
    }
    return code;
}

char *bs_string(const char *source) {
    if (!source) source = "";
    size_t size = strlen(source) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, source, size);
    return copy;
}

int bs_identifier_valid(const char *s) {
    if (!s || !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_')) return 0;
    for (++s; *s; ++s)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') ||
              (*s >= '0' && *s <= '9') || *s == '_')) return 0;
    return 1;
}

int bs_decimal_valid(const char *s) {
    if (!s) return 0;
    if (*s == '-') ++s;
    if (!*s) return 0;
    for (; *s; ++s) if (*s < '0' || *s > '9') return 0;
    return 1;
}

int bs_qualified_valid(const char *s) {
    if (!s) return 0;
    for (;;) {
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_')) return 0;
        do { ++s; } while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') ||
                          (*s >= '0' && *s <= '9') || *s == '_');
        if (!*s) return 1;
        if (s[0] != ':' || s[1] != ':') return 0;
        s += 2;
    }
}

int bs_decimal_equal(const char *a, const char *b) {
    if (!bs_decimal_valid(a) || !bs_decimal_valid(b)) return 0;
    int an = *a == '-', bn = *b == '-';
    a += an; b += bn;
    while (*a == '0' && a[1]) ++a;
    while (*b == '0' && b[1]) ++b;
    return strcmp(a, b) == 0 && (an == bn || strcmp(a, "0") == 0);
}

BSRegistry *bs_registry_create(BSError *error) {
    static const BSConcern views[] = {
        {"HIR::Literals::Term", BS_TERM}, {"HIR::Expressions::Binary", BS_BINARY}
    };
    static const BSOperator operators[] = {
        {"plus", "integer", 1, 1}, {"minus", "integer", 0, 0},
        {"times", "integer", 1, 1}, {"divide", "integer", 0, 0},
        {"bit_and", "integer", 0, 0}, {"bit_or", "integer", 0, 0}, {"bit_xor", "integer", 0, 0}
    };
    static const BSCapability capabilities[] = {
        {"Relational::Properties::Associativity", "Associative", 0, ""},
        {"Relational::Properties::Commutativity", "Commutative", 0, ""},
        {"Arithmetic::Domains::ModularInteger", "ModularInteger", 0, ""},
        {"HIR::Effects::Pure", "Pure", 1, ""}, {"HIR::Effects::Total", "Total", 1, ""}
    };
    BSConcernModule concern = {"HIR", views, 2, operators, 7};
    BSRequiresModule requires = {"Core", capabilities, 5};
    BSRegistry *registry = calloc(1, sizeof(*registry));
    if (!registry) { bs_error(error, BS_NOMEM, (BSSpan){0}, "cannot allocate registry"); return NULL; }
    if (bs_concern_load(registry, &concern, error) != BS_OK ||
        bs_requires_load(registry, &requires, error) != BS_OK ||
        bs_concern_load(registry, bs_loop_descriptor_module(), error) != BS_OK) {
        bs_registry_destroy(registry);
        return NULL;
    }
    if (error) *error = (BSError){0};
    return registry;
}

void bs_registry_destroy(BSRegistry *registry) {
    if (!registry) return;
    bs_concern_truncate(registry, 0, 0);
    bs_requires_truncate(registry, 0);
    free(registry->views); free(registry->operators); free(registry->capabilities);
    free(registry);
}
