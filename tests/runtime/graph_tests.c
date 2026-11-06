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

#include "capture.h"

// The JSON of a node made by object(), buffer() and global() below
#define NODE_FIELDS(ptr, size, refs)                                           \
    "\"kind\":\"object\",\"ptr\":\"" ptr "\",\"size\":" #size ",\"name\":null,\"refs\":" #refs \
    ",\"type\":null,\"file\":\"test.cxy\",\"line\":1,\"gen\":0"
#define NODE(ptr, size, refs) "{" NODE_FIELDS(ptr, size, refs) "}"
#define BUFFER(ptr, size)                                                      \
    "{\"kind\":\"buffer\",\"ptr\":\"" ptr "\",\"size\":" #size                  \
    ",\"name\":null,\"refs\":null,\"type\":null,\"file\":\"test.cxy\",\"line\":2,\"gen\":0}"
#define GLOBAL(ptr, name)                                                      \
    "{\"kind\":\"global\",\"ptr\":\"" ptr "\",\"size\":8,\"name\":\"" name       \
    "\",\"refs\":null,\"type\":null,\"file\":null,\"line\":0,\"gen\":0}"
#define END ",\"approximate\":0}"

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

    CAPTURE(graphHolders(fd, &g, c, NULL));
    CONTAINS("held by");
    CONTAINS("0x2000");
    MISSING("externalRefs");

    CAPTURE(graphHolders(fd, &g, a, NULL));
    CONTAINS("not owned by any object");
    CONTAINS("externalRefs = 1");

    CAPTURE(graphPath(fd, &g, c, NULL));
    CONTAINS("retention path (2 holders deep)");
    CONTAINS("held from outside the heap");

    CAPTURE(graphContains(fd, &g, a, c, NULL));
    CONTAINS("yes, held through 2 step(s)");
    CAPTURE(graphContains(fd, &g, c, a, NULL));
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

    CAPTURE(graphPath(fd, &g, b, NULL));
    CONTAINS("global registry");
    MISSING("held from outside the heap");
    CAPTURE(graphHolders(fd, &g, a, NULL));
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

    CAPTURE(graphRetained(fd, &g, 10, NULL));
    char expected[256];
    snprintf(expected, sizeof(expected), "%12llu %10llu %12llu %10llu  global first", 100ull, 1ull, 1100ull, 2ull);
    CONTAINS(expected);
    snprintf(expected, sizeof(expected), "%12llu %10llu %12llu %10llu  global second", 200ull, 1ull, 1200ull, 2ull);
    CONTAINS(expected);
    CONTAINS("Total: 3 objects, 4 edges, 2 roots (2 hold other objects)");
}

