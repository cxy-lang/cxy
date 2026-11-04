#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <execinfo.h>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

/*
 * Memory tracing runtime. The generated program (prologue.h) only calls the
 * __cxy_mem_trace_* hooks below; all trace state lives here.
 *
 * Live queries: a background thread serves one-line commands on a Unix
 * socket (default /tmp/cxy-mem-<pid>.sock), e.g.
 *     echo sites | nc -U /tmp/cxy-mem-1234.sock
 * CXY_MEM_SOCKET=0 disables it, CXY_MEM_SOCKET=<path> picks the path.
 */

#define CXY_TRACE_RING_SIZE 1024
#define CXY_TRACE_STACK_DEPTH 8
// Frames 0/1 are the trace hook and the __smart_ptr_*_trace wrapper
#define CXY_TRACE_SKIP_FRAMES 2
#define CXY_QUERY_DEFAULT_LIMIT 20

typedef struct {
    void *ptr;
    const char *op;
    const char *path;
    uint64_t line;
    uint32_t refs;
    void *stack[CXY_TRACE_STACK_DEPTH];
    int stackSize;
} TraceEvent;

typedef struct {
    void *ptr; // payload address
    uint64_t size;
    const char *path;
    uint64_t line;
    uint64_t gen; // birth tick, for most-recent-first reports and `mark`
    bool raw;     // unused until the exact graph replaces the scan
    bool buffer;  // container storage from __alloc_buffer, listed like an object
} LiveEntry;

typedef struct {
    const char *path;
    uint64_t line;
    uint64_t count;
    uint64_t bytes;
} SiteEntry;

// Records emitted by the generated program, they mirror __Site and __TypeInfo in
// runtime/mm.h
typedef struct {
    const char *file;
    uint32_t line, column;
    const char *function;
    uint32_t index;
} SiteRecord;

typedef struct {
    const char *name;
    uint64_t size;
    uint32_t align, kind;
    void *deinit, *dropFields, *visit;
    uint32_t index;
} TypeRecord;

/*
 * Registry. Every object and buffer carries a TraceRec in front of it (mirrors
 * __TraceHeader in runtime/mm.h). Entries live on the list of the thread that
 * allocated them, each list has its own lock, so allocating threads never contend
 * and a free on another thread only locks the owner's list.
 */
#define KIND_OBJECT 0
#define KIND_BUFFER 1

struct TraceList;
typedef struct __attribute__((aligned(16))) TraceRec {
    const SiteRecord *site;
    uint64_t birthTick;
    struct TraceRec *prev, *next;
    struct TraceList *list;
    uint64_t size;
    uint32_t kind;
    uint32_t reserved;
    uint64_t stackId;
} TraceRec;

_Static_assert(sizeof(TraceRec) == 64, "must mirror __TraceHeader");

// The object header that follows a TraceRec is 16 bytes
#define OBJ_HEADER_SIZE 16

typedef struct TraceList {
    pthread_mutex_t lock;
    TraceRec *head;
    struct TraceList *next; // every list, push-only: a finished thread keeps its entries
    uint64_t objects, objectBytes;
    uint64_t buffers, bufferBytes;
    uint64_t objectAllocs; // objects registered since start
} TraceList;

static TraceEvent ring[CXY_TRACE_RING_SIZE];
static uint32_t ringIdx = 0;

static __thread TraceList *threadList;
static TraceList *allLists;
static pthread_mutex_t listsLock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t tick;      // allocation clock, entries are stamped with it
static uint64_t startTick; // entries older than this are static, never reported

static bool started = false;

static uint64_t markGen = 0;
static char socketPath[sizeof(((struct sockaddr_un *)0)->sun_path)];

// Defined in the generated program; weak so this library links standalone
__attribute__((weak)) extern uint32_t __cxy_mem_header_refs(void *ptr);

/* ---------------------------------------------------------------- symbols */

static void symbolizeStackFd(int fd, void *const *stack, int count)
{
    if (count <= 0 || stack == NULL)
        return;

#if defined(__APPLE__)
    Dl_info info;
    if (dladdr(stack[0], &info) && info.dli_fname != NULL) {
        char cmd[1024];
        int pos = snprintf(cmd, sizeof(cmd), "atos -o \"%s\" -l %p", info.dli_fname, info.dli_fbase);
        for (int i = 0; i < count && pos < (int)sizeof(cmd) - 30; i++) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %p", stack[i]);
        }
        FILE *pipe = popen(cmd, "r");
        if (pipe != NULL) {
            char line[256];
            int frame = 0;
            while (fgets(line, sizeof(line), pipe) != NULL) {
                dprintf(fd, "    #%-2d %s", frame++, line);
            }
            pclose(pipe);
            return;
        }
    }
#elif defined(__linux__)
    Dl_info info;
    if (dladdr(stack[0], &info) && info.dli_fname != NULL) {
        char cmd[1024];
        int pos = snprintf(cmd, sizeof(cmd), "addr2line -e \"%s\" -f -C", info.dli_fname);
        for (int i = 0; i < count && pos < (int)sizeof(cmd) - 30; i++) {
            pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %p", stack[i]);
        }
        FILE *pipe = popen(cmd, "r");
        if (pipe != NULL) {
            char func[128], fileline[256];
            int frame = 0;
            while (fgets(func, sizeof(func), pipe) != NULL && fgets(fileline, sizeof(fileline), pipe) != NULL) {
                func[strcspn(func, "\r\n")] = 0;
                dprintf(fd, "    #%-2d %s @ %s", frame++, func, fileline);
            }
            pclose(pipe);
            return;
        }
    }
#endif

    backtrace_symbols_fd((void *const *)stack, count, fd);
}

void __cxy_symbolize_stack(void *const *stack, int count)
{
    fflush(stdout);
    symbolizeStackFd(STDOUT_FILENO, stack, count);
}

static void printFrames(int fd, void *const *stack, int count, bool sym)
{
    if (count <= 0)
        return;
    if (sym) {
        symbolizeStackFd(fd, stack, count);
        return;
    }
    for (int i = 0; i < count; i++)
        dprintf(fd, "    #%-2d %p\n", i, stack[i]);
}

static void printEventStack(int fd, const TraceEvent *ev, bool sym)
{
    if (ev->stackSize > CXY_TRACE_SKIP_FRAMES)
        printFrames(fd, ev->stack + CXY_TRACE_SKIP_FRAMES, ev->stackSize - CXY_TRACE_SKIP_FRAMES, sym);
    else
        printFrames(fd, ev->stack, ev->stackSize, sym);
}

/* ------------------------------------------------------------- live table */

static inline size_t liveHash(const void *ptr)
{
    uint64_t h = (uint64_t)(uintptr_t)ptr >> 4;
    h *= 11400714819323198485ull;
    return (size_t)(h >> 16);
}

static TraceList *threadListGet(void)
{
    TraceList *list = threadList;
    if (list != NULL)
        return list;
    list = calloc(1, sizeof(TraceList));
    if (list == NULL)
        return NULL;
    pthread_mutex_init(&list->lock, NULL);
    pthread_mutex_lock(&listsLock);
    list->next = allLists;
    allLists = list;
    pthread_mutex_unlock(&listsLock);
    threadList = list;
    return list;
}

static inline void *recPayload(const TraceRec *rec)
{
    return (char *)rec + sizeof(TraceRec) + (rec->kind == KIND_OBJECT ? OBJ_HEADER_SIZE : 0);
}

