/*
 * Unit tests for the runtime memory manager (src/cxy/runtime/mm.h). The runtime
 * is compiled exactly as generated programs see it: prologue.h then mm.h, with no
 * system headers. Built twice by CMake, with and without NDEBUG.
 */
#include "prologue.h"
#include "mm.h"

extern int pthread_create(void *thread, const void *attr, void *(*fn)(void *), void *arg);
extern int pthread_join(void *thread, void **ret);

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define THREADS 4
#define ITERATIONS 100000

static int dtorCalls;
static int faultCount;
static int lastFault;
static int order[4];
static int orderPos;

static void countingDtor(void *obj)
{
    (void)obj;
    __atomic_fetch_add(&dtorCalls, 1, __ATOMIC_RELAXED);
}

static void onFault(int kind, void *obj)
{
    (void)obj;
    faultCount++;
    lastFault = kind;
}

static void resetState(void)
{
    dtorCalls = 0;
    faultCount = 0;
    lastFault = 0;
    orderPos = 0;
    __mm_fault_hook = onFault;
}

static void testNull(void)
{
    CHECK(__smart_ptr_get(nullptr) == nullptr);
    CHECK(__smart_ptr_drop(nullptr) == false);
}

static void testLifecycle(void)
{
    resetState();
    void *obj = __smart_ptr_alloc(32, countingDtor);
    CHECK(obj != nullptr);
    CHECK(((unsigned long)obj & 15) == 0);
    CHECK(__MM_HEADER(obj)->strong == 1);
    CHECK(__smart_ptr_get(obj) == obj);
    CHECK(__MM_HEADER(obj)->strong == 2);
    CHECK(__smart_ptr_drop(obj) == false);
    CHECK(dtorCalls == 0);
    CHECK(__smart_ptr_drop(obj) == true);
    CHECK(dtorCalls == 1);
    CHECK(faultCount == 0);
}

static void deinitFn(void *obj)
{
    (void)obj;
    order[orderPos++] = 1;
}

static void dropFieldsFn(void *obj)
{
    (void)obj;
    order[orderPos++] = 2;
}

static const __TypeInfo testType = {
    .name = "Test", .size = 24, .align = 8, .deinit = deinitFn, .dropFields = dropFieldsFn};

static void testTypedAlloc(void)
{
    resetState();
    unsigned char *obj = __mm_alloc(&testType, nullptr);
    CHECK(obj != nullptr);
    for (int i = 0; i < 24; i++)
        CHECK(obj[i] == 0);
    CHECK(__MM_HEADER(obj)->type == &testType);
    CHECK(__mm_release(obj) == true);
    CHECK(orderPos == 2 && order[0] == 1 && order[1] == 2);
}

static void testOverflowSaturates(void)
{
    resetState();
    void *obj = __smart_ptr_alloc(16, countingDtor);
    __ObjHeader *h = __MM_HEADER(obj);

    h->strong = __MM_RC_LIMIT - 1;
    __smart_ptr_get(obj);
    CHECK(h->strong == __MM_RC_LIMIT);
    CHECK(faultCount == 0);

    __smart_ptr_get(obj);
    CHECK(faultCount == 1 && lastFault == __MM_FAULT_OVERFLOW);
    CHECK(h->strong == __MM_RC_SATURATED);

    // A saturated object is never freed and its count does not drift down
    for (int i = 0; i < 1000; i++)
        CHECK(__smart_ptr_drop(obj) == false);
    CHECK(dtorCalls == 0);
    CHECK(h->strong >= __MM_RC_LIMIT);
    h->strong = __MM_RC_SATURATED + 5;
    __smart_ptr_drop(obj);
    CHECK(h->strong == __MM_RC_SATURATED);

    h->strong = 1;
    CHECK(__smart_ptr_drop(obj) == true);
}

static void testUnderflowSaturates(void)
{
    resetState();
    void *obj = __smart_ptr_alloc(16, countingDtor);
    __ObjHeader *h = __MM_HEADER(obj);

    h->strong = 0; // as if released once too often
    CHECK(__smart_ptr_drop(obj) == false);
    CHECK(faultCount == 1 && lastFault == __MM_FAULT_UNDERFLOW);
    CHECK(h->strong == __MM_RC_SATURATED);
    CHECK(dtorCalls == 0);

    h->strong = 1;
    CHECK(__smart_ptr_drop(obj) == true);
}

#ifndef NDEBUG
static void *dyingObj;

static void resurrectingDtor(void *obj)
{
    (void)obj;
    __smart_ptr_get(dyingObj);
}

static void testRetainDying(void)
{
    resetState();
    dyingObj = __smart_ptr_alloc(16, resurrectingDtor);
    __smart_ptr_drop(dyingObj);
    CHECK(faultCount == 1 && lastFault == __MM_FAULT_RETAIN_DYING);
}

