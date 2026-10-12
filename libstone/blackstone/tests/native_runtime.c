#include "Blackstone-Rewrite.h"
#include "Blackstone-Requires.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define LOCATION {"native-test.bst", 12, 3}

static BSMatchNode match[] = {
    {BS_TERM, BS_RESOLVES, 1, 0, 0, 0, "", LOCATION},
    {BS_TERM, BS_EXACT, 2, 0, 0, 0, "0", LOCATION},
    {BS_BINARY, 0, 0, 0, 0, 1, "plus", LOCATION}
};
static BSBuildNode build[] = {{BS_CAPTURE, 1, 0, 0, "", LOCATION}};
static BSGuard guards[] = {{"ModularInteger", NULL, 0, LOCATION}};
static BSRule rules[] = {{"ZeroRight", BS_TERM, match, 3, build, 1, guards, 1, LOCATION}};
static BSDeclaration concerns[] = {{"HIR::Literals::Term", LOCATION}, {"HIR::Expressions::Binary", LOCATION}};
static BSDeclaration requires[] = {{"Arithmetic::Domains::ModularInteger", LOCATION}};

static BSKernel kernel(const char *name) {
    BSKernel k = {0}; k.name = name; k.concerns = concerns; k.concern_count = 2;
    k.requires = requires; k.require_count = 1; k.rules = rules; k.rule_count = 1;
    k.span = (BSSpan)LOCATION;
    return k;
}

typedef struct { BSValueView view; char integer[32]; } Node;
typedef struct { Node nodes[512]; size_t count, traces; int wrong_type; } Host;

static BSValue input(Host *h) {
    CHECK(h->count < 512);
    Node *n = &h->nodes[h->count++];
    n->view = (BSValueView){0}; n->view.kind = BS_INPUT_VALUE;
    n->view.resolved = 1; n->view.domain = "integer"; n->view.width = 8;
    return h->count;
}
static BSValue integer(Host *h, unsigned value) {
    BSValue id = input(h); Node *n = &h->nodes[id - 1];
    n->view.kind = BS_LITERAL_VALUE;
    snprintf(n->integer, sizeof(n->integer), "%u", value & 255);
    n->view.integer = n->integer;
    return id;
}
static BSValue operation(Host *h, BSValue left, BSValue right) {
    BSValue id = input(h); Node *n = &h->nodes[id - 1];
    n->view.kind = BS_BINARY_VALUE; n->view.op = "plus";
    n->view.left = left; n->view.right = right;
    return id;
}
static int inspect(void *ctx, BSValue id, BSValueView *out) {
    Host *h = ctx;
    if (!id || id > h->count) return 0;
    *out = h->nodes[id - 1].view;
    return 1;
}
static BSValue rebuild(void *ctx, BSValue source, BSValue left, BSValue right) {
    Host *h = ctx; BSValue id = operation(h, left, right);
    h->nodes[id - 1].view = h->nodes[source - 1].view;
    h->nodes[id - 1].view.left = left; h->nodes[id - 1].view.right = right;
    if (h->wrong_type) h->nodes[id - 1].view.width = 16;
    return id;
}
static BSValue fold(void *ctx, BSValue source, const char *rule) {
    Host *h = ctx; BSValueView *v = &h->nodes[source - 1].view;
    CHECK(!strcmp(rule, "ConstantFolding"));
    unsigned a = (unsigned)strtoul(h->nodes[v->left - 1].view.integer, NULL, 10);
    unsigned b = (unsigned)strtoul(h->nodes[v->right - 1].view.integer, NULL, 10);
    BSValue id = integer(h, a + b);
    if (h->wrong_type) h->nodes[id - 1].view.width = 16;
    return id;
}
static void trace(void *ctx, const BSTrace *event) {
    Host *h = ctx; ++h->traces;
    CHECK(event->step == h->traces);
    CHECK(event->before && event->after && event->rule_source.path);
}
static BSAdapter adapter(Host *h) {
    BSAdapter a = {0}; a.abi_version = BS_ABI_VERSION; a.context = h;
    a.inspect = inspect; a.rebuild = rebuild; a.fold = fold; a.trace = trace;
    return a;
}