// Called by mm.h after the block is allocated, nothing is registered before
// __cxy_mem_trace_start (static allocations are never reported)
void __cxy_mem_trace_register(TraceRec *rec)
{
    rec->list = NULL;
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE))
        return;
    TraceList *list = threadListGet();
    if (list == NULL)
        return;

    rec->birthTick = __atomic_add_fetch(&tick, 1, __ATOMIC_RELAXED);
    rec->list = list;
    pthread_mutex_lock(&list->lock);
    rec->prev = NULL;
    rec->next = list->head;
    if (list->head != NULL)
        list->head->prev = rec;
    list->head = rec;
    if (rec->kind == KIND_BUFFER) {
        list->buffers++;
        list->bufferBytes += rec->size;
    }
    else {
        list->objects++;
        list->objectBytes += rec->size;
        list->objectAllocs++;
    }
    pthread_mutex_unlock(&list->lock);
}

// Called before the block is freed, from any thread
void __cxy_mem_trace_unregister(TraceRec *rec)
{
    TraceList *list = rec->list;
    if (list == NULL)
        return;
    pthread_mutex_lock(&list->lock);
    if (rec->prev != NULL)
        rec->prev->next = rec->next;
    else
        list->head = rec->next;
    if (rec->next != NULL)
        rec->next->prev = rec->prev;
    if (rec->kind == KIND_BUFFER) {
        list->buffers--;
        list->bufferBytes -= rec->size;
    }
    else {
        list->objects--;
        list->objectBytes -= rec->size;
    }
    pthread_mutex_unlock(&list->lock);
    rec->list = NULL;
}

// Locks every list, in registration order, so a fork sees consistent lists
static void lockAllLists(void)
{
    pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next)
        pthread_mutex_lock(&l->lock);
}

static void unlockAllLists(void)
{
    for (TraceList *l = allLists; l != NULL; l = l->next)
        pthread_mutex_unlock(&l->lock);
    pthread_mutex_unlock(&listsLock);
}

// Live totals for tests and tools: objects, object bytes, buffers, buffer bytes
void __cxy_mem_trace_stats(uint64_t out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next) {
        pthread_mutex_lock(&l->lock);
        out[0] += l->objects;
        out[1] += l->objectBytes;
        out[2] += l->buffers;
        out[3] += l->bufferBytes;
        pthread_mutex_unlock(&l->lock);
    }
    pthread_mutex_unlock(&listsLock);
}

static int compareByGenDesc(const void *lhs, const void *rhs)
{
    uint64_t l = ((const LiveEntry *)lhs)->gen, r = ((const LiveEntry *)rhs)->gen;
    return l < r ? 1 : (l > r ? -1 : 0);
}

static void snapshotAppend(LiveEntry **snap, size_t *n, size_t *cap, const TraceRec *rec)
{
    if (*n == *cap) {
        size_t newCap = *cap ? *cap * 2 : 256;
        LiveEntry *grown = realloc(*snap, newCap * sizeof(LiveEntry));
        if (grown == NULL)
            return;
        *snap = grown;
        *cap = newCap;
    }
    (*snap)[(*n)++] = (LiveEntry){.ptr = recPayload(rec),
                                  .size = rec->size,
                                  .path = rec->site ? rec->site->file : NULL,
                                  .line = rec->site ? rec->site->line : 0,
                                  .gen = rec->birthTick,
                                  .buffer = rec->kind == KIND_BUFFER};
}

// Copies live entries born after minGen (most recent first); caller frees
static LiveEntry *liveSnapshot(uint64_t minGen, size_t *count)
{
    LiveEntry *snap = NULL;
    size_t n = 0, cap = 0;
    pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next) {
        pthread_mutex_lock(&l->lock);
        for (const TraceRec *rec = l->head; rec != NULL; rec = rec->next) {
            if (rec->birthTick > minGen)
                snapshotAppend(&snap, &n, &cap, rec);
        }
        pthread_mutex_unlock(&l->lock);
    }
    pthread_mutex_unlock(&listsLock);

    if (snap != NULL)
        qsort(snap, n, sizeof(LiveEntry), compareByGenDesc);
    *count = n;
    return snap;
}

/* ----------------------------------------------------------------- reports */

static const TraceEvent *findAllocEvent(const void *ptr)
{
    uint32_t current = __atomic_load_n(&ringIdx, __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < CXY_TRACE_RING_SIZE; i++) {
        uint32_t idx = (current + CXY_TRACE_RING_SIZE - 1 - i) % CXY_TRACE_RING_SIZE;
        const TraceEvent *ev = &ring[idx];
        if (ev->ptr == ptr && ev->op != NULL && strcmp(ev->op, "alloc") == 0)
            return ev;
    }
    return NULL;
}

static void ringDumpFd(int fd, const void *ptr, bool sym)
{
    dprintf(fd, "\n--- Lifecycle Timeline for %p in __cxy_mem_ring ---\n", ptr);
    uint32_t current = __atomic_load_n(&ringIdx, __ATOMIC_RELAXED);

    // Latest lifecycle epoch, walking backwards until its alloc
    const TraceEvent *epoch[64];
    uint32_t epochCount = 0;
    for (uint32_t i = 0; i < CXY_TRACE_RING_SIZE && epochCount < 64; i++) {
        uint32_t idx = (current + CXY_TRACE_RING_SIZE - 1 - i) % CXY_TRACE_RING_SIZE;
        const TraceEvent *ev = &ring[idx];
        if (ev->ptr == ptr && ev->op != NULL) {
            epoch[epochCount++] = ev;
            if (strcmp(ev->op, "alloc") == 0)
                break;
        }
    }

    if (epochCount == 0) {
        dprintf(fd, "  (no trace events recorded for %p)\n", ptr);
    }
    else {
        for (uint32_t i = 0; i < epochCount; i++) {
            const TraceEvent *ev = epoch[epochCount - 1 - i];
            dprintf(fd,
                    "  [%u] %-5s refs=%-3u @ %s:%llu\n",
                    i + 1,
                    ev->op,
                    (unsigned)ev->refs,
                    ev->path ? ev->path : "<unknown>",
                    (unsigned long long)ev->line);
            printEventStack(fd, ev, sym);
        }
    }
    dprintf(fd, "---------------------------------------------------\n\n");
}

/*
 * Live allocations newer than minGen. leakStyle reproduces the exit report
 * exactly; otherwise a compact listing limited to `limit` entries (0 = all).
 */
static void liveReportFd(int fd, uint64_t minGen, size_t limit, bool sym, bool leakStyle)
{
    size_t count = 0;
    LiveEntry *snap = liveSnapshot(minGen, &count);
    if (count == 0) {
        if (!leakStyle)
            dprintf(fd, "no live allocations\n");
        free(snap);
        return;
    }

    uint64_t totalBytes = 0;
    for (size_t i = 0; i < count; i++)
        totalBytes += snap[i].size;

    size_t shown = (limit == 0 || limit > count) ? count : limit;
    if (leakStyle)
        dprintf(fd, "\n=== CXY Memory Leak Summary ===\n");

    for (size_t i = 0; i < shown; i++) {
        const LiveEntry *e = &snap[i];
        if (e->buffer) {
            dprintf(fd,
                    "%sptr=%p size=%llu buffer @ %s:%llu\n",
                    leakStyle ? "[LEAK] " : "",
                    e->ptr,
                    (unsigned long long)e->size,
                    e->path ? e->path : "<unknown>",
                    (unsigned long long)e->line);
            continue;
        }
        uint32_t refs = __cxy_mem_header_refs ? __cxy_mem_header_refs(e->ptr) : 0;
        dprintf(fd,
                "%sptr=%p size=%llu refs=%u @ %s:%llu\n",
                leakStyle ? "[LEAK] " : "",
                e->ptr,
                (unsigned long long)e->size,
                (unsigned)refs,
                e->path ? e->path : "<unknown>",
                (unsigned long long)e->line);

        const TraceEvent *allocEv = findAllocEvent(e->ptr);
        if (allocEv != NULL && allocEv->stackSize > CXY_TRACE_SKIP_FRAMES) {
            dprintf(fd, "  Allocation callstack:\n");
            printFrames(fd,
                        allocEv->stack + CXY_TRACE_SKIP_FRAMES,
                        allocEv->stackSize - CXY_TRACE_SKIP_FRAMES,
                        sym);
        }
    }

    if (leakStyle) {
        dprintf(fd,
                "Total: %llu active allocations (%llu bytes leaked)\n",
                (unsigned long long)count,
                (unsigned long long)totalBytes);
    }
    else {
        dprintf(fd,
                "Total: %llu live allocations (%llu bytes)%s\n",
                (unsigned long long)count,
                (unsigned long long)totalBytes,
                shown < count ? ", showing most recent" : "");
    }
    free(snap);
}

