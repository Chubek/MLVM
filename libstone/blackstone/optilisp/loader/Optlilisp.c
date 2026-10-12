/* Optlilisp.c -- C implementation of the Blackstone Optilisp loader.
 *
 * Implements the API declared in Optlilisp.h:
 *   - Layer 1 reader (sexpr.py): generic S-expression parser with locations.
 *   - Kernel field extraction (semantic.py shape subset).
 *   - Layers 2/4/5 validation subset (validate.py): required fields,
 *     recognized stage, known/stage-appropriate verb, transform arity.
 *
 * C11, standard library only.
 */

#include "Optlilisp.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/* Allocation failure is fatal, rather than dereferencing NULL or losing
 * the old realloc allocation. No partial result is published on OOM. */
static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}
static void *xrealloc(void *p, size_t n)
{
    void *out = realloc(p, n ? n : 1);
    if (!out) abort();
    return out;
}

static char *xstrndup(const char *s, size_t n)
{
    char *out = (char *)xmalloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                        */
/* ------------------------------------------------------------------ */

void optlilisp_sink_init(OptlilispSink *sink)
{
    sink->items = NULL;
    sink->count = 0;
    sink->capacity = 0;
}

void optlilisp_sink_fini(OptlilispSink *sink)
{
    free(sink->items);
    sink->items = NULL;
    sink->count = 0;
    sink->capacity = 0;
}

void optlilisp_sink_emit(OptlilispSink *sink, const char *severity,
                         const char *code, const char *message,
                         const OptlilispLoc *loc)
{
    if (sink->count == sink->capacity) {
        size_t ncap = sink->capacity ? sink->capacity * 2 : 16;
        sink->items = (OptlilispDiag *)xrealloc(sink->items, ncap * sizeof(*sink->items));
        sink->capacity = ncap;
    }
    {
        OptlilispDiag *d = &sink->items[sink->count++];
        snprintf(d->severity, sizeof(d->severity), "%s", severity ? severity : "error");
        snprintf(d->code, sizeof(d->code), "%s", code ? code : "E-UNKNOWN");
        snprintf(d->message, sizeof(d->message), "%s", message ? message : "");
        if (loc) {
            d->loc = *loc;
            d->has_loc = 1;
        } else {
            memset(&d->loc, 0, sizeof(d->loc));
            d->has_loc = 0;
        }
    }
}

int optlilisp_sink_has_errors(const OptlilispSink *sink)
{
    size_t i;
    for (i = 0; i < sink->count; i++)
        if (strcmp(sink->items[i].severity, "error") == 0) return 1;
    return 0;
}

size_t optlilisp_sink_error_count(const OptlilispSink *sink)
{
    size_t i, n = 0;
    for (i = 0; i < sink->count; i++)
        if (strcmp(sink->items[i].severity, "error") == 0) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Nodes                                                              */
/* ------------------------------------------------------------------ */

static OptlilispNode *node_new(OptlilispKind kind, const OptlilispLoc *loc)
{
    OptlilispNode *n = (OptlilispNode *)xmalloc(sizeof(*n));
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    if (loc) n->loc = *loc;
    return n;
}

static void node_list_push(OptlilispNode *list, OptlilispNode *item)
{
    if (list->u.list.count == list->u.list.capacity) {
        size_t ncap = list->u.list.capacity ? list->u.list.capacity * 2 : 8;
        list->u.list.items =
            (OptlilispNode **)xrealloc(list->u.list.items, ncap * sizeof(*list->u.list.items));
        list->u.list.capacity = ncap;
    }
    list->u.list.items[list->u.list.count++] = item;
}

void optlilisp_node_free(OptlilispNode *node)
{
    size_t i;
    if (!node) return;
    switch (node->kind) {
    case OPTLILISP_SYM: free(node->u.sym); break;
    case OPTLILISP_KEYWORD: free(node->u.keyword); break;
    case OPTLILISP_STRING: free(node->u.string); break;
    case OPTLILISP_LIST:
        for (i = 0; i < node->u.list.count; i++)
            optlilisp_node_free(node->u.list.items[i]);
        free(node->u.list.items);
        break;
    default: break;
    }
    free(node);
}

void optlilisp_program_init(OptlilispProgram *prog)
{
    prog->forms = NULL;
    prog->count = 0;
    prog->capacity = 0;
    prog->filename = NULL;
}

void optlilisp_program_fini(OptlilispProgram *prog)
{
    size_t i;
    for (i = 0; i < prog->count; i++)
        optlilisp_node_free(prog->forms[i]);
    free(prog->forms);
    free(prog->filename);
    prog->forms = NULL;
    prog->count = 0;
    prog->capacity = 0;
    prog->filename = NULL;
}

static void program_push(OptlilispProgram *prog, OptlilispNode *node)
{
    if (prog->count == prog->capacity) {
        size_t ncap = prog->capacity ? prog->capacity * 2 : 16;
        prog->forms = (OptlilispNode **)xrealloc(prog->forms, ncap * sizeof(*prog->forms));
        prog->capacity = ncap;
    }
    prog->forms[prog->count++] = node;
}

/* ------------------------------------------------------------------ */
/* Reader                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int col;
    const char *filename; /* not owned here; owned copy lives in Program */
    OptlilispSink *sink;
    int fatal; /* set after an unrecoverable error inside a list */
    size_t depth;
} Reader;

static OptlilispLoc reader_loc(Reader *r)
{
    OptlilispLoc loc;
    loc.file = r->filename;
    loc.line = r->line;
    loc.column = r->col;
    return loc;
}

static void reader_error(Reader *r, const char *code, const char *message,
                         const OptlilispLoc *loc)
{
    OptlilispLoc here = loc ? *loc : reader_loc(r);
    optlilisp_sink_emit(r->sink, "error", code, message, &here);
}

static int reader_eof(Reader *r) { return r->pos >= r->len; }

static char reader_peek(Reader *r) { return reader_eof(r) ? '\0' : r->src[r->pos]; }

static void reader_advance(Reader *r)
{
    if (reader_eof(r)) return;
    if (r->src[r->pos] == '\n') {
        r->line++;
        r->col = 1;
    } else {
        r->col++;
    }
    r->pos++;
}

static void reader_skip_trivia(Reader *r)
{
    while (!reader_eof(r)) {
        char c = reader_peek(r);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == '\f' || c == '\v') {
            reader_advance(r);
        } else if (c == ';') {
            while (!reader_eof(r) && reader_peek(r) != '\n')
                reader_advance(r);
        } else {
            break;
        }
    }
}