static void registration(BSRegistry *registry) {
    BSError error = {0};
    BSConcern views[] = {{"Host::Temporary", BS_TERM}, {"HIR::Literals::Term", BS_TERM}};
    BSConcernModule m = {"Broken", views, 2, NULL, 0};
    CHECK(bs_concern_load(registry, &m, &error) == BS_INVALID);
    CHECK(!bs_concern_find(registry, "Host::Temporary"));
    m.view_count = 1;
    CHECK(bs_concern_load(registry, &m, &error) == BS_OK);
    CHECK(bs_concern_find(registry, "Host::Temporary")->sort == BS_TERM);
    BSOperator operators[] = {{"temporary", "integer", 0, 0}, {"plus", "integer", 0, 0}};
    m.views = NULL; m.view_count = 0; m.operators = operators; m.operator_count = 2;
    CHECK(bs_concern_load(registry, &m, &error) == BS_INVALID);
    CHECK(!bs_operator_find(registry, "temporary"));
    BSCapability caps[] = {{"Host::Temp", "Temp", 0, ""}, {"HIR::Effects::Pure", "Pure", 1, ""}};
    BSRequiresModule r = {"Broken", caps, 2};
    CHECK(bs_requires_load(registry, &r, &error) == BS_INVALID);
    CHECK(!bs_requires_find(registry, "Host::Temp"));
    CHECK(bs_loop_descriptor_module()->view_count == 2);
    CHECK(bs_concern_find(registry, "HIR::Loops::Descriptor")->sort == BS_TERM);
}

static void resolution(BSRegistry *registry) {
    BSError error = {0}; BSProfile *profile = NULL;
    BSDeclaration base[] = {{"ConstantFolding", LOCATION}};
    BSKernel kernels[3] = {kernel("A"), kernel("B"), kernel("Root")};
    kernels[0].rules = NULL; kernels[0].rule_count = 0; kernels[0].inherits = base; kernels[0].inherit_count = 1;
    kernels[1] = kernels[0]; kernels[1].name = "B";
    BSDeclaration diamond[] = {{"A", LOCATION}, {"B", LOCATION}};
    kernels[2].inherits = diamond; kernels[2].inherit_count = 2;
    CHECK(bs_profile_compile(registry, kernels, 3, "Root", &profile, &error) == BS_OK);
    CHECK(bs_profile_rule_count(profile) == 2);
    CHECK(!strcmp(bs_profile_rule_name(profile, 0), "ConstantFolding"));
    bs_profile_destroy(profile); profile = NULL;
    base[0].name = "Root";
    CHECK(bs_profile_compile(registry, kernels, 3, "Root", &profile, &error) == BS_INVALID);
    CHECK(!profile && strstr(error.message, "native-test.bst:12:3:") && strstr(error.message, "cycle"));
    base[0].name = "Missing";
    CHECK(bs_profile_compile(registry, kernels, 3, "Root", &profile, &error) == BS_INVALID);
    CHECK(strstr(error.message, "unknown inherited kernel"));
    kernels[0] = kernel("A"); kernels[1] = kernel("B");
    CHECK(bs_profile_compile(registry, kernels, 3, "Root", &profile, &error) == BS_INVALID);
    CHECK(strstr(error.message, "duplicate rule name"));
    BSKernel k = kernel("Rules");
    match[0].capture = 0;
    CHECK(bs_profile_compile(registry, &k, 1, "Rules", &profile, &error) == BS_INVALID);
    CHECK(strstr(error.message, "expected $1")); match[0].capture = 1;
    match[2].left = 2;
    CHECK(bs_profile_compile(registry, &k, 1, "Rules", &profile, &error) == BS_INVALID); match[2].left = 0;
    match[1].refinement = BS_SAME; match[1].same = 2;
    CHECK(bs_profile_compile(registry, &k, 1, "Rules", &profile, &error) == BS_INVALID);
    match[1].refinement = BS_EXACT; match[1].same = 0;
    match[2].text = "unknown";
    CHECK(bs_profile_compile(registry, &k, 1, "Rules", &profile, &error) == BS_INVALID); match[2].text = "plus";
    build[0].capture = 3;
    CHECK(bs_profile_compile(registry, &k, 1, "Rules", &profile, &error) == BS_INVALID); build[0].capture = 1;
    match[0].sort = (BSSort)99;
    CHECK(bs_profile_compile(registry, &k, 1, "Rules", &profile, &error) == BS_INVALID); match[0].sort = BS_TERM;
}