// stack -> a -> b -> c, and a global -> x -> y, as JSON
static void testJsonPaths(void)
{
    reset();
    uint32_t a = object(0x1000, 16, 1), b = object(0x2000, 16, 1), c = object(0x3000, 16, 1);
    edge(a, b);
    edge(b, c);
    finish();

    CAPTURE_JSON(graphHolders(fd, &g, c, &w));
    EXPECT("{\"node\":" NODE("0x3000", 16, 1) ",\"holders\":[" NODE("0x2000", 16, 1)
           "],\"holderCount\":1,\"externalRefs\":0,\"inconsistent\":false" END);
    CAPTURE_JSON(graphHolders(fd, &g, a, &w));
    EXPECT("{\"node\":" NODE("0x1000", 16, 1) ",\"holders\":[],\"holderCount\":0,"
           "\"externalRefs\":1,\"inconsistent\":false" END);

    CAPTURE_JSON(graphPath(fd, &g, c, &w));
    EXPECT("{\"found\":true,\"reason\":null,\"depth\":2,\"heldFromOutside\":true,\"path\":["
           NODE("0x1000", 16, 1) "," NODE("0x2000", 16, 1) "," NODE("0x3000", 16, 1) "]" END);

    CAPTURE_JSON(graphContains(fd, &g, a, c, &w));
    EXPECT("{\"contains\":true,\"same\":false,\"steps\":2,\"path\":[" NODE("0x1000", 16, 1) ","
           NODE("0x2000", 16, 1) "," NODE("0x3000", 16, 1) "]" END);
    CAPTURE_JSON(graphContains(fd, &g, c, a, &w));
    EXPECT("{\"contains\":false,\"same\":false,\"steps\":0,\"path\":[]" END);
    CAPTURE_JSON(graphContains(fd, &g, a, a, &w));
    EXPECT("{\"contains\":false,\"same\":true,\"steps\":0,\"path\":[]" END);

    // A named root starts the path and nothing is held from outside
    reset();
    uint32_t r = global(0x500, "registry");
    a = object(0x1000, 16, 1);
    b = object(0x2000, 16, 1);
    edge(r, a);
    edge(a, b);
    finish();
    CAPTURE_JSON(graphPath(fd, &g, b, &w));
    EXPECT("{\"found\":true,\"reason\":null,\"depth\":2,\"heldFromOutside\":false,\"path\":["
           GLOBAL("0x500", "registry") "," NODE("0x1000", 16, 1) "," NODE("0x2000", 16, 1) "]" END);

    // Only cycles above it: no root to start from
    reset();
    a = object(0x1000, 16, 1);
    b = object(0x2000, 16, 1);
    edge(a, b);
    edge(b, a);
    finish();
    CAPTURE_JSON(graphPath(fd, &g, b, &w));
    EXPECT("{\"found\":false,\"reason\":\"cycle\",\"depth\":null,\"heldFromOutside\":null,\"path\":[]" END);

    // More edges into an object than it has references
    reset();
    a = object(0x1000, 16, 1);
    b = object(0x2000, 16, 1);
    c = object(0x3000, 16, 1);
    edge(a, c);
    edge(b, c);
    finish();
    CAPTURE_JSON(graphHolders(fd, &g, c, &w));
    EXPECT("{\"node\":" NODE("0x3000", 16, 1) ",\"holders\":[" NODE("0x1000", 16, 1) ","
           NODE("0x2000", 16, 1) "],\"holderCount\":2,\"externalRefs\":0,\"inconsistent\":true" END);

    // A buffer has no reference count
    reset();
    a = object(0x1000, 16, 1);
    uint32_t buf = buffer(0x2000, 64);
    edge(a, buf);
    finish();
    CAPTURE_JSON(graphHolders(fd, &g, buf, &w));
    EXPECT("{\"node\":" BUFFER("0x2000", 64) ",\"holders\":[" NODE("0x1000", 16, 1)
           "],\"holderCount\":1,\"externalRefs\":null,\"inconsistent\":false" END);
}

// a -> b, a -> c, b -> c: depth cut, a node shown twice, more children than listed
static void testJsonTree(void)
{
    reset();
    uint32_t a = object(0x1000, 16, 1), b = object(0x2000, 16, 1), c = object(0x3000, 16, 2);
    edge(a, b);
    edge(a, c);
    edge(b, c);
    finish();
    uint8_t seen[3] = {0};
    CAPTURE_JSON(jsonTreeNode(&w, &g, a, 0, 3, seen));
    EXPECT("{" NODE_FIELDS("0x1000", 16, 1)
           ",\"seen\":false,\"truncated\":false,\"moreChildren\":0,\"children\":["
           "{" NODE_FIELDS("0x2000", 16, 1)
           ",\"seen\":false,\"truncated\":false,\"moreChildren\":0,\"children\":["
           "{" NODE_FIELDS("0x3000", 16, 2)
           ",\"seen\":false,\"truncated\":false,\"moreChildren\":0,\"children\":[]}]},"
           "{" NODE_FIELDS("0x3000", 16, 2)
           ",\"seen\":true,\"truncated\":false,\"moreChildren\":0,\"children\":[]}]}");

    memset(seen, 0, sizeof(seen));
    CAPTURE_JSON(jsonTreeNode(&w, &g, b, 0, 0, seen));
    EXPECT("{" NODE_FIELDS("0x2000", 16, 1)
           ",\"seen\":false,\"truncated\":true,\"moreChildren\":0,\"children\":[]}");

    // 25 children: 20 are listed, 5 are counted
    reset();
    uint32_t parent = object(0x1000, 16, 1);
    for (int i = 0; i < 25; i++)
        edge(parent, object(0x10000 + i * 0x100, 8, 1));
    finish();
    uint8_t *many = calloc(g.count, 1);
    CAPTURE_JSON(jsonTreeNode(&w, &g, parent, 0, 3, many));
    CONTAINS("\"moreChildren\":5,\"children\":[");
    CHECK(strstr(out, "\"ptr\":\"0x10000\"") != NULL);
    CHECK(strstr(out, "\"ptr\":\"0x11300\"") != NULL); // the 20th child is the last one listed
    CHECK(strstr(out, "\"ptr\":\"0x11400\"") == NULL); // the 21st is not listed
    free(many);
}