static int is_atom_terminator(char c)
{
    return c == '\0' || c == '(' || c == ')' || c == ';' ||
           c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}

/* Forward declarations. */
static OptlilispNode *reader_read_form(Reader *r);

static OptlilispNode *reader_read_string(Reader *r)
{
    OptlilispLoc start = reader_loc(r);
    char *buf = NULL;
    size_t n = 0, cap = 0;
    OptlilispNode *node;
    reader_advance(r); /* opening quote */
    for (;;) {
        char c, esc;
        char out;
        if (reader_eof(r)) {
            OptlilispLoc loc = start;
            reader_error(r, "E-SYNTAX", "unterminated string literal", &loc);
            free(buf);
            return NULL;
        }
        c = reader_peek(r);
        if (c == '"') {
            reader_advance(r);
            break;
        }
        if (c == '\\') {
            OptlilispLoc eloc;
            reader_advance(r);
            if (reader_eof(r)) {
                OptlilispLoc loc = start;
                reader_error(r, "E-SYNTAX", "unterminated escape sequence", &loc);
                free(buf);
                return NULL;
            }
            esc = reader_peek(r);
            reader_advance(r);
            switch (esc) {
            case 'n': out = '\n'; break;
            case 't': out = '\t'; break;
            case 'r': out = '\r'; break;
            case '"': out = '"'; break;
            case '\\': out = '\\'; break;
            case '0': out = '\0'; break;
            default: {
                char msg[OPTLILISP_MAX_MESSAGE];
                eloc = start;
                snprintf(msg, sizeof(msg), "invalid escape sequence '\\%c'", esc);
                reader_error(r, "E-SYNTAX", msg, &eloc);
                r->fatal = 1;
                free(buf);
                return NULL;
            }
            }
            c = out;
        } else {
            reader_advance(r);
        }
        if (n + 1 >= cap) {
            size_t ncap = cap ? cap * 2 : 32;
            buf = (char *)xrealloc(buf, ncap);
            cap = ncap;
        }
        buf[n++] = c;
    }
    if (n + 1 >= cap) {
        buf = (char *)xrealloc(buf, n + 1);
    }
    buf[n] = '\0';
    node = node_new(OPTLILISP_STRING, &start);
    node->u.string = buf;
    node->text_length = n;
    return node;
}

