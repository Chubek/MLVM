/* Optlilisp.h -- C loader for Blackstone Optilisp (S-expression database).
 *
 * Mirrors the Python implementation in optilisp/src/optilisp/:
 *   sexpr.py    (Layer 1: syntax / reader)
 *   semantic.py (form shape: define-kernel, define-monoid, ...; verb tables)
 *   validate.py (Layers 2/4/5 subset: structural + stage/verb checks)
 *
 * Layers 3 (reference resolution against stdlib registries) and 6 (database
 * directory scan) are intentionally left to the Python tooling
 * (tools/validate-kernels, optilisp.validate.load_database), which owns the
 * on-disk registry. This C loader provides the embedding-facing subset:
 * read source, extract kernel fields, and run the statically checkable
 * structural/verb validation with source locations.
 *
 * Standard C11 only. No external dependencies. Numeric conversion follows
 * the C locale; callers using other locales should retain LC_NUMERIC=C.
 *
 * NOTE on the filename: the directory spells this "Optlilisp" (double-l).
 * The header keeps that spelling for the file name; the API prefix is
 * `optlilisp_` / `Optlilisp` for compatibility with these filenames.
 */

#ifndef OPTLILISP_H_INCLUDED
#define OPTLILISP_H_INCLUDED

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Limits                                                             */
/* ------------------------------------------------------------------ */

#define OPTLILISP_MAX_CODE 32
#define OPTLILISP_MAX_SEVERITY 16
#define OPTLILISP_MAX_MESSAGE 256
#define OPTLILISP_MAX_FIELD 256
#define OPTLILISP_MAX_STAGE 16
#define OPTLILISP_MAX_VERB 64
#define OPTLILISP_MAX_DEPTH 256

/* ------------------------------------------------------------------ */
/* Locations and diagnostics                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *file; /* not owned; points at the Program filename or caller string */
    int line;         /* 1-based */
    int column;       /* 1-based */
} OptlilispLoc;

typedef struct {
    char severity[OPTLILISP_MAX_SEVERITY]; /* "error" | "warning" | "note" */
    char code[OPTLILISP_MAX_CODE];         /* stable code, e.g. "E-SYNTAX" */
    char message[OPTLILISP_MAX_MESSAGE];
    OptlilispLoc loc;
    int has_loc;
} OptlilispDiag;

typedef struct {
    OptlilispDiag *items;
    size_t count;
    size_t capacity;
} OptlilispSink;

void optlilisp_sink_init(OptlilispSink *sink);
void optlilisp_sink_fini(OptlilispSink *sink);
void optlilisp_sink_emit(OptlilispSink *sink, const char *severity,
                         const char *code, const char *message,
                         const OptlilispLoc *loc);
int optlilisp_sink_has_errors(const OptlilispSink *sink);
size_t optlilisp_sink_error_count(const OptlilispSink *sink);

/* ------------------------------------------------------------------ */
/* S-expression nodes (Layer 1)                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    OPTLILISP_SYM = 0,    /* symbol: integer-add, define-kernel */
    OPTLILISP_KEYWORD,    /* keyword: :id (name stored without ':') */
    OPTLILISP_STRING,     /* string value */
    OPTLILISP_INT,        /* integer literal */
    OPTLILISP_FLOAT,      /* float literal */
    OPTLILISP_BOOL,       /* true / false */
    OPTLILISP_NIL,        /* nil */
    OPTLILISP_LIST        /* parenthesized list */
} OptlilispKind;

typedef struct OptlilispNode OptlilispNode;

struct OptlilispNode {
    OptlilispKind kind;
    OptlilispLoc loc;
    size_t text_length; /* byte length for symbols, keywords and strings;
                        * strings may contain embedded NUL bytes */
    union {
        char *sym;    /* OPTLILISP_SYM */
        char *keyword; /* OPTLILISP_KEYWORD (without ':') */
        char *string;  /* OPTLILISP_STRING */
        long long ival; /* OPTLILISP_INT */
        double fval;    /* OPTLILISP_FLOAT */
        int bval;       /* OPTLILISP_BOOL */
        struct {
            OptlilispNode **items;
            size_t count;
            size_t capacity;
        } list; /* OPTLILISP_LIST */
    } u;
};

typedef struct {
    OptlilispNode **forms; /* top-level forms */
    size_t count;
    size_t capacity;
    char *filename; /* owned copy used by node loc.file pointers */
} OptlilispProgram;