static void execution(BSProfile *profile) {
    BSError error = {0}; BSRewriteStats stats; BSValue out = 999;
    Host h = {0}; BSAdapter a = adapter(&h);
    BSValue x = input(&h), zero = integer(&h, 0), root = operation(&h, x, zero);
    CHECK(bs_optimize(profile, &a, root, NULL, &out, &stats, &error) == BS_OK);
    CHECK(out == x && h.traces == 1 && stats.rewrites == 1);
    h.traces = 0;
    root = operation(&h, integer(&h, 250), integer(&h, 6));
    BSValue whole = operation(&h, x, root);
    CHECK(bs_optimize(profile, &a, whole, NULL, &out, &stats, &error) == BS_OK);
    CHECK(out == x && stats.rewrites == 2 && h.traces == 2);
    h.traces = 0;
    h.nodes[whole - 1].view.flag_count = 1;
    CHECK(bs_optimize(profile, &a, whole, NULL, &out, &stats, &error) == BS_OK);
    CHECK(out != x && h.nodes[out - 1].view.flag_count == 1);
    BSRewriteOptions options = {0, BS_MAX_DEPTH}; h.traces = 0;
    CHECK(bs_optimize(profile, &a, root, &options, &out, &stats, &error) == BS_NONCONVERGENCE);
    CHECK(out == 0 && stats.rewrites == 0 && strstr(error.message, "ConstantFolding"));
    h.wrong_type = 1;
    CHECK(bs_optimize(profile, &a, root, NULL, &out, &stats, &error) == BS_ADAPTER_ERROR);
    CHECK(out == 0); h.wrong_type = 0;
    h.nodes[root - 1].view.left = root;
    CHECK(bs_optimize(profile, &a, root, NULL, &out, &stats, &error) == BS_INVALID);
    CHECK(out == 0 && strstr(error.message, "cyclic"));
    a.abi_version = 9;
    CHECK(bs_optimize(profile, &a, x, NULL, &out, &stats, &error) == BS_INVALID);
}

static void codec(BSRegistry *registry, BSProfile *profile) {
    BSError error = {0}; BSProfile *copy = NULL;
    FILE *file = tmpfile(); CHECK(file);
    CHECK(bs_profile_write(profile, file, &error) == BS_OK);
    long size = ftell(file); CHECK(size > 20);
    rewind(file);
    CHECK(bs_profile_read(registry, file, &copy, &error) == BS_OK);
    CHECK(bs_profile_rule_count(copy) == bs_profile_rule_count(profile));
    execution(copy); bs_profile_destroy(copy); copy = NULL;
    unsigned char *data = malloc((size_t)size); CHECK(data);
    rewind(file); CHECK(fread(data, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    /* Every truncated prefix must fail without publishing a partially read profile. */
    for (long i = 0; i < size; ++i) {
        file = tmpfile(); CHECK(file);
        CHECK(fwrite(data, 1, (size_t)i, file) == (size_t)i); rewind(file);
        CHECK(bs_profile_read(registry, file, &copy, &error) != BS_OK);
        CHECK(!copy); fclose(file);
    }
    /* Corrupt each byte in a valid file; accepted encodings still revalidate and
     * round-trip. This exercises count/string/enum/index bounds under sanitizers. */
    for (long i = 0; i < size; ++i) {
        data[i] ^= 0xff;
        file = tmpfile(); CHECK(file); CHECK(fwrite(data, 1, (size_t)size, file) == (size_t)size); rewind(file);
        BSStatus status = bs_profile_read(registry, file, &copy, &error);
        if (status == BS_OK) { CHECK(copy); bs_profile_destroy(copy); copy = NULL; }
        else CHECK(!copy);
        fclose(file); data[i] ^= 0xff;
    }
    free(data);
}

int main(void) {
    BSError error = {0}; CHECK(bs_abi_version() == BS_ABI_VERSION);
    BSRegistry *registry = bs_registry_create(&error); CHECK(registry);
    registration(registry); resolution(registry);
    BSDeclaration base[] = {{"ConstantFolding", LOCATION}};
    BSKernel k = kernel("Test"); k.inherits = base; k.inherit_count = 1;
    BSProfile *profile = NULL;
    CHECK(bs_profile_compile(registry, &k, 1, "Test", &profile, &error) == BS_OK);
    /* Compiled profiles own their IR; changing frontend storage has no effect. */
    match[2].text = "changed"; build[0].capture = 2;
    execution(profile); codec(registry, profile);
    bs_profile_destroy(profile); bs_registry_destroy(registry);
    puts("native registry, compiler, adapter, driver and codec checks passed");
    return 0;
}