static int text_is_integer(const char *text, size_t n)
{
    size_t i = 0;
    if (n == 0) return 0;
    if (text[0] == '+' || text[0] == '-') i = 1;
    if (i == n) return 0;
    for (; i < n; i++)
        if (!isdigit((unsigned char)text[i])) return 0;
    return 1;
}

/* True when text looks like a float and strtod consumes all of it. */
static int text_is_float(const char *text)
{
    char *end = NULL;
    double v;
    int has_dot_e = 0;
    const char *p;
    (void)v;
    if (!text || !*text) return 0;
    if (strcmp(text, "+") == 0 || strcmp(text, "-") == 0) return 0;
    for (p = text; *p; p++)
        if (*p == '.' || *p == 'e' || *p == 'E') has_dot_e = 1;
    if (!has_dot_e) return 0;
    v = strtod(text, &end);
    (void)v;
    return end && *end == '\0';
}

static OptlilispNode *reader_read_atom(Reader *r)
{
    OptlilispLoc start = reader_loc(r);
    size_t begin = r->pos;
    size_t n;
    char *text;
    OptlilispNode *node;
    while (!reader_eof(r) && !is_atom_terminator(reader_peek(r)))
        reader_advance(r);
    n = r->pos - begin;
    text = xstrndup(r->src + begin, n);
    if (strcmp(text, "true") == 0) {
        node = node_new(OPTLILISP_BOOL, &start);
        node->u.bval = 1;
        free(text);
        return node;
    }
    if (strcmp(text, "false") == 0) {
        node = node_new(OPTLILISP_BOOL, &start);
        node->u.bval = 0;
        free(text);
        return node;
    }
    if (strcmp(text, "nil") == 0) {
        node = node_new(OPTLILISP_NIL, &start);
        free(text);
        return node;
    }
    if (text_is_integer(text, n)) {
        long long value;
        errno = 0;
        value = strtoll(text, NULL, 10);
        if (errno == ERANGE) {
            reader_error(r, "E-SYNTAX", "integer literal outside long long range", &start);
            free(text);
            return NULL;
        }
        node = node_new(OPTLILISP_INT, &start);
        node->u.ival = value;
        free(text);
        return node;
    }
    if (text_is_float(text)) {
        node = node_new(OPTLILISP_FLOAT, &start);
        node->u.fval = strtod(text, NULL);
        free(text);
        return node;
    }
    node = node_new(OPTLILISP_SYM, &start);
    node->u.sym = text;
    node->text_length = n;
    return node;
}

static OptlilispNode *reader_read_list(Reader *r)
{
    OptlilispLoc start = reader_loc(r);
    OptlilispNode *list;
    OptlilispNode *form;
    if (r->depth == OPTLILISP_MAX_DEPTH) {
        reader_error(r, "E-SYNTAX", "maximum list nesting depth exceeded", &start);
        r->fatal = 1;
        return NULL;
    }
    r->depth++;
    list = node_new(OPTLILISP_LIST, &start);
    reader_advance(r); /* '(' */
    for (;;) {
        reader_skip_trivia(r);
        if (reader_eof(r)) {
            reader_error(r, "E-SYNTAX", "unexpected end of input: missing ')'", &start);
            r->fatal = 1;
            optlilisp_node_free(list);
            r->depth--;
            return NULL;
        }
        if (reader_peek(r) == ')') {
            reader_advance(r);
            r->depth--;
            return list;
        }
        form = reader_read_form(r);
        if (form) node_list_push(list, form);
        if (r->fatal) {
            optlilisp_node_free(list);
            r->depth--;
            return NULL;
        }
    }
}

static OptlilispNode *reader_read_form(Reader *r)
{
    OptlilispLoc start;
    char c;
    size_t begin, n;
    char *name;
    OptlilispNode *node;
    reader_skip_trivia(r);
    if (reader_eof(r)) return NULL;
    c = reader_peek(r);
    if (c == '(') return reader_read_list(r);
    if (c == ')') {
        reader_error(r, "E-SYNTAX", "unexpected ')'", NULL);
        reader_advance(r);
        return NULL;
    }
    if (c == '"') return reader_read_string(r);
    if (c == ':') {
        start = reader_loc(r);
        reader_advance(r);
        begin = r->pos;
        while (!reader_eof(r) && !is_atom_terminator(reader_peek(r)))
            reader_advance(r);
        n = r->pos - begin;
        if (n == 0) {
            reader_error(r, "E-SYNTAX", "expected a keyword name after ':'", &start);
            return NULL;
        }
        name = xstrndup(r->src + begin, n);
        node = node_new(OPTLILISP_KEYWORD, &start);
        node->u.keyword = name;
        node->text_length = n;
        return node;
    }
    return reader_read_atom(r);
}