static int compareSiteKey(const void *lhs, const void *rhs)
{
    const LiveEntry *l = lhs, *r = rhs;
    int c = strcmp(l->path ? l->path : "", r->path ? r->path : "");
    if (c != 0)
        return c;
    return l->line < r->line ? -1 : (l->line > r->line ? 1 : 0);
}

static int compareSiteBytesDesc(const void *lhs, const void *rhs)
{
    const SiteEntry *l = lhs, *r = rhs;
    if (l->bytes != r->bytes)
        return l->bytes < r->bytes ? 1 : -1;
    return l->count < r->count ? 1 : (l->count > r->count ? -1 : 0);
}

// Live allocations newer than minGen, grouped by allocation site
static void sitesReportFd(int fd, uint64_t minGen, size_t limit)
{
    size_t count = 0;
    LiveEntry *snap = liveSnapshot(minGen, &count);
    if (count == 0) {
        dprintf(fd, "no live allocations\n");
        free(snap);
        return;
    }

    qsort(snap, count, sizeof(LiveEntry), compareSiteKey);
    SiteEntry *sites = malloc(count * sizeof(SiteEntry));
    if (sites == NULL) {
        dprintf(fd, "error: out of memory\n");
        free(snap);
        return;
    }

    size_t nSites = 0;
    uint64_t totalBytes = 0;
    for (size_t i = 0; i < count; i++) {
        totalBytes += snap[i].size;
        if (nSites > 0 && compareSiteKey(&snap[i], &snap[i - 1]) == 0) {
            sites[nSites - 1].count++;
            sites[nSites - 1].bytes += snap[i].size;
        }
        else {
            sites[nSites++] = (SiteEntry){
                .path = snap[i].path, .line = snap[i].line, .count = 1, .bytes = snap[i].size};
        }
    }
    qsort(sites, nSites, sizeof(SiteEntry), compareSiteBytesDesc);

    size_t shown = (limit == 0 || limit > nSites) ? nSites : limit;
    dprintf(fd, "%12s %8s  %s\n", "bytes", "count", "site");
    for (size_t i = 0; i < shown; i++) {
        dprintf(fd,
                "%12llu %8llu  %s:%llu\n",
                (unsigned long long)sites[i].bytes,
                (unsigned long long)sites[i].count,
                sites[i].path ? sites[i].path : "<unknown>",
                (unsigned long long)sites[i].line);
    }
    dprintf(fd,
            "Total: %llu live allocations (%llu bytes) across %llu sites%s\n",
            (unsigned long long)count,
            (unsigned long long)totalBytes,
            (unsigned long long)nSites,
            shown < nSites ? ", showing largest" : "");
    free(sites);
    free(snap);
}

static void summaryFd(int fd)
{
    uint64_t count = 0, bytes = 0, gen = 0, buffers = 0, bufferBytes = 0;
    pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next) {
        pthread_mutex_lock(&l->lock);
        count += l->objects;
        bytes += l->objectBytes;
        gen += l->objectAllocs;
        buffers += l->buffers;
        bufferBytes += l->bufferBytes;
        pthread_mutex_unlock(&l->lock);
    }
    pthread_mutex_unlock(&listsLock);
    dprintf(fd,
            "live: %llu allocations, %llu bytes\n"
            "buffers: %llu live, %llu bytes\n"
            "allocations since start: %llu\n"
            "trace events: %u\n"
            "mark: %llu\n",
            (unsigned long long)count,
            (unsigned long long)bytes,
            (unsigned long long)buffers,
            (unsigned long long)bufferBytes,
            (unsigned long long)gen,
            (unsigned)__atomic_load_n(&ringIdx, __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&markGen, __ATOMIC_RELAXED));
}

// Called by the program's shutdown sequence after all finalizers ran
void __cxy_mem_trace_report(void)
{
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE))
        return;
    fflush(stdout);
    liveReportFd(STDOUT_FILENO, 0, 0, true, true);
}

/* ---------------------------------------------------------------- counters */

/*
 * Cumulative per-type and per-site counters. Each thread counts into its own
 * arrays (no shared cache line), a query sums them. The generated program
 * provides the tables (__cxy_sites, __cxy_types) and the hooks below are called
 * from mm.h. The records mirror __Site and __TypeInfo in runtime/mm.h.
 */
__attribute__((weak)) extern const SiteRecord *const __cxy_sites[];
__attribute__((weak)) extern const uint32_t __cxy_site_count;
__attribute__((weak)) extern const TypeRecord *const __cxy_types[];
__attribute__((weak)) extern const uint32_t __cxy_type_count;

typedef struct {
    uint64_t allocs, bytes;
} SiteCount;

typedef struct {
    uint64_t allocs, bytes, frees;
} TypeCount;

typedef struct Counters {
    struct Counters *next;
    SiteCount *sites;
    TypeCount *types;
} Counters;

static __thread Counters *threadCounters;
static Counters *allCounters; // grows only, a finished thread keeps its numbers
static pthread_mutex_t countersLock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t siteTableSize(void)
{
    return &__cxy_site_count != NULL ? __cxy_site_count : 0;
}

static uint32_t typeTableSize(void)
{
    return &__cxy_type_count != NULL ? __cxy_type_count : 0;
}

static Counters *countersForThread(void)
{
    Counters *c = threadCounters;
    if (c != NULL)
        return c;

    c = calloc(1, sizeof(Counters));
    if (c == NULL)
        return NULL;
    c->sites = calloc(siteTableSize() + 1, sizeof(SiteCount));
    c->types = calloc(typeTableSize() + 1, sizeof(TypeCount));
    if (c->sites == NULL || c->types == NULL) {
        free(c->sites);
        free(c->types);
        free(c);
        return NULL;
    }

    pthread_mutex_lock(&countersLock);
    c->next = allCounters;
    allCounters = c;
    pthread_mutex_unlock(&countersLock);
    threadCounters = c;
    return c;
}

void __cxy_mem_trace_count(uint32_t type, uint32_t site, uint64_t bytes)
{
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE))
        return;
    Counters *c = countersForThread();
    if (c == NULL)
        return;
    if (type < typeTableSize()) {
        __atomic_fetch_add(&c->types[type].allocs, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&c->types[type].bytes, bytes, __ATOMIC_RELAXED);
    }
    if (site < siteTableSize()) {
        __atomic_fetch_add(&c->sites[site].allocs, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&c->sites[site].bytes, bytes, __ATOMIC_RELAXED);
    }
}

void __cxy_mem_trace_type_free(uint32_t type)
{
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE) || type >= typeTableSize())
        return;
    Counters *c = countersForThread();
    if (c != NULL)
        __atomic_fetch_add(&c->types[type].frees, 1, __ATOMIC_RELAXED);
}

