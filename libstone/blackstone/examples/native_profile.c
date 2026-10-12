/* A minimal compiler integration: compile, persist, reload and apply x + 0 -> x.
 * Uses only public headers and immutable host-owned node handles. */
#include "Blackstone-Rewrite.h"
#include <stdio.h>
#include <stdint.h>

typedef struct Node {
    BSValueKind kind;
    const char *integer;
    const struct Node *left, *right;
} Node;

static BSValue handle(const Node *node) { return (BSValue)(uintptr_t)node; }

static int inspect(void *context, BSValue value, BSValueView *out) {
    (void)context;
    const Node *node = (const Node *)(uintptr_t)value;
    if (!node) return 0;
    *out = (BSValueView){0};
    out->kind = node->kind; out->resolved = 1;
    out->domain = "integer"; out->width = 8;
    out->op = node->kind == BS_BINARY_VALUE ? "plus" : NULL;
    out->integer = node->integer;
    out->left = handle(node->left); out->right = handle(node->right);
    return 1;
}

static void trace(void *context, const BSTrace *event) {
    (void)context;
    printf("%zu: %s (%s:%u:%u)\n", event->step, event->rule,
           event->rule_source.path, event->rule_source.line, event->rule_source.column);
}

int main(void) {
    BSSpan span = {"native-example.bst", 1, 1};
    BSMatchNode match[] = {
        {BS_TERM, BS_RESOLVES, 1, 0, 0, 0, "", span},
        {BS_TERM, BS_EXACT, 2, 0, 0, 0, "0", span},
        {BS_BINARY, 0, 0, 0, 0, 1, "plus", span}
    };
    BSBuildNode build[] = {{BS_CAPTURE, 1, 0, 0, "", span}};
    BSGuard guard[] = {{"ModularInteger", NULL, 0, span}};
    BSRule rule = {"NativeKeepLeft", BS_TERM, match, 3, build, 1, guard, 1, span};
    BSDeclaration views[] = {{"HIR::Literals::Term", span}, {"HIR::Expressions::Binary", span}};
    BSDeclaration imports[] = {{"Arithmetic::Domains::ModularInteger", span}};
    BSKernel kernel = {0};
    kernel.name = "NativeExample"; kernel.concerns = views; kernel.concern_count = 2;
    kernel.requires = imports; kernel.require_count = 1;
    kernel.rules = &rule; kernel.rule_count = 1; kernel.span = span;

    BSError error = {0};
    BSRegistry *registry = bs_registry_create(&error);
    BSProfile *compiled = NULL, *loaded = NULL;
    FILE *file = NULL;
    int result = 1;
    if (!registry) goto done;
    if (bs_profile_compile(registry, &kernel, 1, kernel.name, &compiled, &error) != BS_OK) goto done;
    file = tmpfile();
    if (!file) { perror("tmpfile"); goto done; }
    if (bs_profile_write(compiled, file, &error) != BS_OK) goto done;
    rewind(file);
    if (bs_profile_read(registry, file, &loaded, &error) != BS_OK) goto done;
    printf("Reloaded %zu rule(s): %s\n", bs_profile_rule_count(loaded), bs_profile_rule_name(loaded, 0));

    const Node x = {BS_INPUT_VALUE, NULL, NULL, NULL};
    const Node zero = {BS_LITERAL_VALUE, "0", NULL, NULL};
    const Node sum = {BS_BINARY_VALUE, NULL, &x, &zero};
    BSAdapter adapter = {0}; adapter.abi_version = BS_ABI_VERSION;
    adapter.inspect = inspect; adapter.trace = trace;
    /* ModularInteger has a native fallback; this rule only forwards a capture,
     * so no query, constructors, rebuild or folding callbacks are necessary. */
    BSRewriteOptions options = {8, BS_MAX_DEPTH};
    BSRewriteStats stats = {0}; BSValue optimized = 0;
    if (bs_optimize(loaded, &adapter, handle(&sum), &options, &optimized, &stats, &error) != BS_OK) goto done;
    if (optimized != handle(&x) || stats.rewrites != 1) {
        fputs("expected x with exactly one rewrite\n", stderr); goto done;
    }
    puts("Result: x, with the original value identity and unsigned-8-bit type");
    result = 0;
done:
    if (result && error.message[0]) fprintf(stderr, "blackstone: %s\n", error.message);
    if (file) fclose(file);
    bs_profile_destroy(loaded); bs_profile_destroy(compiled); bs_registry_destroy(registry);
    return result;
}