int optlilisp_parse(const char *source, const char *filename,
                    OptlilispProgram *prog, OptlilispSink *sink)
{
    Reader r;
    OptlilispNode *form;
    size_t before;
    if (!source || !prog || !sink) return 1;
    before = optlilisp_sink_error_count(sink);
    optlilisp_program_fini(prog);
    prog->filename = xstrdup(filename ? filename : "<string>");
    r.src = source;
    r.len = strlen(source);
    r.pos = 0;
    r.line = 1;
    r.col = 1;
    r.filename = prog->filename;
    r.sink = sink;
    r.fatal = 0;
    r.depth = 0;
    for (;;) {
        reader_skip_trivia(&r);
        if (reader_eof(&r)) break;
        form = reader_read_form(&r);
        if (form) program_push(prog, form);
        if (r.fatal) break;
    }
    return optlilisp_sink_error_count(sink) != before;
}

int optlilisp_parse_file(const char *path, OptlilispProgram *prog,
                         OptlilispSink *sink)
{
    FILE *fp = NULL;
    long size = 0;
    char *buf = NULL;
    size_t got = 0;
    int rc;
    if (!path || !prog || !sink) return 1;
    optlilisp_program_fini(prog);
    fp = fopen(path, "rb");
    if (!fp) {
        char msg[OPTLILISP_MAX_MESSAGE];
        snprintf(msg, sizeof(msg), "cannot open file '%s'", path);
        optlilisp_sink_emit(sink, "error", "E-IO", msg, NULL);
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        optlilisp_sink_emit(sink, "error", "E-IO", "cannot seek input file", NULL);
        return 1;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        optlilisp_sink_emit(sink, "error", "E-IO", "cannot determine input size", NULL);
        return 1;
    }
    rewind(fp);
    buf = (char *)xmalloc((size_t)size + 1);
    got = fread(buf, 1, (size_t)size, fp);
    if (got != (size_t)size || ferror(fp)) {
        fclose(fp);
        free(buf);
        optlilisp_sink_emit(sink, "error", "E-IO", "cannot read complete input file", NULL);
        return 1;
    }
    fclose(fp);
    if (memchr(buf, '\0', got)) {
        free(buf);
        optlilisp_sink_emit(sink, "error", "E-SYNTAX", "input contains a raw NUL byte", NULL);
        return 1;
    }
    buf[got] = '\0';
    rc = optlilisp_parse(buf, path, prog, sink);
    free(buf);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                          */
/* ------------------------------------------------------------------ */

int optlilisp_is_symbol(const OptlilispNode *node, const char *name)
{
    if (!node || node->kind != OPTLILISP_SYM) return 0;
    if (!name) return 1;
    return strcmp(node->u.sym, name) == 0;
}

int optlilisp_is_keyword(const OptlilispNode *node, const char *name)
{
    if (!node || node->kind != OPTLILISP_KEYWORD) return 0;
    if (!name) return 1;
    return strcmp(node->u.keyword, name) == 0;
}

const char *optlilisp_as_symbol(const OptlilispNode *node)
{
    if (!node || node->kind != OPTLILISP_SYM) return NULL;
    return node->u.sym;
}

const char *optlilisp_as_string(const OptlilispNode *node)
{
    if (!node || node->kind != OPTLILISP_STRING) return NULL;
    return node->u.string;
}

size_t optlilisp_list_count(const OptlilispNode *node)
{
    if (!node || node->kind != OPTLILISP_LIST) return 0;
    return node->u.list.count;
}

OptlilispNode *optlilisp_list_get(const OptlilispNode *node, size_t index)
{
    if (!node || node->kind != OPTLILISP_LIST) return NULL;
    if (index >= node->u.list.count) return NULL;
    return node->u.list.items[index];
}

static int define_head_is(const OptlilispNode *node, const char *head)
{
    if (!node || node->kind != OPTLILISP_LIST) return 0;
    if (node->u.list.count == 0) return 0;
    return optlilisp_is_symbol(node->u.list.items[0], head);
}

int optlilisp_is_define_kernel(const OptlilispNode *node)
{
    return define_head_is(node, "define-kernel");
}

int optlilisp_is_define_monoid(const OptlilispNode *node)
{
    return define_head_is(node, "define-monoid");
}

int optlilisp_is_define_capability(const OptlilispNode *node)
{
    return define_head_is(node, "define-capability");
}

int optlilisp_is_define_analysis(const OptlilispNode *node)
{
    return define_head_is(node, "define-analysis");
}

OptlilispNode *optlilisp_find_section(const OptlilispNode *define_form,
                                      const char *head)
{
    size_t i;
    if (!define_form || define_form->kind != OPTLILISP_LIST || !head)
        return NULL;
    for (i = 1; i < define_form->u.list.count; i++) {
        OptlilispNode *item = define_form->u.list.items[i];
        if (item->kind == OPTLILISP_LIST && item->u.list.count > 0 &&
            optlilisp_is_symbol(item->u.list.items[0], head))
            return item;
    }
    return NULL;
}

OptlilispNode *optlilisp_keyword_arg(const OptlilispNode *define_form,
                                     const char *name)
{
    size_t i;
    if (!define_form || define_form->kind != OPTLILISP_LIST || !name)
        return NULL;
    for (i = 1; i + 1 < define_form->u.list.count; i += 2) {
        OptlilispNode *k = define_form->u.list.items[i];
        if (k->kind != OPTLILISP_KEYWORD) break;
        if (strcmp(k->u.keyword, name) == 0)
            return define_form->u.list.items[i + 1];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Kernel info                                                        */
/* ------------------------------------------------------------------ */

static int copy_field(char *dest, size_t capacity, const OptlilispNode *node,
                      const char *text)
{
    if (node->text_length >= capacity || strlen(text) != node->text_length)
        return 1;
    memcpy(dest, text, node->text_length + 1);
    return 0;
}

int optlilisp_kernel_info(const OptlilispNode *define_kernel,
                          OptlilispKernelInfo *info)
{
    OptlilispNode *id, *name, *stage, *transform, *verb_form;
    if (!define_kernel || !info) return 1;
    memset(info, 0, sizeof(*info));
    if (!optlilisp_is_define_kernel(define_kernel)) return 1;

    id = optlilisp_keyword_arg(define_kernel, "id");
    if (id && id->kind == OPTLILISP_STRING) {
        if (copy_field(info->id, sizeof(info->id), id, id->u.string)) return 1;
        info->has_id = info->id[0] != '\0';
    }
    name = optlilisp_keyword_arg(define_kernel, "name");
    if (name && name->kind == OPTLILISP_STRING) {
        if (copy_field(info->name, sizeof(info->name), name, name->u.string)) return 1;
        info->has_name = info->name[0] != '\0';
    }
    stage = optlilisp_keyword_arg(define_kernel, "stage");
    if (stage && stage->kind == OPTLILISP_SYM) {
        if (copy_field(info->stage, sizeof(info->stage), stage, stage->u.sym)) return 1;
        info->has_stage = info->stage[0] != '\0';
    }
    transform = optlilisp_find_section(define_kernel, "transform");
    if (transform && transform->u.list.count > 1) {
        size_t i;
        for (i = 1; i < transform->u.list.count; i++) {
            verb_form = transform->u.list.items[i];
            if (verb_form->kind == OPTLILISP_LIST &&
                verb_form->u.list.count > 0 &&
                verb_form->u.list.items[0]->kind == OPTLILISP_SYM) {
                OptlilispNode *head = verb_form->u.list.items[0];
                if (copy_field(info->verb, sizeof(info->verb), head, head->u.sym)) return 1;
                info->has_transform = 1;
                break;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                         */
/* ------------------------------------------------------------------ */

int optlilisp_is_stage(const char *stage)
{
    return stage && (strcmp(stage, "hir") == 0 ||
                     strcmp(stage, "mir") == 0 ||
                     strcmp(stage, "lir") == 0);
}

static const char *const kVerbs[] = {
    "rewrite", "fold", "propagate", "eliminate", "hoist", "sink",
    "interchange", "unroll", "peel", "fuse", "distribute", "tile", "skew",
    "inline", "specialize", "devirtualize", "lower", "select", "combine",
    "promote", "demote", "coalesce", "rematerialize", "forward",
    "canonicalize", "simplify", "split", "merge", "rotate", "vectorize",
    "parallelize", "if-convert", "construct", "outline", "place", "schedule",
    "predict", "deduplicate", "remove", "strength-reduce", NULL
};

static const char *const kHirVerbs[] = {
    "rewrite", "fold", "propagate", "eliminate", "hoist", "sink",
    "interchange", "unroll", "peel", "fuse", "distribute", "tile",
    "inline", "specialize", "canonicalize", "simplify", "split",
    "merge", "deduplicate", "remove", "strength-reduce", NULL
};

static const char *const kMirVerbs[] = {
    "rewrite", "fold", "propagate", "eliminate", "hoist", "sink",
    "interchange", "unroll", "peel", "fuse", "distribute", "tile",
    "skew", "inline", "specialize", "devirtualize", "promote",
    "forward", "canonicalize", "simplify", "split", "merge",
    "vectorize", "parallelize", "if-convert", "construct", "outline",
    "deduplicate", "remove", "strength-reduce", "coalesce", "combine", NULL
};

static const char *const kLirVerbs[] = {
    "rewrite", "fold", "eliminate", "hoist", "lower", "select",
    "combine", "promote", "demote", "coalesce", "rematerialize",
    "canonicalize", "simplify", "split", "merge", "place", "schedule",
    "deduplicate", "remove", "strength-reduce", "sink", NULL
};

static int str_in_list(const char *s, const char *const *list)
{
    size_t i;
    if (!s || !list) return 0;
    for (i = 0; list[i]; i++)
        if (strcmp(s, list[i]) == 0) return 1;
    return 0;
}

int optlilisp_is_verb(const char *verb) { return str_in_list(verb, kVerbs); }

int optlilisp_verb_ok_at_stage(const char *verb, const char *stage)
{
    if (!verb || !stage) return 0;
    if (strcmp(stage, "hir") == 0) return str_in_list(verb, kHirVerbs);
    if (strcmp(stage, "mir") == 0) return str_in_list(verb, kMirVerbs);
    if (strcmp(stage, "lir") == 0) return str_in_list(verb, kLirVerbs);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Validation                                                         */
/* ------------------------------------------------------------------ */

static int section_present(const OptlilispNode *define_kernel, const char *head)
{
    OptlilispNode *section = optlilisp_find_section(define_kernel, head);
    return section && section->u.list.count > 1;
}

static const char *const kSections[] = {
    "category", "paradigms", "objective", "requires", "preconditions",
    "forbids", "prerequisites", "forbidden-prior-transformations",
    "analysis-contract", "transform", "correctness", "expected-effects",
    "limitations", "documentation", NULL
};

static int nonblank(const char *s)
{
    while (*s) {
        if (!isspace((unsigned char)*s)) return 1;
        s++;
    }
    return 0;
}

static void validate_shape(const OptlilispNode *form, OptlilispSink *sink)
{
    size_t i = 1, j;
    /* Keywords are leading singleton pairs; never skip a malformed value. */
    while (i < form->u.list.count && form->u.list.items[i]->kind == OPTLILISP_KEYWORD) {
        OptlilispNode *key = form->u.list.items[i];
        OptlilispNode *value = optlilisp_list_get(form, i + 1);
        const char *name = key->u.keyword;
        int stage = strcmp(name, "stage") == 0;
        if (!stage && strcmp(name, "id") && strcmp(name, "name"))
            optlilisp_sink_emit(sink, "error", "E-STRUCT", "unknown kernel keyword", &key->loc);
        for (j = 1; j < i; j += 2)
            if (optlilisp_is_keyword(form->u.list.items[j], name))
                optlilisp_sink_emit(sink, "error", "E-STRUCT", "duplicate kernel keyword", &key->loc);
        if (!value || value->kind != (stage ? OPTLILISP_SYM : OPTLILISP_STRING))
            optlilisp_sink_emit(sink, "error", "E-STRUCT", "invalid kernel keyword value", &key->loc);
        else if (!stage && !nonblank(value->u.string))
            optlilisp_sink_emit(sink, "error", "E-STRUCT", "blank kernel keyword value", &value->loc);
        i += 2;
    }
    for (; i < form->u.list.count; i++) {
        OptlilispNode *section = form->u.list.items[i];
        OptlilispNode *head = optlilisp_list_get(section, 0);
        const char *name = optlilisp_as_symbol(head);
        if (!name || !str_in_list(name, kSections)) {
            optlilisp_sink_emit(sink, "error", "E-STRUCT", "unknown or malformed kernel section", &section->loc);
            continue;
        }
        for (j = 1; j < i; j++) {
            OptlilispNode *previous = form->u.list.items[j];
            if (define_head_is(previous, name))
                optlilisp_sink_emit(sink, "error", "E-STRUCT", "duplicate kernel section", &section->loc);
        }
        for (j = 1; j < section->u.list.count; j++) {
            OptlilispNode *item = section->u.list.items[j];
            int valid = 1;
            if (!strcmp(name, "objective") || !strcmp(name, "expected-effects") || !strcmp(name, "limitations"))
                valid = item->kind == OPTLILISP_STRING && nonblank(item->u.string);
            else if (!strcmp(name, "category") || !strcmp(name, "paradigms"))
                valid = item->kind == OPTLILISP_SYM;
            else if (!strcmp(name, "requires")) {
                OptlilispNode *ref = optlilisp_list_get(item, 1);
                valid = (define_head_is(item, "capability") || define_head_is(item, "analysis")) &&
                    optlilisp_list_count(item) == 2 && ref &&
                    (ref->kind == OPTLILISP_SYM || ref->kind == OPTLILISP_STRING);
            } else if (!strcmp(name, "preconditions") || !strcmp(name, "forbids") || !strcmp(name, "correctness"))
                valid = item->kind == OPTLILISP_SYM || optlilisp_as_symbol(optlilisp_list_get(item, 0));
            else if (!strcmp(name, "transform"))
                valid = section->u.list.count == 2 && optlilisp_as_symbol(optlilisp_list_get(item, 0));
            else if (!strcmp(name, "documentation"))
                valid = (item->kind == OPTLILISP_STRING && nonblank(item->u.string)) ||
                    (optlilisp_as_symbol(optlilisp_list_get(item, 0)) && optlilisp_list_count(item) > 1);
            if (!valid)
                optlilisp_sink_emit(sink, "error", "E-STRUCT", "invalid kernel section value", &item->loc);
        }
    }
}

static void struct_error(OptlilispSink *sink, const OptlilispNode *node,
                         const char *kernel_id, const char *fmt_tail)
{
    char msg[OPTLILISP_MAX_MESSAGE];
    if (kernel_id && *kernel_id)
        snprintf(msg, sizeof(msg), "kernel '%s' %s", kernel_id, fmt_tail);
    else
        snprintf(msg, sizeof(msg), "kernel %s", fmt_tail);
    optlilisp_sink_emit(sink, "error", "E-STRUCT", msg, node ? &node->loc : NULL);
}

int optlilisp_validate_kernel(const OptlilispNode *define_kernel,
                              OptlilispSink *sink)
{
    OptlilispKernelInfo info;
    size_t before;
    OptlilispNode *transform = NULL;
    OptlilispNode *verb_form = NULL;
    size_t nargs = 0;
    const char *verb = NULL;
    if (!define_kernel || !sink) return 1;
    before = optlilisp_sink_error_count(sink);
    if (!optlilisp_is_define_kernel(define_kernel)) {
        optlilisp_sink_emit(sink, "error", "E-STRUCT",
                            "expected a (define-kernel ...) form",
                            &define_kernel->loc);
        return 1;
    }
    validate_shape(define_kernel, sink);
    if (optlilisp_kernel_info(define_kernel, &info)) {
        struct_error(sink, define_kernel, "", "field exceeds buffer size or contains embedded NUL");
        return 1;
    }

    if (!info.has_id)
        struct_error(sink, define_kernel, "", "is missing a stable :id");
    if (!info.has_name)
        struct_error(sink, define_kernel, info.id, "is missing a human-readable :name");
    if (!info.has_stage)
        struct_error(sink, define_kernel, info.id, "has an invalid or missing stage");
    else if (!optlilisp_is_stage(info.stage)) {
        char tail[128];
        snprintf(tail, sizeof(tail), "has an invalid stage '%s'", info.stage);
        struct_error(sink, define_kernel, info.id, tail);
    }
    if (!section_present(define_kernel, "category"))
        struct_error(sink, define_kernel, info.id, "must declare at least one (category ...)");
    if (!section_present(define_kernel, "objective"))
        struct_error(sink, define_kernel, info.id, "must declare a primary (objective ...)");
    if (!section_present(define_kernel, "requires"))
        struct_error(sink, define_kernel, info.id, "must declare (requires ...)");
    if (!section_present(define_kernel, "preconditions"))
        struct_error(sink, define_kernel, info.id, "must declare at least one precondition");
    if (!info.has_transform)
        struct_error(sink, define_kernel, info.id, "must declare a (transform ...)");
    if (!section_present(define_kernel, "correctness"))
        struct_error(sink, define_kernel, info.id, "must declare (correctness ...)");
    if (!section_present(define_kernel, "expected-effects"))
        struct_error(sink, define_kernel, info.id, "must declare (expected-effects ...)");
    if (!section_present(define_kernel, "limitations"))
        struct_error(sink, define_kernel, info.id, "must declare (limitations ...)");
    if (!section_present(define_kernel, "documentation"))
        struct_error(sink, define_kernel, info.id, "must declare (documentation ...)");

    /* Layer 4: verb vocabulary and stage compatibility. */
    if (info.has_transform && info.has_stage) {
        if (!optlilisp_is_verb(info.verb)) {
            char msg[OPTLILISP_MAX_MESSAGE];
            snprintf(msg, sizeof(msg),
                     "unknown transformation verb '%.63s' in kernel '%.140s'",
                     info.verb, info.id);
            optlilisp_sink_emit(sink, "error", "E-SEMANTIC", msg,
                                &define_kernel->loc);
        } else if (optlilisp_is_stage(info.stage) &&
                   !optlilisp_verb_ok_at_stage(info.verb, info.stage)) {
            char msg[OPTLILISP_MAX_MESSAGE];
            snprintf(msg, sizeof(msg),
                     "transformation verb '%.63s' is not valid at stage '%.15s' for kernel '%.110s'",
                     info.verb, info.stage, info.id);
            optlilisp_sink_emit(sink, "error", "E-SEMANTIC", msg,
                                &define_kernel->loc);
        }
    }

    /* Layer 5: transform arity. */
    transform = optlilisp_find_section(define_kernel, "transform");
    if (transform && info.has_transform) {
        size_t i;
        for (i = 1; i < transform->u.list.count; i++) {
            OptlilispNode *item = transform->u.list.items[i];
            if (item->kind == OPTLILISP_LIST && item->u.list.count > 0 &&
                item->u.list.items[0]->kind == OPTLILISP_SYM) {
                verb_form = item;
                break;
            }
        }
        if (verb_form) {
            verb = verb_form->u.list.items[0]->u.sym;
            nargs = verb_form->u.list.count - 1;
            if (nargs < 1) {
                char msg[OPTLILISP_MAX_MESSAGE];
                snprintf(msg, sizeof(msg),
                         "kernel '%.140s' transform verb '%.63s' has no arguments",
                         info.id, verb);
                optlilisp_sink_emit(sink, "error", "E-TRANSFORM", msg,
                                    &verb_form->loc);
            } else if ((strcmp(verb, "rewrite") == 0 ||
                        strcmp(verb, "fold") == 0) &&
                       nargs < 2) {
                char msg[OPTLILISP_MAX_MESSAGE];
                snprintf(msg, sizeof(msg),
                         "transform verb '%s' requires a pattern and a result form",
                         verb);
                optlilisp_sink_emit(sink, "error", "E-TRANSFORM", msg,
                                    &verb_form->loc);
            }
        }
    }

    return optlilisp_sink_error_count(sink) == before ? 0 : 1;
}

int optlilisp_validate_program(const OptlilispProgram *prog,
                               OptlilispSink *sink)
{
    size_t i, before;
    if (!prog || !sink) return 1;
    before = optlilisp_sink_error_count(sink);
    for (i = 0; i < prog->count; i++) {
        if (optlilisp_is_define_kernel(prog->forms[i]))
            optlilisp_validate_kernel(prog->forms[i], sink);
        else if (!optlilisp_is_define_monoid(prog->forms[i]) &&
                 !optlilisp_is_define_capability(prog->forms[i]) &&
                 !optlilisp_is_define_analysis(prog->forms[i]))
            optlilisp_sink_emit(sink, "error", "E-FORM", "unknown top-level definition", &prog->forms[i]->loc);
    }
    return optlilisp_sink_error_count(sink) == before ? 0 : 1;
}
