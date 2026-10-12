#include "internal.h"
#include "compiler.h"
#include "bincode.h"
#include <stdlib.h>
#include <string.h>

typedef struct { FILE *file; size_t bytes; BSStatus status; } Codec;

static void bytes(Codec *c, void *data, size_t size, int writing) {
    if (c->status != BS_OK) return;
    if (size > BS_PROFILE_MAX_BYTES - c->bytes) { c->status = BS_INVALID; return; }
    size_t done = writing ? fwrite(data, 1, size, c->file) : fread(data, 1, size, c->file);
    if (done != size) c->status = writing || ferror(c->file) ? BS_IO_ERROR : BS_INVALID;
    c->bytes += done;
}

static uint32_t number(Codec *c, uint32_t value, int writing) {
    unsigned char data[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                             (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    bytes(c, data, 4, writing);
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void put_string(Codec *c, const char *s) {
    if (!s) s = "";
    size_t size = strlen(s);
    if (size > BS_PROFILE_MAX_BYTES) { c->status = BS_INVALID; return; }
    number(c, (uint32_t)size, 1); bytes(c, (void *)s, size, 1);
}

static char *get_string(Codec *c) {
    uint32_t size = number(c, 0, 0);
    if (c->status != BS_OK) return NULL;
    if (size > BS_PROFILE_MAX_BYTES - c->bytes) { c->status = BS_INVALID; return NULL; }
    char *s = malloc((size_t)size + 1);
    if (!s) { c->status = BS_NOMEM; return NULL; }
    bytes(c, s, size, 0); s[size] = 0;
    if (c->status != BS_OK || memchr(s, 0, size)) { free(s); if (c->status == BS_OK) c->status = BS_INVALID; return NULL; }
    return s;
}

static void put_span(Codec *c, BSSpan span) {
    put_string(c, span.path); number(c, span.line, 1); number(c, span.column, 1);
}
static BSSpan get_span(Codec *c) {
    BSSpan span = {0}; span.path = get_string(c);
    span.line = number(c, 0, 0); span.column = number(c, 0, 0);
    return span;
}
static size_t get_count(Codec *c) {
    uint32_t n = number(c, 0, 0);
    if (n > BS_MAX_RULE_NODES) { c->status = BS_INVALID; return 0; }
    return n;
}

BSStatus bs_profile_write(const BSProfile *p, FILE *file, BSError *error) {
    if (error) *error = (BSError){0};
    if (!p || !file) return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid profile output");
    Codec c = {file, 0, BS_OK};
    bytes(&c, (void *)BS_PROFILE_MAGIC, BS_PROFILE_MAGIC_SIZE, 1);
    number(&c, (uint32_t)p->count, 1);
    for (size_t i = 0; i < p->count && c.status == BS_OK; ++i) {
        const BSCompiledRule *compiled = &p->rules[i]; const BSRule *r = &compiled->rule;
        number(&c, (uint32_t)compiled->folding, 1); put_string(&c, r->name); put_span(&c, r->span);
        if (compiled->folding) continue;
        number(&c, r->result, 1); number(&c, (uint32_t)r->match_count, 1);
        for (size_t j = 0; j < r->match_count; ++j) {
            const BSMatchNode *n = &r->match[j];
            number(&c, n->sort, 1); number(&c, n->refinement, 1); number(&c, n->capture, 1);
            number(&c, n->same, 1); number(&c, n->left, 1); number(&c, n->right, 1);
            put_string(&c, n->text); put_span(&c, n->span);
        }
        number(&c, (uint32_t)r->build_count, 1);
        for (size_t j = 0; j < r->build_count; ++j) {
            const BSBuildNode *n = &r->build[j];
            number(&c, n->kind, 1); number(&c, n->capture, 1); number(&c, n->left, 1); number(&c, n->right, 1);
            put_string(&c, n->text); put_span(&c, n->span);
        }
        number(&c, (uint32_t)r->guard_count, 1);
        for (size_t j = 0; j < r->guard_count; ++j) {
            const BSGuard *g = &r->guards[j];
            put_string(&c, g->name); put_string(&c, compiled->predicates[j]->name);
            number(&c, (uint32_t)g->count, 1);
            for (size_t k = 0; k < g->count; ++k) number(&c, g->arguments[k], 1);
            put_span(&c, g->span);
        }
    }
    if (c.status == BS_OK && fflush(file)) c.status = BS_IO_ERROR;
    if (c.status != BS_OK) return bs_error(error, c.status, (BSSpan){0}, "cannot write profile (v1 size limit is %u bytes)", BS_PROFILE_MAX_BYTES);
    return BS_OK;
}

static void get_rule(Codec *c, BSCompiledRule *temp) {
    BSRule *r = &temp->rule;
    temp->folding = (int)number(c, 0, 0); r->name = get_string(c); r->span = get_span(c);
    if (temp->folding) {
        if (temp->folding != 1 || !r->name || strcmp(r->name, "ConstantFolding")) c->status = BS_INVALID;
        return;
    }
    r->result = (BSSort)number(c, 0, 0); r->match_count = get_count(c);
    BSMatchNode *match = calloc(r->match_count, sizeof(*match)); r->match = match;
    if (r->match_count && !match) { c->status = BS_NOMEM; return; }
    for (size_t i = 0; i < r->match_count && c->status == BS_OK; ++i) {
        BSMatchNode *n = &match[i];
        n->sort = (BSSort)number(c, 0, 0); n->refinement = (BSRefinement)number(c, 0, 0);
        n->capture = number(c, 0, 0); n->same = number(c, 0, 0); n->left = number(c, 0, 0); n->right = number(c, 0, 0);
        n->text = get_string(c); n->span = get_span(c);
    }
    r->build_count = get_count(c);
    BSBuildNode *build = calloc(r->build_count, sizeof(*build)); r->build = build;
    if (r->build_count && !build) { c->status = BS_NOMEM; return; }
    for (size_t i = 0; i < r->build_count && c->status == BS_OK; ++i) {
        BSBuildNode *n = &build[i];
        n->kind = (BSBuildKind)number(c, 0, 0); n->capture = number(c, 0, 0);
        n->left = number(c, 0, 0); n->right = number(c, 0, 0); n->text = get_string(c); n->span = get_span(c);
    }
    r->guard_count = get_count(c);
    BSGuard *guards = calloc(r->guard_count, sizeof(*guards)); r->guards = guards;
    temp->predicates = calloc(r->guard_count, sizeof(*temp->predicates));
    if (r->guard_count && (!guards || !temp->predicates)) { c->status = BS_NOMEM; return; }
    for (size_t i = 0; i < r->guard_count && c->status == BS_OK; ++i) {
        BSGuard *g = &guards[i]; g->name = get_string(c);
        BSCapability *cap = calloc(1, sizeof(*cap)); temp->predicates[i] = cap;
        if (!cap) { c->status = BS_NOMEM; return; }
        cap->name = get_string(c); g->count = get_count(c);
        uint32_t *args = calloc(g->count, sizeof(*args)); g->arguments = args;
        if (g->count && !args) { c->status = BS_NOMEM; return; }
        for (size_t j = 0; j < g->count && c->status == BS_OK; ++j) args[j] = number(c, 0, 0);
        g->span = get_span(c);
    }
}

BSStatus bs_profile_read(const BSRegistry *registry, FILE *file, BSProfile **out, BSError *error) {
    if (out) *out = NULL;
    if (error) *error = (BSError){0};
    if (!registry || !file || !out) return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid profile input");
    Codec c = {file, 0, BS_OK}; char magic[BS_PROFILE_MAGIC_SIZE];
    bytes(&c, magic, sizeof(magic), 0);
    if (c.status != BS_OK || memcmp(magic, BS_PROFILE_MAGIC, sizeof(magic)))
        return bs_error(error, BS_INVALID, (BSSpan){0}, "invalid or unsupported profile header");
    size_t count = get_count(&c);
    BSProfile *p = calloc(1, sizeof(*p));
    if (!p) return bs_error(error, BS_NOMEM, (BSSpan){0}, "cannot allocate profile");
    p->registry = registry; p->rules = calloc(count, sizeof(*p->rules));
    if (count && !p->rules) { bs_profile_destroy(p); return bs_error(error, BS_NOMEM, (BSSpan){0}, "cannot allocate profile rules"); }
    BSStatus status = c.status;
    for (size_t i = 0; i < count && status == BS_OK; ++i) {
        BSCompiledRule temp = {0}; get_rule(&c, &temp);
        status = c.status;
        if (status == BS_OK) for (size_t j = 0; j < p->count; ++j)
            if (!strcmp(temp.rule.name, p->rules[j].rule.name)) { status = bs_error(error, BS_INVALID, temp.rule.span, "duplicate rule name %s", temp.rule.name); break; }
        if (status == BS_OK && temp.folding) {
            p->rules[p->count++] = temp; temp = (BSCompiledRule){0};
        } else if (status == BS_OK) {
            BSDeclaration *concerns = calloc(registry->view_count, sizeof(*concerns));
            BSDeclaration *requires = calloc(temp.rule.guard_count, sizeof(*requires));
            if ((registry->view_count && !concerns) || (temp.rule.guard_count && !requires)) status = BS_NOMEM;
            else {
                for (size_t j = 0; j < registry->view_count; ++j) concerns[j].name = registry->views[j].name;
                for (size_t j = 0; j < temp.rule.guard_count; ++j) requires[j].name = temp.predicates[j]->name;
                BSKernel kernel = {0}; kernel.concerns = concerns; kernel.concern_count = registry->view_count;
                kernel.requires = requires; kernel.require_count = temp.rule.guard_count;
                status = bs_rule_compile(registry, &kernel, &temp.rule, &p->rules[p->count], error);
                if (status == BS_OK) ++p->count;
            }
            free(concerns); free(requires);
        }
        bs_rule_dispose(&temp);
    }
    if (status == BS_OK && fgetc(file) != EOF) status = BS_INVALID;
    if (status == BS_OK && ferror(file)) status = BS_IO_ERROR;
    if (status != BS_OK) {
        bs_profile_destroy(p);
        if (c.status != BS_OK || !error || error->code != status)
            bs_error(error, status, (BSSpan){0}, "malformed, truncated, or oversized profile");
        return status;
    }
    *out = p; if (error) *error = (BSError){0};
    return BS_OK;
}