// Sums every thread's arrays into freshly allocated tables
static void sumCounters(SiteCount **sites, TypeCount **types)
{
    uint32_t ns = siteTableSize(), nt = typeTableSize();
    *sites = calloc(ns + 1, sizeof(SiteCount));
    *types = calloc(nt + 1, sizeof(TypeCount));
    if (*sites == NULL || *types == NULL)
        return;

    pthread_mutex_lock(&countersLock);
    for (Counters *c = allCounters; c != NULL; c = c->next) {
        for (uint32_t i = 0; i < ns; i++) {
            (*sites)[i].allocs += __atomic_load_n(&c->sites[i].allocs, __ATOMIC_RELAXED);
            (*sites)[i].bytes += __atomic_load_n(&c->sites[i].bytes, __ATOMIC_RELAXED);
        }
        for (uint32_t i = 0; i < nt; i++) {
            (*types)[i].allocs += __atomic_load_n(&c->types[i].allocs, __ATOMIC_RELAXED);
            (*types)[i].bytes += __atomic_load_n(&c->types[i].bytes, __ATOMIC_RELAXED);
            (*types)[i].frees += __atomic_load_n(&c->types[i].frees, __ATOMIC_RELAXED);
        }
    }
    pthread_mutex_unlock(&countersLock);
}

typedef struct {
    uint32_t index;
    uint64_t allocs;
} RankEntry;

static int compareRankDesc(const void *lhs, const void *rhs)
{
    const RankEntry *l = lhs, *r = rhs;
    if (l->allocs != r->allocs)
        return l->allocs < r->allocs ? 1 : -1;
    return l->index < r->index ? -1 : (l->index > r->index ? 1 : 0);
}

// Types with at least one allocation, most allocated first
static void typesReportFd(int fd)
{
    SiteCount *sites = NULL;
    TypeCount *types = NULL;
    sumCounters(&sites, &types);
    uint32_t nt = typeTableSize();
    if (types == NULL || nt == 0) {
        dprintf(fd, "no type information, was the program built with --memory-trace?\n");
        free(sites);
        free(types);
        return;
    }

    RankEntry *rank = calloc(nt, sizeof(RankEntry));
    size_t n = 0;
    for (uint32_t i = 0; rank != NULL && i < nt; i++) {
        if (types[i].allocs > 0)
            rank[n++] = (RankEntry){.index = i, .allocs = types[i].allocs};
    }
    qsort(rank, n, sizeof(RankEntry), compareRankDesc);

    dprintf(fd, "%10s %10s %10s %12s  %s\n", "allocs", "frees", "live", "bytes", "type");
    for (size_t i = 0; i < n; i++) {
        const TypeCount *t = &types[rank[i].index];
        const TypeRecord *info = __cxy_types[rank[i].index];
        dprintf(fd,
                "%10llu %10llu %10llu %12llu  %s\n",
                (unsigned long long)t->allocs,
                (unsigned long long)t->frees,
                (unsigned long long)(t->allocs > t->frees ? t->allocs - t->frees : 0),
                (unsigned long long)t->bytes,
                info != NULL && info->name != NULL ? info->name : "<unknown>");
    }
    dprintf(fd, "Total: %llu types\n", (unsigned long long)n);
    free(rank);
    free(sites);
    free(types);
}

// Allocation points with at least one allocation, most allocating first
static void siteCountsReportFd(int fd, size_t limit)
{
    SiteCount *sites = NULL;
    TypeCount *types = NULL;
    sumCounters(&sites, &types);
    uint32_t ns = siteTableSize();
    if (sites == NULL || ns == 0) {
        dprintf(fd, "no site information, was the program built with --memory-trace?\n");
        free(sites);
        free(types);
        return;
    }

    RankEntry *rank = calloc(ns, sizeof(RankEntry));
    size_t n = 0;
    for (uint32_t i = 0; rank != NULL && i < ns; i++) {
        if (sites[i].allocs > 0)
            rank[n++] = (RankEntry){.index = i, .allocs = sites[i].allocs};
    }
    qsort(rank, n, sizeof(RankEntry), compareRankDesc);

    size_t shown = (limit == 0 || limit > n) ? n : limit;
    dprintf(fd, "%10s %12s  %s\n", "allocs", "bytes", "site");
    for (size_t i = 0; i < shown; i++) {
        const SiteCount *s = &sites[rank[i].index];
        const SiteRecord *info = __cxy_sites[rank[i].index];
        dprintf(fd,
                "%10llu %12llu  %s:%u:%u (%s)\n",
                (unsigned long long)s->allocs,
                (unsigned long long)s->bytes,
                info != NULL && info->file != NULL ? info->file : "<unknown>",
                info != NULL ? info->line : 0,
                info != NULL ? info->column : 0,
                info != NULL && info->function != NULL ? info->function : "?");
    }
    dprintf(fd, "Total: %llu sites%s\n", (unsigned long long)n, shown < n ? ", showing most allocating" : "");
    free(rank);
    free(sites);
    free(types);
}

/* ------------------------------------------------------------ query server */

static void helpFd(int fd)
{
    dprintf(fd,
            "commands (append 'nosym' to skip symbolization):\n"
            "  summary              live count/bytes and counters\n"
            "  sites [n]            live allocations grouped by site (default %d)\n"
            "  list [n]             most recent live allocations with stacks (default %d)\n"
            "  leaks                full report, same as the exit-time leak report\n"
            "  ring <ptr>           lifecycle timeline of one pointer\n"
            "  mark                 remember the current allocation generation\n"
            "  types                cumulative allocs/frees/bytes per class\n"
            "  sitecounts [n]       cumulative allocations per allocation point\n"
            "  diff [n]             sites of allocations made since `mark` still alive\n"
            "  difflist [n]         like `list`, but only allocations since `mark`\n"
            "containment (scans a forked copy of the process):\n"
            "  holders <ptr>        who points at this object\n"
            "  path <ptr>           shortest chain of holders from a root to this object\n"
            "  contains <y> <x>     is x held, directly or indirectly, by y\n"
            "  retained [n]         roots ranked by what only they keep alive\n"
            "  tree <ptr> [depth]   what this object holds (default depth 3)\n",
            CXY_QUERY_DEFAULT_LIMIT,
            CXY_QUERY_DEFAULT_LIMIT);
}

static size_t parseLimit(const char *arg)
{
    if (arg == NULL)
        return CXY_QUERY_DEFAULT_LIMIT;
    char *end = NULL;
    unsigned long long n = strtoull(arg, &end, 10);
    return (end == arg) ? CXY_QUERY_DEFAULT_LIMIT : (size_t)n;
}

/* ------------------------------------------------------ containment graph */

/*
 * Who holds what. Every live allocation is a node. An aligned word inside a
 * node that points at (or into) another node is an edge. Nothing is recorded
 * while the program runs: the graph is built from a forked copy of the process
 * when a command asks for it, so the program is never stalled and the child
 * reads a consistent snapshot.
 *
 * Container storage (vector and map buffers) is plain malloc memory. It takes
 * part when registered through __cxy_mem_trace_raw_*, and on macOS any heap
 * block found by following a pointer is added with its malloc_size().
 *
 * The scan is conservative: an integer that looks like an address is a false
 * edge, and references held only on a stack or in globals are not seen, such
 * objects show up as roots.
 */

#define GRAPH_NONE UINT32_MAX
#define GRAPH_MULTI UINT32_MAX

typedef struct {
    uintptr_t start;
    uint64_t size;
    const char *path;
    uint64_t line;
    bool raw; // a buffer, not a smart pointer allocation
} GraphNode;

typedef struct {
    uint32_t from, to;
    uint32_t off; // byte offset in `from` where the pointer is stored
} GraphEdge;

typedef struct {
    uintptr_t start;
    uint32_t id;
} GraphKey;

typedef struct {
    GraphNode *nodes;
    size_t count, cap;
    GraphKey *keys; // registered nodes ordered by address
    size_t keyCount;
    uint32_t *found; // nodes discovered by malloc_size, id + 1, 0 = empty
    size_t foundCap, foundCount;
    GraphEdge *edges;
    size_t edgeCount, edgeCap;
    uint32_t *outStart, *outEdge;
    uint32_t *inStart, *inEdge;
    uint64_t scanned, scanLimit;
    bool truncated;
} Graph;

