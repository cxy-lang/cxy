/*
 * Tests for the ownership graph commands of the trace library. The library source
 * is included so the graph can be built by hand and every command checked against
 * known shapes: a chain, a shared object, a cache, a leaked cycle.
 */
#include "stdlib/trace/mem.c"

// What a generated program provides, nothing here
uint32_t __cxy_mem_header_refs(void *ptr)
{
    (void)ptr;
    return 0;
}

const uint32_t __cxy_site_count = 0;
const SiteRecord *const __cxy_sites[] = {NULL};
const uint32_t __cxy_type_count = 0;
const TypeRecord *const __cxy_types[] = {NULL};
const uint32_t __cxy_root_count = 0;
const RootRecord __cxy_roots[] = {{NULL, NULL, 0, NULL}};

static int failures = 0;
static char out[1 << 16];

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CONTAINS(text) CHECK(strstr(out, text) != NULL)
#define MISSING(text) CHECK(strstr(out, text) == NULL)

// Runs `stmt` with `fd` writing to a scratch file and leaves the text in `out`
#define CAPTURE(stmt)                                                          \
    do {                                                                       \
        FILE *capture = tmpfile();                                             \
        int fd = fileno(capture);                                              \
        stmt;                                                                  \
        fseek(capture, 0, SEEK_SET);                                           \
        size_t got = fread(out, 1, sizeof(out) - 1, capture);                  \
        out[got] = '\0';                                                       \
        fclose(capture);                                                       \
    } while (0)

static Graph g;

static void reset(void)
{
    free(g.nodes);
    free(g.keys);
    free(g.edges);
    free(g.outStart);
    free(g.outEdge);
    free(g.inStart);
    free(g.inEdge);
    memset(&g, 0, sizeof(g));
}

static uint32_t object(uintptr_t addr, uint64_t size, uint32_t refs)
{
    return graphAddNode(&g, (GraphNode){.start = addr, .size = size, .refs = refs, .path = "test.cxy", .line = 1});
}

static uint32_t buffer(uintptr_t addr, uint64_t size)
{
    return graphAddNode(&g, (GraphNode){.start = addr, .size = size, .raw = true, .path = "test.cxy", .line = 2});
}

static uint32_t global(uintptr_t addr, const char *name)
{
    return graphAddNode(&g, (GraphNode){.start = addr, .size = 8, .name = name, .raw = true, .root = true});
}

static void edge(uint32_t from, uint32_t to)
{
    graphAddEdge(&g, from, to);
}

static void finish(void)
{
    g.keys = malloc(g.count * sizeof(GraphKey));
    for (size_t i = 0; i < g.count; i++)
        g.keys[i] = (GraphKey){.start = g.nodes[i].start, .id = (uint32_t)i};
    g.keyCount = g.count;
    qsort(g.keys, g.keyCount, sizeof(GraphKey), compareGraphKey);
    CHECK(graphIndex(&g));
}

static void testFind(void)
{
    reset();
    uint32_t a = object(0x1000, 32, 1), b = buffer(0x2000, 64);
    finish();
    CHECK(graphFind(&g, 0x1000) == a);
    CHECK(graphFind(&g, 0x2000) == b);
    CHECK(graphFind(&g, 0x1008) == GRAPH_NONE); // inside, not the start
    CHECK(graphFind(&g, 0x3000) == GRAPH_NONE);
}

// stack -> a -> b -> c: only `a` has a reference from outside the heap
static void testChain(void)
{
    reset();
    uint32_t a = object(0x1000, 16, 1), b = object(0x2000, 16, 1), c = object(0x3000, 16, 1);
    edge(a, b);
    edge(b, c);
    finish();

    CAPTURE(graphHolders(fd, &g, c));
    CONTAINS("held by");
    CONTAINS("0x2000");
    MISSING("externalRefs");

    CAPTURE(graphHolders(fd, &g, a));
    CONTAINS("not owned by any object");
    CONTAINS("externalRefs = 1");

    CAPTURE(graphPath(fd, &g, c));
    CONTAINS("retention path (2 holders deep)");
    CONTAINS("held from outside the heap");

    CAPTURE(graphContains(fd, &g, a, c));
    CONTAINS("yes, held through 2 step(s)");
    CAPTURE(graphContains(fd, &g, c, a));
    CONTAINS("no,");
}

// global R -> a -> b: the path starts at the named global
static void testNamedRoot(void)
{
    reset();
    uint32_t r = global(0x500, "registry");
    uint32_t a = object(0x1000, 16, 1), b = object(0x2000, 16, 1);
    edge(r, a);
    edge(a, b);
    finish();

    CAPTURE(graphPath(fd, &g, b));
    CONTAINS("global registry");
    MISSING("held from outside the heap");
    CAPTURE(graphHolders(fd, &g, a));
    CONTAINS("global registry");
    MISSING("externalRefs");
}