// Objects with more references than edges into them are held from outside the heap
static void testJsonExternal(void)
{
    reset();
    uint32_t a = object(0x1000, 16, 3), b = object(0x2000, 16, 1), c = object(0x3000, 16, 1);
    uint32_t buf = buffer(0x4000, 64);
    edge(a, b);
    edge(a, buf);
    (void)c;
    finish();
    // a: 3 references, none from the heap (3 outside). b: 1 and 1 edge. c: 1 outside.
    // The buffer has no count.
    CAPTURE_JSON(jsonGraphExternal(&w, &g));
    EXPECT("{\"objects\":2,\"refs\":4,\"freshObjects\":0,\"freshRefs\":0}");
}

// What was made since the mark: nodes born after it. A named global is never new.
static void testSinceTheMark(void)
{
    markGen = 10;

    reset();
    uint32_t old = object(0x1000, 16, 3), young = object(0x2000, 16, 2);
    uint32_t buf = buffer(0x3000, 64);
    g.nodes[old].gen = 5;
    g.nodes[young].gen = 11;
    g.nodes[buf].gen = 12;
    finish();
    // both objects are held from outside: old by 3, young by 2, but only young is new
    CAPTURE_JSON(jsonGraphExternal(&w, &g));
    EXPECT("{\"objects\":2,\"refs\":5,\"freshObjects\":1,\"freshRefs\":2}");
    // the new buffer counts for the bytes, not for the objects
    CAPTURE_JSON(jsonGraphFresh(&w, &g));
    EXPECT("{\"objects\":1,\"bytes\":80}");

    // a global that holds a cache made before the mark, and keys made since
    reset();
    uint32_t r = global(0x500, "cache");
    uint32_t map = object(0x1000, 32, 1), arr = buffer(0x2000, 800);
    g.nodes[map].gen = 5;
    g.nodes[arr].gen = 5;
    edge(r, map);
    edge(map, arr);
    for (int i = 0; i < 10; i++) {
        uint32_t key = object(0x10000 + i * 0x100, 50, 1);
        g.nodes[key].gen = 20 + i;
        edge(arr, key);
    }
    finish();
    CAPTURE_JSON(graphRetained(fd, &g, 10, &w));
    CONTAINS("\"exclusiveBytes\":1332,");   // everything the cache keeps
    CONTAINS("\"exclusiveFreshBytes\":500,"); // the ten keys

    // a cycle with one node made since the mark is new, one that was all there is not
    reset();
    uint32_t x = object(0x1000, 40, 1), y = object(0x2000, 60, 1);
    g.nodes[x].gen = 5;
    g.nodes[y].gen = 20;
    edge(x, y);
    edge(y, x);
    finish();
    CAPTURE_JSON(graphCycles(fd, &g, 10, &w));
    CONTAINS("\"nodes\":2,\"bytes\":100,\"freshNodes\":1,");
    g.nodes[y].gen = 6;
    CAPTURE_JSON(graphCycles(fd, &g, 10, &w));
    CONTAINS("\"freshNodes\":0,");

    markGen = 0;
}