void optlilisp_program_init(OptlilispProgram *prog);
void optlilisp_program_fini(OptlilispProgram *prog);
void optlilisp_node_free(OptlilispNode *node);

/* Initialize prog before parsing. Parsing replaces its previous contents.
 * Returns 0 if no new errors were added, nonzero otherwise. Complete forms
 * before a fatal error remain available. Numeric integers must fit long long;
 * list depth is bounded by OPTLILISP_MAX_DEPTH. Diagnostics borrow filename
 * storage from prog and must be consumed before prog is replaced/freed. */
int optlilisp_parse(const char *source, const char *filename,
                    OptlilispProgram *prog, OptlilispSink *sink);

/* Read a file and parse it. Returns 0 on success, nonzero on I/O or
 * parse errors (diagnostics in sink). */
int optlilisp_parse_file(const char *path, OptlilispProgram *prog,
                         OptlilispSink *sink);

/* ------------------------------------------------------------------ */
/* Predicates and accessors                                           */
/* ------------------------------------------------------------------ */

int optlilisp_is_symbol(const OptlilispNode *node, const char *name);
int optlilisp_is_keyword(const OptlilispNode *node, const char *name);
const char *optlilisp_as_symbol(const OptlilispNode *node);
const char *optlilisp_as_string(const OptlilispNode *node);
size_t optlilisp_list_count(const OptlilispNode *node);
OptlilispNode *optlilisp_list_get(const OptlilispNode *node, size_t index);

/* Find a body section `(head ...)` inside a define form. Returns NULL
 * when absent. */
OptlilispNode *optlilisp_find_section(const OptlilispNode *define_form,
                                      const char *head);

/* Fetch a leading `:keyword value` argument from a define form.
 * Returns NULL when absent. */
OptlilispNode *optlilisp_keyword_arg(const OptlilispNode *define_form,
                                     const char *name);

/* Top-level form classification. */
int optlilisp_is_define_kernel(const OptlilispNode *node);
int optlilisp_is_define_monoid(const OptlilispNode *node);
int optlilisp_is_define_capability(const OptlilispNode *node);
int optlilisp_is_define_analysis(const OptlilispNode *node);

/* ------------------------------------------------------------------ */
/* Kernel field extraction (semantic shape, no validation)            */
/* ------------------------------------------------------------------ */

typedef struct {
    char id[OPTLILISP_MAX_FIELD];
    char name[OPTLILISP_MAX_FIELD];
    char stage[OPTLILISP_MAX_STAGE];
    char verb[OPTLILISP_MAX_VERB];
    int has_id;
    int has_name;
    int has_stage;
    int has_transform;
} OptlilispKernelInfo;

/* Fill `info` from a `(define-kernel ...)` form. Returns 0 when the node
 * is a define-kernel form (fields may still be missing; check has_*),
 * nonzero otherwise or if an extracted field exceeds its buffer size or
 * contains embedded NUL. Full field values remain accessible in the AST. */
int optlilisp_kernel_info(const OptlilispNode *define_kernel,
                          OptlilispKernelInfo *info);

/* ------------------------------------------------------------------ */
/* Vocabulary tables (semantic.py: TRANSFORM_VERBS / STAGE_VERBS)     */
/* ------------------------------------------------------------------ */

int optlilisp_is_stage(const char *stage);
int optlilisp_is_verb(const char *verb);
int optlilisp_verb_ok_at_stage(const char *verb, const char *stage);

/* ------------------------------------------------------------------ */
/* Validation (Layers 2/4/5 subset)                                   */
/* ------------------------------------------------------------------ */

/* Validate one `(define-kernel ...)` form: required fields, recognized
 * stage, known verb, stage-appropriate verb, transform arity.
 * Emits E-STRUCT / E-SEMANTIC / E-TRANSFORM diagnostics.
 * Returns 0 when no new errors were added, nonzero otherwise. */
int optlilisp_validate_kernel(const OptlilispNode *define_kernel,
                              OptlilispSink *sink);

/* Validate every define-kernel form in a program and reject unknown top-level
 * definitions. Registry definitions are recognized but not validated. */
int optlilisp_validate_program(const OptlilispProgram *prog,
                               OptlilispSink *sink);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OPTLILISP_H_INCLUDED */