#if !defined(__MM_ASAN)
// A borrow of a destroyed object is caught where it is loaded, a live one is not
static void testDanglingBorrow(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(32, countingDtor);
    CHECK(__mm_ref(obj, "t.cxy", 1) == obj);
    CHECK(faultCount == 0);
    CHECK(__mm_ref((unsigned char *)nullptr, "t.cxy", 2) == nullptr);
    CHECK(faultCount == 0);

    __smart_ptr_drop(obj);
    __mm_ref(obj, "t.cxy", 3);
    CHECK(faultCount == 1 && lastFault == __MM_FAULT_DANGLING_REF);
}

static void testPoisonAndQuarantine(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(64, countingDtor);
    __ObjHeader *h = __MM_HEADER(obj);
    __smart_ptr_drop(obj);

    // Still readable: the block waits in the quarantine, poisoned
    CHECK(h->weakFlags == __MM_DEAD);
    CHECK(obj[0] == 0xDD && obj[63] == 0xDD);

    // Cycling through the quarantine releases old blocks without trouble
    for (int i = 0; i < __MM_QUARANTINE_SLOTS * 3; i++)
        __smart_ptr_drop(__smart_ptr_alloc(16, countingDtor));
    CHECK(dtorCalls == 1 + __MM_QUARANTINE_SLOTS * 3);
}
#endif
#endif

/* ------------------------------------------------------------------- weak */

#define WEAK_COUNT(obj) (__MM_HEADER(obj)->weakFlags >> __MM_WEAK_SHIFT)

// While the object lives, upgrade gives a new owner and counts it
static void testWeakUpgradeLive(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(32, countingDtor);
    CHECK(WEAK_COUNT(obj) == 1); // the implicit one the owners hold together

    CHECK(__mm_weak_retain(obj) == obj);
    CHECK(WEAK_COUNT(obj) == 2);
    CHECK(__mm_upgrade(obj) == obj);
    CHECK(__MM_HEADER(obj)->strong == 2);

    CHECK(__smart_ptr_drop(obj) == false);
    __mm_weak_release(obj);
    CHECK(WEAK_COUNT(obj) == 1);
    CHECK(__smart_ptr_drop(obj) == true);
    CHECK(dtorCalls == 1);
    CHECK(faultCount == 0);
}

// After the last owner the object is destroyed but the block stays for the weak
// references: upgrade fails, the block is freed with the last weak release
static void testWeakZombie(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(32, countingDtor);
    __mm_weak_retain(obj);
    __mm_weak_retain(obj);

    CHECK(__smart_ptr_drop(obj) == true);
    CHECK(dtorCalls == 1);
    CHECK(__MM_HEADER(obj)->strong == 0);
    CHECK(__MM_HEADER(obj)->weakFlags & __MM_F_DYING);
    CHECK(WEAK_COUNT(obj) == 2);
    CHECK(__mm_upgrade(obj) == nullptr);
    CHECK(__MM_HEADER(obj)->strong == 0);

    __mm_weak_release(obj);
    CHECK(WEAK_COUNT(obj) == 1);
    CHECK(dtorCalls == 1);
    __mm_weak_release(obj);
#if !defined(NDEBUG) && !defined(__MM_ASAN)
    CHECK(__MM_HEADER(obj)->weakFlags == __MM_DEAD); // freed, waiting in quarantine
#endif
    CHECK(faultCount == 0);
}

// With no weak reference the block is freed with the last owner, as before
static void testWeakNoneFreesAtOnce(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(32, countingDtor);
    __ObjHeader *h = __MM_HEADER(obj);
    CHECK(__smart_ptr_drop(obj) == true);
    CHECK(dtorCalls == 1);
#if !defined(NDEBUG) && !defined(__MM_ASAN)
    CHECK(h->weakFlags == __MM_DEAD);
#endif
    (void)h;
}

static void testWeakNull(void)
{
    resetState();
    CHECK(__mm_weak_retain(nullptr) == nullptr);
    CHECK(__mm_upgrade(nullptr) == nullptr);
    __mm_weak_release(nullptr);
    CHECK(faultCount == 0);
}

// A weak release that would give up the owners' own count is a bug
static void testWeakUnderflow(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(32, countingDtor);
    __mm_weak_release(obj);
    CHECK(faultCount == 1 && lastFault == __MM_FAULT_WEAK_UNDERFLOW);
    CHECK(dtorCalls == 0);
    CHECK(__MM_HEADER(obj)->strong == 1);
    __smart_ptr_drop(obj);
}