static int graphFd = -1;
static const Graph *sortGraph = NULL;

static int compareGraphKey(const void *lhs, const void *rhs)
{
    uintptr_t l = ((const GraphKey *)lhs)->start, r = ((const GraphKey *)rhs)->start;
    return l < r ? -1 : (l > r ? 1 : 0);
}

static uint32_t graphAddNode(Graph *g, GraphNode node)
{
    if (g->count == g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 4096;
        GraphNode *nodes = realloc(g->nodes, cap * sizeof(GraphNode));
        if (nodes == NULL)
            return GRAPH_NONE;
        g->nodes = nodes;
        g->cap = cap;
    }
    g->nodes[g->count] = node;
    return (uint32_t)g->count++;
}

static uint32_t graphFoundLookup(const Graph *g, uintptr_t addr)
{
    if (g->foundCap == 0)
        return GRAPH_NONE;
    size_t idx = liveHash((const void *)addr) & (g->foundCap - 1);
    while (g->found[idx] != 0) {
        uint32_t id = g->found[idx] - 1;
        if (g->nodes[id].start == addr)
            return id;
        idx = (idx + 1) & (g->foundCap - 1);
    }
    return GRAPH_NONE;
}

static bool graphFoundInsert(Graph *g, uintptr_t addr, uint32_t id)
{
    if ((g->foundCount + 1) * 2 >= g->foundCap) {
        size_t cap = g->foundCap ? g->foundCap * 2 : 1024;
        uint32_t *table = calloc(cap, sizeof(uint32_t));
        if (table == NULL)
            return false;
        for (size_t i = 0; i < g->foundCap; i++) {
            if (g->found[i] == 0)
                continue;
            size_t idx = liveHash((const void *)g->nodes[g->found[i] - 1].start) & (cap - 1);
            while (table[idx] != 0)
                idx = (idx + 1) & (cap - 1);
            table[idx] = g->found[i];
        }
        free(g->found);
        g->found = table;
        g->foundCap = cap;
    }
    size_t idx = liveHash((const void *)addr) & (g->foundCap - 1);
    while (g->found[idx] != 0)
        idx = (idx + 1) & (g->foundCap - 1);
    g->found[idx] = id + 1;
    g->foundCount++;
    return true;
}

// The node holding `addr`, which may point inside it, or GRAPH_NONE
static uint32_t graphFind(Graph *g, uintptr_t addr)
{
    size_t lo = 0, hi = g->keyCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g->keys[mid].start <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo > 0) {
        const GraphNode *n = &g->nodes[g->keys[lo - 1].id];
        if (addr - n->start < n->size)
            return g->keys[lo - 1].id;
    }

    uint32_t id = graphFoundLookup(g, addr);
    if (id != GRAPH_NONE)
        return id;

#if defined(__APPLE__)
    // Heap blocks are 16 byte aligned and malloc_size() is 0 for anything else
    if ((addr & 15) == 0 && addr >= 4096 && addr < ((uintptr_t)1 << 47)) {
        size_t size = malloc_size((const void *)addr);
        if (size > 0) {
            id = graphAddNode(g, (GraphNode){.start = addr, .size = size, .raw = true});
            if (id != GRAPH_NONE)
                graphFoundInsert(g, addr, id);
            return id;
        }
    }
#endif
    return GRAPH_NONE;
}

static bool graphAddEdge(Graph *g, uint32_t from, uint32_t to, uint32_t off)
{
    if (g->edgeCount == g->edgeCap) {
        size_t cap = g->edgeCap ? g->edgeCap * 2 : 4096;
        GraphEdge *edges = realloc(g->edges, cap * sizeof(GraphEdge));
        if (edges == NULL)
            return false;
        g->edges = edges;
        g->edgeCap = cap;
    }
    g->edges[g->edgeCount++] = (GraphEdge){.from = from, .to = to, .off = off};
    return true;
}

static bool graphScan(Graph *g, uint32_t id)
{
    const GraphNode n = g->nodes[id]; // the node array can move while scanning
    if ((n.start & (sizeof(uintptr_t) - 1)) != 0)
        return true;
    const uintptr_t *words = (const uintptr_t *)n.start;
    size_t count = (size_t)(n.size / sizeof(uintptr_t));
    g->scanned += n.size;
    for (size_t i = 0; i < count; i++) {
        uintptr_t w = words[i];
        if (w < 4096)
            continue;
        uint32_t to = graphFind(g, w);
        if (to == GRAPH_NONE || to == id)
            continue;
        if (!graphAddEdge(g, id, to, (uint32_t)(i * sizeof(uintptr_t))))
            return false;
    }
    return true;
}

static bool graphIndex(Graph *g)
{
    size_t n = g->count, m = g->edgeCount;
    g->outStart = calloc(n + 1, sizeof(uint32_t));
    g->inStart = calloc(n + 1, sizeof(uint32_t));
    g->outEdge = malloc((m ? m : 1) * sizeof(uint32_t));
    g->inEdge = malloc((m ? m : 1) * sizeof(uint32_t));
    uint32_t *oc = malloc((n ? n : 1) * sizeof(uint32_t));
    uint32_t *ic = malloc((n ? n : 1) * sizeof(uint32_t));
    if (!g->outStart || !g->inStart || !g->outEdge || !g->inEdge || !oc || !ic) {
        free(oc);
        free(ic);
        return false;
    }

    for (size_t e = 0; e < m; e++) {
        g->outStart[g->edges[e].from + 1]++;
        g->inStart[g->edges[e].to + 1]++;
    }
    for (size_t v = 0; v < n; v++) {
        g->outStart[v + 1] += g->outStart[v];
        g->inStart[v + 1] += g->inStart[v];
        oc[v] = g->outStart[v];
        ic[v] = g->inStart[v];
    }
    for (size_t e = 0; e < m; e++) {
        g->outEdge[oc[g->edges[e].from]++] = (uint32_t)e;
        g->inEdge[ic[g->edges[e].to]++] = (uint32_t)e;
    }
    free(oc);
    free(ic);
    return true;
}

// Runs in the forked child: the live table is a private copy, no lock needed
static bool graphBuild(Graph *g)
{
    const char *env = getenv("CXY_MEM_SCAN_LIMIT_MB");
    g->scanLimit = (env != NULL ? strtoull(env, NULL, 10) : 2048ull) << 20;

    for (const TraceList *l = allLists; l != NULL; l = l->next) {
        for (const TraceRec *rec = l->head; rec != NULL; rec = rec->next) {
            GraphNode node = {.start = (uintptr_t)recPayload(rec),
                              .size = rec->size,
                              .path = rec->site ? rec->site->file : NULL,
                              .line = rec->site ? rec->site->line : 0,
                              .raw = rec->kind == KIND_BUFFER};
            if (graphAddNode(g, node) == GRAPH_NONE)
                return false;
        }
    }

    g->keys = malloc((g->count ? g->count : 1) * sizeof(GraphKey));
    if (g->keys == NULL)
        return false;
    for (size_t i = 0; i < g->count; i++)
        g->keys[i] = (GraphKey){.start = g->nodes[i].start, .id = (uint32_t)i};
    g->keyCount = g->count;
    qsort(g->keys, g->keyCount, sizeof(GraphKey), compareGraphKey);

    // Nodes found while scanning are appended and scanned in turn
    for (size_t id = 0; id < g->count; id++) {
        if (g->scanned >= g->scanLimit) {
            g->truncated = true;
            break;
        }
        if (!graphScan(g, (uint32_t)id))
            return false;
    }
    return graphIndex(g);
}

