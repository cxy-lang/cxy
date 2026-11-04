/*
 * Unit tests for the trace registry (runtime/mm.h in a trace build plus
 * stdlib/trace/mem.c): registration, cross-thread frees, buffers, counters.
 * Compiled like a generated program, linked with the trace library.
 */
#define ENABLE_MEMORY_TRACE 1
#include "prologue.h"
#include "mm.h"

extern int pthread_create(void *thread, const void *attr, void *(*fn)(void *), void *arg);
extern int pthread_join(void *thread, void **ret);
extern void __cxy_mem_trace_stats(uint64_t out[4]);

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define THREADS 4
#define ITERATIONS 20000

static const __TypeInfo testType = {.name = "Test", .size = 24, .align = 8, .index = 0};
static const __Site testSite = {.file = "trace_tests.c", .line = 1, .column = 1, .function = "test", .index = 0};

// The tables a generated program provides for names and per-site counters
const __Site *const __cxy_sites[] = {&testSite, nullptr};
const uint32_t __cxy_site_count = 1;
const __TypeInfo *const __cxy_types[] = {&testType, nullptr};
const uint32_t __cxy_type_count = 1;

static uint64_t live[4];

static void stats(void)
{
    __cxy_mem_trace_stats(live);
}

static void testBeforeStart(void)
{
    // Not started: blocks work, nothing is registered
    void *obj = __mm_alloc(&testType, &testSite);
    void *buf = __alloc_buffer_trace(64, &testSite);
    stats();
    CHECK(live[0] == 0 && live[2] == 0);
    __mm_release(obj);
    __free_buffer(buf);
    stats();
    CHECK(live[0] == 0 && live[2] == 0);
}

static void testObjects(void)
{
    void *a = __mm_alloc_trace(&testType, &testSite);
    void *b = __mm_alloc(&testType, &testSite);
    stats();
    CHECK(live[0] == 2 && live[1] == 48);
    CHECK(live[2] == 0);
    CHECK(((unsigned long)a & 15) == 0);

    __mm_release(a);
    stats();
    CHECK(live[0] == 1 && live[1] == 24);
    __mm_release(b);
    stats();
    CHECK(live[0] == 0 && live[1] == 0);
}

static void testLegacyObjects(void)
{
    // Objects from the old entry point have no site but are registered too
    void *obj = __smart_ptr_alloc(32, nullptr);
    stats();
    CHECK(live[0] == 1 && live[1] == 32);
    __smart_ptr_drop(obj);
    stats();
    CHECK(live[0] == 0);
}

static void testBuffers(void)
{
    unsigned char *zeroed = __calloc_buffer_trace(100, &testSite);
    CHECK(zeroed != nullptr);
    for (int i = 0; i < 100; i++)
        CHECK(zeroed[i] == 0);

    unsigned char *grown = __alloc_buffer_trace(8, &testSite);
    memset(grown, 0xAB, 8);
    stats();
    CHECK(live[2] == 2 && live[3] == 108);

    grown = __realloc_buffer_trace(grown, 4096, &testSite);
    CHECK(grown != nullptr);
    for (int i = 0; i < 8; i++)
        CHECK(grown[i] == 0xAB);
    stats();
    CHECK(live[2] == 2 && live[3] == 4196);

    unsigned char *fromNull = __realloc_buffer_trace(nullptr, 16, &testSite);
    stats();
    CHECK(live[2] == 3 && live[3] == 4212);

    __free_buffer(zeroed);
    __free_buffer(grown);
    __free_buffer(fromNull);
    __free_buffer(nullptr);
    stats();
    CHECK(live[2] == 0 && live[3] == 0);
    CHECK(live[0] == 0);
}

// Allocated on one thread, released on the main thread
static void *allocWorker(void *arg)
{
    void **slots = arg;
    for (int i = 0; i < 100; i++) {
        slots[i] = __mm_alloc_trace(&testType, &testSite);
        slots[100 + i] = __alloc_buffer_trace(32, &testSite);
    }
    return nullptr;
}

static void testCrossThreadFree(void)
{
    static void *slots[THREADS][200];
    void *threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        CHECK(pthread_create(&threads[i], nullptr, allocWorker, slots[i]) == 0);
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], nullptr);

    stats();
    CHECK(live[0] == THREADS * 100 && live[2] == THREADS * 100);
    CHECK(live[1] == THREADS * 100 * 24 && live[3] == THREADS * 100 * 32);

    for (int i = 0; i < THREADS; i++) {
        for (int j = 0; j < 100; j++) {
            __mm_release(slots[i][j]);
            __free_buffer(slots[i][100 + j]);
        }
    }
    stats();
    CHECK(live[0] == 0 && live[2] == 0 && live[1] == 0 && live[3] == 0);
}

static void *churnWorker(void *arg)
{
    (void)arg;
    unsigned char *buf = nullptr;
    for (int i = 0; i < ITERATIONS; i++) {
        __mm_release(__mm_alloc_trace(&testType, &testSite));
        buf = __realloc_buffer_trace(buf, 16 + (i % 64) * 8, &testSite);
        if (i % 7 == 0) {
            __free_buffer(buf);
            buf = nullptr;
        }
    }
    __free_buffer(buf);
    return nullptr;
}

static void testConcurrentChurn(void)
{
    void *threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        CHECK(pthread_create(&threads[i], nullptr, churnWorker, nullptr) == 0);
    // Totals are read while the threads churn, they only have to be consistent
    for (int i = 0; i < 200; i++)
        stats();
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], nullptr);
    stats();
    CHECK(live[0] == 0 && live[1] == 0 && live[2] == 0 && live[3] == 0);
}

static int edgeCount;

static void countEdge(__Visitor *v, const void *from, const void *to)
{
    (void)v;
    (void)from;
    (void)to;
    edgeCount++;
}

static void testVisitEdge(void)
{
    __Visitor v = {.edge = countEdge, .ctx = nullptr};
    __visit_edge(&v, (void *)16, (void *)32);
    __visit_edge(&v, (void *)16, nullptr); // null targets are not edges
    CHECK(edgeCount == 1);
}

int main(void)
{
    testBeforeStart();
    __cxy_mem_trace_start();
    testVisitEdge();
    testObjects();
    testLegacyObjects();
    testBuffers();
    testCrossThreadFree();
    testConcurrentChurn();

    if (failures == 0)
        printf("trace tests: ok\n");
    else
        printf("trace tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