// Two globals, a leaked cycle, and a cap on what is listed
static void testJsonRetainedAndCycles(void)
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
    CAPTURE_JSON(graphRetained(fd, &g, 1, &w));
    // `second` keeps more of its own alive, so it comes first; `first` is cut off.
    // The shared object is excluded from both, so only own2 is a site of `second`.
    CONTAINS("{\"roots\":[{\"node\":" GLOBAL("0x508", "second") ",\"exclusiveBytes\":200,"
             "\"exclusiveObjects\":1,\"reachBytes\":1200,\"reachObjects\":2,\"exclusiveFreshBytes\":0,\"sites\":["
             "{\"file\":\"test.cxy\",\"line\":1,\"objects\":1,\"bytes\":200}]}],");
    CONTAINS("\"objects\":3,\"edges\":4,\"rootCount\":2,\"holding\":2,\"limited\":true,"
             "\"unrooted\":{\"objects\":0,\"bytes\":0,\"sites\":[]}" END);
    MISSING("\"name\":\"first\"");

    reset();
    CAPTURE_JSON(graphRetained(fd, &g, 10, &w));
    EXPECT("{\"roots\":[],\"objects\":0,\"edges\":0,\"rootCount\":0,\"holding\":0,\"limited\":false,"
           "\"unrooted\":{\"objects\":0,\"bytes\":0,\"sites\":[]}" END);

    // x <-> y and nothing else is leaked
    reset();
    uint32_t x = object(0x1000, 40, 1), y = object(0x2000, 60, 1);
    edge(x, y);
    edge(y, x);
    finish();
    CAPTURE_JSON(graphCycles(fd, &g, 10, &w));
    EXPECT("{\"cycles\":[{\"nodes\":2,\"bytes\":100,\"freshNodes\":0,\"moreMembers\":0,\"members\":["
           NODE("0x1000", 40, 1) "," NODE("0x2000", 60, 1) "]}],\"total\":1,\"bytes\":100,\"limited\":false" END);

    // A chain is not a cycle
    reset();
    x = object(0x1000, 8, 1);
    y = object(0x2000, 8, 1);
    edge(x, y);
    finish();
    CAPTURE_JSON(graphCycles(fd, &g, 10, &w));
    EXPECT("{\"cycles\":[],\"total\":0,\"bytes\":0,\"limited\":false" END);

    // A limit of 0 lists nothing and says so
    reset();
    x = object(0x1000, 24, 1);
    edge(x, x);
    finish();
    CAPTURE_JSON(graphCycles(fd, &g, 0, &w));
    EXPECT("{\"cycles\":[],\"total\":1,\"bytes\":24,\"limited\":true" END);
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

    CAPTURE(graphRetained(fd, &g, 10, NULL));
    char expected[256];
    // 32 + 800 + 10 * 50 bytes, 12 nodes, all exclusively the cache's
    snprintf(expected, sizeof(expected), "%12llu %10llu %12llu %10llu  global cache", 1332ull, 12ull, 1332ull, 12ull);
    CONTAINS(expected);

    CAPTURE(graphContains(fd, &g, r, keys[7], NULL));
    CONTAINS("yes, held through 3 step(s)");
    CAPTURE(graphPath(fd, &g, keys[3], NULL));
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
    CAPTURE(graphCycles(fd, &g, 10, NULL));
    CONTAINS("cycle: 2 nodes, 100 bytes");
    CONTAINS("Total: 1 leaked cycles, 100 bytes");

    reset();
    x = object(0x1000, 40, 2); // one more reference than the cycle accounts for
    y = object(0x2000, 60, 1);
    edge(x, y);
    edge(y, x);
    finish();
    CAPTURE(graphCycles(fd, &g, 10, NULL));
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
    CAPTURE(graphCycles(fd, &g, 10, NULL));
    CONTAINS("Total: 0 leaked cycles");

    // An object that owns itself
    reset();
    x = object(0x1000, 24, 1);
    edge(x, x);
    finish();
    CAPTURE(graphCycles(fd, &g, 10, NULL));
    CONTAINS("cycle: 1 nodes, 24 bytes");

    // A chain is not a cycle
    reset();
    x = object(0x1000, 8, 1);
    y = object(0x2000, 8, 1);
    edge(x, y);
    finish();
    CAPTURE(graphCycles(fd, &g, 10, NULL));
    CONTAINS("Total: 0 leaked cycles");
}

// The note on each line of the exit report says why a leaked block is alive
static void leakNoteOf(const ExitCtx *ctx, uintptr_t addr, char *buf, size_t size)
{
    LiveEntry entry = {.ptr = (void *)addr};
    leakNote(&entry, (void *)ctx, buf, size);
}