static void printGraphNode(int fd, const Graph *g, uint32_t id)
{
    const GraphNode *n = &g->nodes[id];
    if (!n->raw) {
        uint32_t refs = __cxy_mem_header_refs ? __cxy_mem_header_refs((void *)n->start) : 0;
        dprintf(fd,
                "%p size=%llu refs=%u @ %s:%llu",
                (void *)n->start,
                (unsigned long long)n->size,
                (unsigned)refs,
                n->path ? n->path : "<unknown>",
                (unsigned long long)n->line);
    }
    else if (n->path != NULL) {
        dprintf(fd,
                "%p size=%llu buffer @ %s:%llu",
                (void *)n->start,
                (unsigned long long)n->size,
                n->path,
                (unsigned long long)n->line);
    }
    else {
        dprintf(fd, "%p size=%llu buffer", (void *)n->start, (unsigned long long)n->size);
    }
}

// One node per line, each indented one level deeper than the holder
static void printChain(int fd, const Graph *g, const uint32_t *ids, const uint32_t *offs, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dprintf(fd, "%*s", (int)(2 + i * 2), "");
        if (i > 0)
            dprintf(fd, "+%u -> ", (unsigned)offs[i]);
        printGraphNode(fd, g, ids[i]);
        dprintf(fd, "\n");
    }
}

static uint32_t graphResolve(int fd, Graph *g, const char *arg)
{
    if (arg == NULL) {
        dprintf(fd, "usage: <command> <ptr>\n");
        return GRAPH_NONE;
    }
    uintptr_t addr = (uintptr_t)strtoull(arg, NULL, 0);
    uint32_t id = graphFind(g, addr);
    if (id == GRAPH_NONE)
        dprintf(fd, "%s is not inside a live allocation\n", arg);
    else if (g->nodes[id].start != addr)
        dprintf(fd, "note: %s is inside the allocation at %p\n", arg, (void *)g->nodes[id].start);
    return id;
}

static void graphHolders(int fd, const Graph *g, uint32_t id)
{
    printGraphNode(fd, g, id);
    dprintf(fd, "\n");
    size_t holders = 0;
    for (uint32_t k = g->inStart[id]; k < g->inStart[id + 1]; k++) {
        const GraphEdge *e = &g->edges[g->inEdge[k]];
        if (holders++ < 50) {
            dprintf(fd, "  held by ");
            printGraphNode(fd, g, e->from);
            dprintf(fd, " at +%u\n", (unsigned)e->off);
        }
    }
    if (holders > 50)
        dprintf(fd, "  ... %llu more\n", (unsigned long long)(holders - 50));
    if (holders == 0)
        dprintf(fd, "  not held by any tracked memory (stack, global or untracked buffer)\n");

    const GraphNode *n = &g->nodes[id];
    uint32_t refs = (!n->raw && __cxy_mem_header_refs) ? __cxy_mem_header_refs((void *)n->start) : 0;
    if (refs > holders)
        dprintf(fd,
                "  %llu of %u reference(s) are held outside tracked memory\n",
                (unsigned long long)(refs - holders),
                (unsigned)refs);
}

// Shortest chain from a node nothing tracked points at, down to `target`
static void graphPath(int fd, const Graph *g, uint32_t target)
{
    size_t n = g->count;
    uint32_t *queue = malloc(n * sizeof(uint32_t)), *prev = malloc(n * sizeof(uint32_t));
    uint32_t *ids = malloc(n * sizeof(uint32_t)), *offs = malloc(n * sizeof(uint32_t));
    uint8_t *seen = calloc(n, 1);
    if (!queue || !prev || !ids || !offs || !seen) {
        dprintf(fd, "error: out of memory\n");
        goto done;
    }

    size_t head = 0, tail = 0;
    uint32_t root = GRAPH_NONE;
    queue[tail++] = target;
    seen[target] = 1;
    while (head < tail) {
        uint32_t v = queue[head++];
        if (g->inStart[v] == g->inStart[v + 1]) {
            root = v;
            break;
        }
        for (uint32_t k = g->inStart[v]; k < g->inStart[v + 1]; k++) {
            uint32_t e = g->inEdge[k], u = g->edges[e].from;
            if (!seen[u]) {
                seen[u] = 1;
                prev[u] = e;
                queue[tail++] = u;
            }
        }
    }

    if (root == GRAPH_NONE) {
        dprintf(fd,
                "no root: every chain of holders ends in a cycle, or is held from memory "
                "the scan cannot see\n");
        goto done;
    }

    size_t len = 0;
    uint32_t v = root;
    ids[len] = v;
    offs[len++] = 0;
    while (v != target) {
        const GraphEdge *e = &g->edges[prev[v]];
        v = e->to;
        offs[len] = e->off;
        ids[len++] = v;
    }
    dprintf(fd, "retention path (%llu holders deep):\n", (unsigned long long)(len - 1));
    printChain(fd, g, ids, offs, len);

done:
    free(queue);
    free(prev);
    free(ids);
    free(offs);
    free(seen);
}

// Is `inner` held, directly or through other objects and buffers, by `outer`
static void graphContains(int fd, const Graph *g, uint32_t outer, uint32_t inner)
{
    if (outer == inner) {
        dprintf(fd, "same allocation\n");
        return;
    }
    size_t n = g->count;
    uint32_t *queue = malloc(n * sizeof(uint32_t)), *prev = malloc(n * sizeof(uint32_t));
    uint32_t *ids = malloc(n * sizeof(uint32_t)), *offs = malloc(n * sizeof(uint32_t));
    uint8_t *seen = calloc(n, 1);
    if (!queue || !prev || !ids || !offs || !seen) {
        dprintf(fd, "error: out of memory\n");
        goto done;
    }

    size_t head = 0, tail = 0;
    bool found = false;
    queue[tail++] = outer;
    seen[outer] = 1;
    while (head < tail && !found) {
        uint32_t v = queue[head++];
        for (uint32_t k = g->outStart[v]; k < g->outStart[v + 1]; k++) {
            uint32_t e = g->outEdge[k], w = g->edges[e].to;
            if (seen[w])
                continue;
            seen[w] = 1;
            prev[w] = e;
            queue[tail++] = w;
            if (w == inner) {
                found = true;
                break;
            }
        }
    }

    if (!found) {
        dprintf(fd, "no, %p does not hold %p\n", (void *)g->nodes[outer].start, (void *)g->nodes[inner].start);
        goto done;
    }

    // Walk back from `inner`, then print from `outer` down
    size_t len = 0;
    for (uint32_t v = inner; v != outer; v = g->edges[prev[v]].from)
        ids[len++] = v;
    dprintf(fd, "yes, held through %llu step(s):\n", (unsigned long long)len);
    uint32_t *chain = queue, *chainOffs = offs; // queue is no longer needed
    chain[0] = outer;
    chainOffs[0] = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t v = ids[len - 1 - i];
        chain[i + 1] = v;
        chainOffs[i + 1] = g->edges[prev[v]].off;
    }
    printChain(fd, g, chain, chainOffs, len + 1);

done:
    free(queue);
    free(prev);
    free(ids);
    free(offs);
    free(seen);
}

static void graphTreeNode(int fd,
                          const Graph *g,
                          uint32_t id,
                          uint32_t off,
                          int depth,
                          int maxDepth,
                          uint8_t *seen)
{
    dprintf(fd, "%*s", 2 + depth * 2, "");
    if (depth > 0)
        dprintf(fd, "+%u -> ", (unsigned)off);
    printGraphNode(fd, g, id);
    uint32_t children = g->outStart[id + 1] - g->outStart[id];
    if (seen[id]) {
        dprintf(fd, " (shown above)\n");
        return;
    }
    seen[id] = 1;
    if (depth >= maxDepth) {
        dprintf(fd, children ? " ...\n" : "\n");
        return;
    }
    dprintf(fd, "\n");

    uint32_t shown = 0;
    for (uint32_t k = g->outStart[id]; k < g->outStart[id + 1]; k++) {
        if (shown++ == 20) {
            dprintf(fd, "%*s... %u more\n", 4 + depth * 2, "", children - 20);
            break;
        }
        const GraphEdge *e = &g->edges[g->outEdge[k]];
        graphTreeNode(fd, g, e->to, e->off, depth + 1, maxDepth, seen);
    }
}