// Two globals share one object: `excl` leaves it out, `reach` counts it
static void testSharedObject(void)
{
    reset();
    uint32_t r1 = global(0x500, "first"), r2 = global(0x508, "second");
    uint32_t own1 = object(0x1000, 100, 1), own2 = object(0x2000, 200, 1);
    uint32_t shared = object(0x3000, 1000, 2);
    edge(r1, own1);
    edge(r1, shared);
    edge(r2, own2);
    edge(r2, shared);
    finish();

    CAPTURE(graphRetained(fd, &g, 10));
    char expected[256];
    snprintf(expected, sizeof(expected), "%12llu %10llu %12llu %10llu  global first", 100ull, 1ull, 1100ull, 2ull);
    CONTAINS(expected);
    snprintf(expected, sizeof(expected), "%12llu %10llu %12llu %10llu  global second", 200ull, 1ull, 1200ull, 2ull);
    CONTAINS(expected);
    CONTAINS("Total: 3 objects, 4 edges, 2 roots (2 hold other objects)");
}

// A cache: global -> map object -> buffer -> N keys. Keys are in the buffer only.
static void testCache(void)
{
    reset();
    uint32_t r = global(0x500, "cache");
    uint32_t map = object(0x1000, 32, 1), buf = buffer(0x2000, 800);
    edge(r, map);
    edge(map, buf);
    uint32_t keys[10];
    for (int i = 0; i < 10; i++) {
        keys[i] = object(0x10000 + i * 0x100, 50, 1);
        edge(buf, keys[i]);
    }
    finish();

    CAPTURE(graphRetained(fd, &g, 10));
    char expected[256];
    // 32 + 800 + 10 * 50 bytes, 12 nodes, all exclusively the cache's
    snprintf(expected, sizeof(expected), "%12llu %10llu %12llu %10llu  global cache", 1332ull, 12ull, 1332ull, 12ull);
    CONTAINS(expected);

    CAPTURE(graphContains(fd, &g, r, keys[7]));
    CONTAINS("yes, held through 3 step(s)");
    CAPTURE(graphPath(fd, &g, keys[3]));
    CONTAINS("global cache");
    CONTAINS("buffer");
}

// x <-> y with nothing else: leaked. With an outside reference on x: not leaked.
static void testCycles(void)
{
    reset();
    uint32_t x = object(0x1000, 40, 1), y = object(0x2000, 60, 1);
    edge(x, y);
    edge(y, x);
    finish();
    CAPTURE(graphCycles(fd, &g, 10));
    CONTAINS("cycle: 2 nodes, 100 bytes");
    CONTAINS("Total: 1 leaked cycles, 100 bytes");

    reset();
    x = object(0x1000, 40, 2); // one more reference than the cycle accounts for
    y = object(0x2000, 60, 1);
    edge(x, y);
    edge(y, x);
    finish();
    CAPTURE(graphCycles(fd, &g, 10));
    CONTAINS("Total: 0 leaked cycles, 0 bytes");

    // A cycle that something outside the group owns is not garbage either
    reset();
    uint32_t r = global(0x500, "keeper");
    x = object(0x1000, 40, 2);
    y = object(0x2000, 60, 1);
    edge(r, x);
    edge(x, y);
    edge(y, x);
    finish();
    CAPTURE(graphCycles(fd, &g, 10));
    CONTAINS("Total: 0 leaked cycles");

    // An object that owns itself
    reset();
    x = object(0x1000, 24, 1);
    edge(x, x);
    finish();
    CAPTURE(graphCycles(fd, &g, 10));
    CONTAINS("cycle: 1 nodes, 24 bytes");

    // A chain is not a cycle
    reset();
    x = object(0x1000, 8, 1);
    y = object(0x2000, 8, 1);
    edge(x, y);
    finish();
    CAPTURE(graphCycles(fd, &g, 10));
    CONTAINS("Total: 0 leaked cycles");
}

// Deep chains do not overflow the stack of the cycle search
static void testDeepChain(void)
{
    reset();
    uint32_t prev = object(0x1000, 8, 1);
    for (uintptr_t i = 1; i < 200000; i++) {
        uint32_t next = object(0x1000 + i * 16, 8, 1);
        edge(prev, next);
        prev = next;
    }
    edge(prev, 0); // closes one huge cycle
    finish();
    CAPTURE(graphCycles(fd, &g, 10));
    CONTAINS("cycle: 200000 nodes");
}

int main(void)
{
    testFind();
    testChain();
    testNamedRoot();
    testSharedObject();
    testCache();
    testCycles();
    testDeepChain();
    reset();

    if (failures == 0)
        printf("graph tests: ok\n");
    else
        printf("graph tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