static void testLeakNotes(void)
{
    reset();
    uint32_t x = object(0x1000, 16, 1), y = object(0x2000, 16, 1);
    edge(x, y);
    edge(y, x);
    uint32_t w = object(0x4000, 16, 2); // two references nothing in the heap accounts for
    uint32_t buf = buffer(0x5000, 64);
    edge(w, buf);
    uint32_t s = object(0x6000, 16, 1);
    edge(s, s);
    finish();

    uint32_t *comp = malloc(g.count * sizeof(uint32_t)), compCount = 0;
    CHECK(graphComponents(&g, comp, &compCount));
    uint32_t *compSize = calloc(compCount, sizeof(uint32_t));
    for (uint32_t v = 0; v < g.count; v++)
        compSize[comp[v]]++;
    uint8_t reached[16] = {0}; // no named roots: nothing is owned from a global
    ExitCtx ctx = {.g = &g, .reached = reached, .comp = comp, .compSize = compSize};

    char note[256];
    leakNoteOf(&ctx, 0x1000, note, sizeof(note));
    CHECK(strstr(note, "cycle of 2 nodes, held by 0x2000") != NULL);
    leakNoteOf(&ctx, 0x4000, note, sizeof(note));
    CHECK(strstr(note, "no owner, 2 reference(s) held outside the heap") != NULL);
    leakNoteOf(&ctx, 0x5000, note, sizeof(note));
    CHECK(strstr(note, "owned by the leaked object 0x4000") != NULL);
    leakNoteOf(&ctx, 0x6000, note, sizeof(note));
    CHECK(strstr(note, "owns itself") != NULL);
    leakNoteOf(&ctx, 0x9999, note, sizeof(note)); // not in the graph
    CHECK(note[0] == '\0');

    free(comp);
    free(compSize);
    (void)buf;
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
    CAPTURE(graphCycles(fd, &g, 10, NULL));
    CONTAINS("cycle: 200000 nodes");
}

// A thread that allocates and frees in a loop must stop at the pause
static volatile int stopWorker;
static volatile uint64_t workDone;

static void *hookWorker(void *arg)
{
    (void)arg;
    TraceRec rec;
    memset(&rec, 0, sizeof(rec));
    while (!stopWorker) {
        __cxy_mem_trace_register(&rec);
        __cxy_mem_trace_unregister(&rec);
        workDone++;
    }
    return NULL;
}

// Hooks once, then computes for a while without ever reaching a safepoint
static void *busyWorker(void *arg)
{
    (void)arg;
    TraceRec rec;
    memset(&rec, 0, sizeof(rec));
    __cxy_mem_trace_register(&rec);
    uint64_t until = nowNs() + 150000000ull;
    while (nowNs() < until && !stopWorker)
        ;
    __cxy_mem_trace_unregister(&rec);
    return NULL;
}

static void testPause(void)
{
    started = true;
    CHECK(pauseThreads() == 0); // nothing has allocated yet: nothing to wait for
    resumeThreads();

    pthread_t t;
    stopWorker = 0;
    CHECK(pthread_create(&t, NULL, hookWorker, NULL) == 0);
    usleep(20000);
    CHECK(workDone > 0);

    CHECK(pauseThreads() == 0);
    uint64_t atPause = workDone;
    usleep(60000);
    CHECK(workDone == atPause); // parked: no progress while paused
    resumeThreads();
    usleep(60000);
    CHECK(workDone > atPause);

    stopWorker = 1;
    pthread_join(t, NULL);

    // After the thread is gone and idle, a pause returns at once
    uint64_t begin = nowNs();
    CHECK(pauseThreads() == 0);
    resumeThreads();
    CHECK(nowNs() - begin < 100000000ull);
}

static void testPauseApproximate(void)
{
    // A recently active thread that does not reach a safepoint in time is counted
    setenv("CXY_MEM_PAUSE_MS", "5", 1);
    pthread_t t;
    stopWorker = 0;
    CHECK(pthread_create(&t, NULL, busyWorker, NULL) == 0);
    usleep(5000);
    CHECK(pauseThreads() == 1);
    resumeThreads();
    stopWorker = 1;
    pthread_join(t, NULL);
    unsetenv("CXY_MEM_PAUSE_MS");
}

int main(void)
{
    testPause();
    testPauseApproximate();
    testFind();
    testChain();
    testNamedRoot();
    testJsonPaths();
    testJsonTree();
    testJsonExternal();
    testSinceTheMark();
    testJsonRetainedAndCycles();
    testSharedObject();
    testCache();
    testCycles();
    testLeakNotes();
    testDeepChain();
    reset();

    if (failures == 0)
        printf("graph tests: ok\n");
    else
        printf("graph tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