static int compareNodeSite(const void *lhs, const void *rhs)
{
    const GraphNode *l = &sortGraph->nodes[*(const uint32_t *)lhs];
    const GraphNode *r = &sortGraph->nodes[*(const uint32_t *)rhs];
    int c = strcmp(l->path ? l->path : "", r->path ? r->path : "");
    if (c != 0)
        return c;
    return l->line < r->line ? -1 : (l->line > r->line ? 1 : 0);
}

// The sites that account for most of `ids` (reorders it)
static void printTopSites(int fd, const Graph *g, uint32_t *ids, size_t count, size_t top)
{
    typedef struct {
        const char *path;
        uint64_t line, objs, bytes;
    } Site;
    Site best[8] = {{0}};
    if (top > 8)
        top = 8;

    sortGraph = g;
    qsort(ids, count, sizeof(uint32_t), compareNodeSite);
    for (size_t i = 0; i < count;) {
        const GraphNode *first = &g->nodes[ids[i]];
        Site s = {.path = first->path, .line = first->line};
        for (; i < count && compareNodeSite(&ids[i], &(uint32_t){(uint32_t)(first - g->nodes)}) == 0; i++) {
            s.objs++;
            s.bytes += g->nodes[ids[i]].size;
        }
        for (size_t j = 0; j < top; j++) {
            if (s.bytes > best[j].bytes) {
                for (size_t k = top - 1; k > j; k--)
                    best[k] = best[k - 1];
                best[j] = s;
                break;
            }
        }
    }
    for (size_t j = 0; j < top && best[j].objs > 0; j++) {
        if (best[j].path != NULL)
            dprintf(fd,
                    "      %12llu bytes %8llu objs  %s:%llu\n",
                    (unsigned long long)best[j].bytes,
                    (unsigned long long)best[j].objs,
                    best[j].path,
                    (unsigned long long)best[j].line);
        else
            dprintf(fd,
                    "      %12llu bytes %8llu objs  (buffers)\n",
                    (unsigned long long)best[j].bytes,
                    (unsigned long long)best[j].objs);
    }
}

typedef struct {
    uint32_t id, slot;
    uint64_t objs, bytes, exObjs, exBytes;
} RootInfo;

static int compareRootInfo(const void *lhs, const void *rhs)
{
    const RootInfo *l = lhs, *r = rhs;
    if (l->exBytes != r->exBytes)
        return l->exBytes < r->exBytes ? 1 : -1;
    if (l->bytes != r->bytes)
        return l->bytes < r->bytes ? 1 : -1;
    return 0;
}

/*
 * Roots are objects nothing tracked points at. For each, `excl` is what only
 * that root keeps alive (freed if it goes away), `reach` also counts objects
 * other roots hold too. A cache is a root with a large `excl`.
 */
static void graphRetained(int fd, const Graph *g, size_t limit)
{
    size_t n = g->count;
    if (n == 0) {
        dprintf(fd, "no live allocations\n");
        return;
    }
    uint32_t *claim = calloc(n, sizeof(uint32_t)), *stamp = calloc(n, sizeof(uint32_t));
    uint32_t *stack = malloc(n * sizeof(uint32_t)), *ids = malloc(n * sizeof(uint32_t));
    RootInfo *roots = malloc(n * sizeof(RootInfo));
    if (!claim || !stamp || !stack || !ids || !roots) {
        dprintf(fd, "error: out of memory\n");
        goto done;
    }

    size_t rootCount = 0;
    for (uint32_t r = 0; r < n; r++) {
        if (g->inStart[r] != g->inStart[r + 1])
            continue;
        uint32_t slot = (uint32_t)rootCount + 1;
        RootInfo *info = &roots[rootCount++];
        *info = (RootInfo){.id = r, .slot = slot};

        size_t sp = 0;
        stack[sp++] = r;
        stamp[r] = slot;
        while (sp > 0) {
            uint32_t v = stack[--sp];
            info->objs++;
            info->bytes += g->nodes[v].size;
            if (claim[v] == 0)
                claim[v] = slot;
            else if (claim[v] != slot)
                claim[v] = GRAPH_MULTI;
            for (uint32_t k = g->outStart[v]; k < g->outStart[v + 1]; k++) {
                uint32_t w = g->edges[g->outEdge[k]].to;
                if (stamp[w] != slot) {
                    stamp[w] = slot;
                    stack[sp++] = w;
                }
            }
        }
    }

    size_t unrooted = 0;
    uint64_t unrootedBytes = 0;
    for (uint32_t v = 0; v < n; v++) {
        if (claim[v] == 0) {
            unrooted++;
            unrootedBytes += g->nodes[v].size;
        }
        else if (claim[v] != GRAPH_MULTI) {
            roots[claim[v] - 1].exObjs++;
            roots[claim[v] - 1].exBytes += g->nodes[v].size;
        }
    }

    qsort(roots, rootCount, sizeof(RootInfo), compareRootInfo);
    dprintf(fd, "%12s %10s %12s %10s  %s\n", "excl.bytes", "excl.objs", "reach.bytes", "reach.objs", "root");
    size_t shown = 0, holding = 0;
    for (size_t i = 0; i < rootCount; i++) {
        const RootInfo *info = &roots[i];
        if (info->objs < 2)
            continue; // holds nothing
        holding++;
        if (shown >= limit)
            continue;
        shown++;
        dprintf(fd,
                "%12llu %10llu %12llu %10llu  ",
                (unsigned long long)info->exBytes,
                (unsigned long long)info->exObjs,
                (unsigned long long)info->bytes,
                (unsigned long long)info->objs);
        printGraphNode(fd, g, info->id);
        dprintf(fd, "\n");

        size_t count = 0;
        for (uint32_t v = 0; v < n; v++) {
            if (claim[v] == info->slot)
                ids[count++] = v;
        }
        printTopSites(fd, g, ids, count, 3);
    }
    dprintf(fd,
            "Total: %llu objects, %llu edges, %llu roots (%llu hold other objects)%s\n",
            (unsigned long long)n,
            (unsigned long long)g->edgeCount,
            (unsigned long long)rootCount,
            (unsigned long long)holding,
            holding > shown ? ", showing the largest" : "");

    if (unrooted > 0) {
        dprintf(fd,
                "\nunrooted: %llu objects, %llu bytes only held through cycles, or from "
                "memory the scan cannot see\n",
                (unsigned long long)unrooted,
                (unsigned long long)unrootedBytes);
        size_t count = 0;
        for (uint32_t v = 0; v < n; v++) {
            if (claim[v] == 0)
                ids[count++] = v;
        }
        printTopSites(fd, g, ids, count, 3);
    }

done:
    free(claim);
    free(stamp);
    free(stack);
    free(ids);
    free(roots);
}

// Runs in the forked child
static void runGraphCommand(int fd, const char *cmd, const char *arg1, const char *arg2)
{
    Graph g = {0};
    if (!graphBuild(&g)) {
        dprintf(fd, "error: out of memory while scanning\n");
        return;
    }

    if (strcmp(cmd, "retained") == 0) {
        graphRetained(fd, &g, parseLimit(arg1));
    }
    else {
        uint32_t id = graphResolve(fd, &g, arg1);
        if (id != GRAPH_NONE) {
            if (strcmp(cmd, "holders") == 0) {
                graphHolders(fd, &g, id);
            }
            else if (strcmp(cmd, "path") == 0) {
                graphPath(fd, &g, id);
            }
            else if (strcmp(cmd, "contains") == 0) {
                uint32_t inner = graphResolve(fd, &g, arg2);
                if (inner != GRAPH_NONE)
                    graphContains(fd, &g, id, inner);
            }
            else {
                int depth = arg2 ? atoi(arg2) : 3;
                uint8_t *seen = calloc(g.count, 1);
                if (seen != NULL) {
                    graphTreeNode(fd, &g, id, 0, 0, depth < 0 ? 0 : (depth > 8 ? 8 : depth), seen);
                    free(seen);
                }
            }
        }
    }

    if (g.truncated)
        dprintf(fd,
                "warning: scan stopped after %llu MB (CXY_MEM_SCAN_LIMIT_MB), results are partial\n",
                (unsigned long long)(g.scanLimit >> 20));
}