static void testWeakOverflowSaturates(void)
{
    resetState();
    unsigned char *obj = __smart_ptr_alloc(32, countingDtor);
    __atomic_store_n(&__MM_HEADER(obj)->weakFlags,
                     (__MM_HEADER(obj)->weakFlags & 0xFFU) | (__MM_WEAK_LIMIT << __MM_WEAK_SHIFT),
                     __ATOMIC_RELAXED);
    __mm_weak_retain(obj);
    CHECK(faultCount == 1 && lastFault == __MM_FAULT_WEAK_OVERFLOW);
    CHECK(WEAK_COUNT(obj) == __MM_WEAK_MAX);
    // saturated: nothing brings it to zero, the block is never freed
    __smart_ptr_drop(obj);
    __mm_weak_release(obj);
    CHECK(WEAK_COUNT(obj) == __MM_WEAK_MAX);
}

static void *weakWorker(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        void *strong = __mm_upgrade(arg);
        if (strong != nullptr)
            __smart_ptr_drop(strong);
    }
    __mm_weak_release(arg);
    return nullptr;
}

// Threads upgrade while the last owner goes away: the object is destroyed exactly
// once, nothing is freed twice and the block outlives every weak reference
static void testConcurrentUpgrade(void)
{
    for (int round = 0; round < 50; round++) {
        resetState();
        void *obj = __smart_ptr_alloc(16, countingDtor);
        void *threads[THREADS];
        for (int i = 0; i < THREADS; i++)
            __mm_weak_retain(obj);
        for (int i = 0; i < THREADS; i++)
            CHECK(pthread_create(&threads[i], nullptr, weakWorker, obj) == 0);
        __smart_ptr_drop(obj); // the creator's own reference
        for (int i = 0; i < THREADS; i++)
            pthread_join(threads[i], nullptr);
        CHECK(dtorCalls == 1);
        CHECK(faultCount == 0);
    }
}

static void testBuffers(void)
{
    unsigned char *zeroed = __calloc_buffer(64);
    CHECK(zeroed != nullptr);
    for (int i = 0; i < 64; i++)
        CHECK(zeroed[i] == 0);

    unsigned char *grown = __alloc_buffer(8);
    memset(grown, 0xAB, 8);
    grown = __realloc_buffer(grown, 4096);
    CHECK(grown != nullptr);
    for (int i = 0; i < 8; i++)
        CHECK(grown[i] == 0xAB);

    __free_buffer(zeroed);
    __free_buffer(grown);
    __free_buffer(nullptr);
}

static void *retainReleaseWorker(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        __smart_ptr_get(arg);
        __smart_ptr_drop(arg);
    }
    return nullptr;
}

static void testConcurrentCounts(void)
{
    resetState();
    void *obj = __smart_ptr_alloc(16, countingDtor);
    void *threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        CHECK(pthread_create(&threads[i], nullptr, retainReleaseWorker, obj) == 0);
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], nullptr);
    CHECK(__MM_HEADER(obj)->strong == 1);
    CHECK(dtorCalls == 0);
    CHECK(__smart_ptr_drop(obj) == true);
    CHECK(dtorCalls == 1);
}

// Each thread owns one reference: exactly one release destroys the object
static void *lastOwnerWorker(void *arg)
{
    retainReleaseWorker(arg);
    __smart_ptr_drop(arg);
    return nullptr;
}

static void testConcurrentLastRelease(void)
{
    resetState();
    void *obj = __smart_ptr_alloc(16, countingDtor);
    void *threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        __smart_ptr_get(obj);
    for (int i = 0; i < THREADS; i++)
        CHECK(pthread_create(&threads[i], nullptr, lastOwnerWorker, obj) == 0);
    __smart_ptr_drop(obj); // the creator's own reference
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], nullptr);
    CHECK(dtorCalls == 1);
    CHECK(faultCount == 0);
}

static void *allocFreeWorker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERATIONS / 10; i++)
        __smart_ptr_drop(__smart_ptr_alloc(48, countingDtor));
    return nullptr;
}

static void testConcurrentAllocFree(void)
{
    resetState();
    void *threads[THREADS];
    for (int i = 0; i < THREADS; i++)
        CHECK(pthread_create(&threads[i], nullptr, allocFreeWorker, nullptr) == 0);
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], nullptr);
    CHECK(dtorCalls == THREADS * (ITERATIONS / 10));
}

int main(void)
{
    testNull();
    testLifecycle();
    testTypedAlloc();
    testOverflowSaturates();
    testUnderflowSaturates();
    testBuffers();
    testWeakUpgradeLive();
    testWeakZombie();
    testWeakNoneFreesAtOnce();
    testWeakNull();
    testWeakUnderflow();
    testWeakOverflowSaturates();
#ifndef NDEBUG
    testRetainDying();
#if !defined(__MM_ASAN)
    testPoisonAndQuarantine();
    testDanglingBorrow();
#endif
#endif
    testConcurrentCounts();
    testConcurrentLastRelease();
    testConcurrentAllocFree();
    testConcurrentUpgrade();

    if (failures == 0)
        printf("runtime rc tests: ok\n");
    else
        printf("runtime rc tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