static void graphCrashHandler(int sig)
{
    static const char msg[] = "error: the scan crashed while reading memory\n";
    (void)!write(graphFd, msg, sizeof(msg) - 1);
    _exit(128 + sig);
}

/*
 * Graph commands run in a forked child so the program keeps running and the
 * scan sees a stable copy. The table lock is held across the fork so the copy
 * is consistent. The child never takes it again and leaves with _exit() so
 * none of the program's exit handlers run.
 */
static void graphCommand(int fd, const char *cmd, const char *arg1, const char *arg2)
{
    lockAllLists();
    pid_t pid = fork();
    if (pid == 0) {
        graphFd = fd;
        struct sigaction sa = {.sa_handler = graphCrashHandler};
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, SIGSEGV);
        sigaddset(&unblock, SIGBUS);
        pthread_sigmask(SIG_UNBLOCK, &unblock, NULL);
        runGraphCommand(fd, cmd, arg1, arg2);
        _exit(0);
    }
    unlockAllLists();
    if (pid < 0) {
        dprintf(fd, "error: fork failed: %s\n", strerror(errno));
        return;
    }

    const char *env = getenv("CXY_MEM_SCAN_SECONDS");
    long limitMs = (env != NULL ? atol(env) : 120) * 1000;
    int status = 0;
    for (long waited = 0;; waited += 10) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (r < 0 && errno != EINTR)
            break; // reaped by the program (SIGCHLD ignored)
        if (waited >= limitMs) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            dprintf(fd, "error: scan timed out (CXY_MEM_SCAN_SECONDS)\n");
            return;
        }
        usleep(10000);
    }
}

static void handleCommand(int fd, char *line)
{
    char *args[4] = {NULL};
    int nargs = 0;
    bool sym = true;
    for (char *tok = strtok(line, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        if (strcmp(tok, "nosym") == 0)
            sym = false;
        else if (nargs < 4)
            args[nargs++] = tok;
    }
    if (nargs == 0 || strcmp(args[0], "help") == 0) {
        helpFd(fd);
        return;
    }

    const char *cmd = args[0];
    uint64_t mark = __atomic_load_n(&markGen, __ATOMIC_RELAXED);
    if (strcmp(cmd, "summary") == 0)
        summaryFd(fd);
    else if (strcmp(cmd, "sites") == 0)
        sitesReportFd(fd, 0, parseLimit(args[1]));
    else if (strcmp(cmd, "list") == 0)
        liveReportFd(fd, 0, parseLimit(args[1]), sym, false);
    else if (strcmp(cmd, "leaks") == 0)
        liveReportFd(fd, 0, 0, sym, true);
    else if (strcmp(cmd, "types") == 0)
        typesReportFd(fd);
    else if (strcmp(cmd, "sitecounts") == 0)
        siteCountsReportFd(fd, parseLimit(args[1]));
    else if (strcmp(cmd, "ring") == 0) {
        if (args[1] == NULL) {
            dprintf(fd, "usage: ring <ptr>\n");
            return;
        }
        ringDumpFd(fd, (void *)(uintptr_t)strtoull(args[1], NULL, 0), sym);
    }
    else if (strcmp(cmd, "mark") == 0) {
        uint64_t gen = __atomic_load_n(&tick, __ATOMIC_RELAXED);
        __atomic_store_n(&markGen, gen, __ATOMIC_RELAXED);
        dprintf(fd, "mark %llu\n", (unsigned long long)gen);
    }
    else if (strcmp(cmd, "diff") == 0)
        sitesReportFd(fd, mark, parseLimit(args[1]));
    else if (strcmp(cmd, "difflist") == 0)
        liveReportFd(fd, mark, parseLimit(args[1]), sym, false);
    else if (strcmp(cmd, "holders") == 0 || strcmp(cmd, "path") == 0 ||
             strcmp(cmd, "contains") == 0 || strcmp(cmd, "retained") == 0 ||
             strcmp(cmd, "tree") == 0)
        graphCommand(fd, cmd, args[1], args[2]);
    else
        dprintf(fd, "unknown command '%s', try 'help'\n", cmd);
}

static void removeSocketAtExit(void)
{
    if (socketPath[0] != '\0')
        unlink(socketPath);
}

static void *queryServer(void *arg)
{
    int listener = (int)(intptr_t)arg;
    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            break;
        }

        char line[256];
        size_t len = 0;
        while (len < sizeof(line) - 1) {
            ssize_t n = read(client, line + len, sizeof(line) - 1 - len);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            len += (size_t)n;
            if (memchr(line, '\n', len) != NULL)
                break;
        }
        line[len] = '\0';
        handleCommand(client, line);
        close(client);
    }
    close(listener);
    return NULL;
}

static void startQueryServer(void)
{
    const char *env = getenv("CXY_MEM_SOCKET");
    if (env != NULL && strcmp(env, "0") == 0)
        return;

    if (env != NULL && env[0] != '\0' && strcmp(env, "1") != 0)
        snprintf(socketPath, sizeof(socketPath), "%s", env);
    else
        snprintf(socketPath, sizeof(socketPath), "/tmp/cxy-mem-%d.sock", (int)getpid());

    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0)
        goto failed;

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socketPath);
    unlink(socketPath);

    // Owner-only socket
    mode_t prevMask = umask(0177);
    int rc = bind(listener, (struct sockaddr *)&addr, sizeof(addr));
    umask(prevMask);
    if (rc < 0 || listen(listener, 4) < 0) {
        close(listener);
        goto failed;
    }

    // The server thread must never receive the program's signals
    sigset_t all, prev;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &prev);
    pthread_t thread;
    rc = pthread_create(&thread, NULL, queryServer, (void *)(intptr_t)listener);
    pthread_sigmask(SIG_SETMASK, &prev, NULL);
    if (rc != 0) {
        close(listener);
        unlink(socketPath);
        goto failed;
    }
    pthread_detach(thread);
    atexit(removeSocketAtExit);
    dprintf(STDERR_FILENO, "[cxy-mem] query socket: %s\n", socketPath);
    return;

failed:
    dprintf(STDERR_FILENO, "[cxy-mem] query socket disabled: %s\n", strerror(errno));
    socketPath[0] = '\0';
}

/* -------------------------------------------------------------------- hooks */

void __cxy_mem_trace_start(void)
{
    // Allocations before this point are static and never reported as leaks
    bool expected = false;
    if (__atomic_compare_exchange_n(&started, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&startTick, __atomic_load_n(&tick, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
        startQueryServer();
    }
}

void __cxy_mem_trace_event(void *ptr, const char *op, uint32_t refs, const char *path, uint64_t line)
{
    if (ptr == NULL)
        return;
    uint32_t idx = __atomic_fetch_add(&ringIdx, 1, __ATOMIC_RELAXED) % CXY_TRACE_RING_SIZE;
    TraceEvent *ev = &ring[idx];
    ev->ptr = ptr;
    ev->op = op;
    ev->path = path;
    ev->line = line;
    ev->refs = refs;
    ev->stackSize = backtrace(ev->stack, CXY_TRACE_STACK_DEPTH);
}

void __cxy_mem_trace_dump(void *ptr)
{
    fflush(stdout);
    ringDumpFd(STDOUT_FILENO, ptr, true);
}
