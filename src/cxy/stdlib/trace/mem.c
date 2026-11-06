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
#include <time.h>
#include <dlfcn.h>
#include <execinfo.h>

#include "trace_json.h"

/*
 * Memory tracing runtime. The generated program (mm_trace.h) only calls the
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
// A destroyed object whose block weak references still keep. It stays on its list
// until the block is freed but is not live: reports and the graph skip it.
#define KIND_ZOMBIE 2

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
    uint64_t zombies, zombieBytes;
    uint64_t objectAllocs; // objects registered since start
    uint64_t lastHookNs;   // when the thread last allocated or freed, for pausing
    bool parked;           // waiting at a safepoint, under pauseLock
    bool exited;           // the thread is gone
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


/*
 * Pausing the program for a consistent snapshot. Allocation and free pass through
 * __cxy_mem_trace_register/unregister, which park the calling thread while a
 * query holds the pause. A thread that has not allocated or freed for a while is
 * idle (sleeping, blocked on I/O) and is not waited for. A thread that was active
 * but does not reach a safepoint before the timeout is reported: the answer is
 * then marked approximate.
 */
#define PAUSE_ACTIVE_NS 20000000ull // a thread is active if it hooked this recently
#define PAUSE_DEFAULT_MS 200

static int pauseRequested; // atomic
static pthread_mutex_t pauseLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pauseCond = PTHREAD_COND_INITIALIZER;
static pthread_key_t threadKey;
static pthread_once_t threadKeyOnce = PTHREAD_ONCE_INIT;

static uint64_t nowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// A finished thread must not be waited for
static void threadExited(void *arg)
{
    TraceList *list = arg;
    pthread_mutex_lock(&pauseLock);
    list->exited = true;
    pthread_cond_broadcast(&pauseCond);
    pthread_mutex_unlock(&pauseLock);
}

static void createThreadKey(void)
{
    pthread_key_create(&threadKey, threadExited);
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
    pthread_once(&threadKeyOnce, createThreadKey);
    pthread_setspecific(threadKey, list);
    pthread_mutex_lock(&listsLock);
    list->next = allLists;
    allLists = list;
    pthread_mutex_unlock(&listsLock);
    threadList = list;
    return list;
}

static void parkThread(TraceList *list)
{
    pthread_mutex_lock(&pauseLock);
    list->parked = true;
    pthread_cond_broadcast(&pauseCond);
    while (__atomic_load_n(&pauseRequested, __ATOMIC_ACQUIRE))
        pthread_cond_wait(&pauseCond, &pauseLock);
    list->parked = false;
    pthread_mutex_unlock(&pauseLock);
}

// Runs at the start of every allocation and free of a registered block
static inline void safepoint(TraceList *list)
{
    __atomic_store_n(&list->lastHookNs, nowNs(), __ATOMIC_RELAXED);
    if (__builtin_expect(__atomic_load_n(&pauseRequested, __ATOMIC_ACQUIRE), 0))
        parkThread(list);
}

// Active threads that are not parked yet. Called with pauseLock held.
static uint32_t threadsStillRunning(void)
{
    uint64_t now = nowNs();
    uint32_t running = 0;
    for (TraceList *l = __atomic_load_n(&allLists, __ATOMIC_ACQUIRE); l != NULL; l = l->next) {
        if (l->exited || l->parked)
            continue;
        if (now - __atomic_load_n(&l->lastHookNs, __ATOMIC_RELAXED) < PAUSE_ACTIVE_NS)
            running++;
    }
    return running;
}

// Asks the program to stop at its next allocation or free. Returns how many
// active threads did not stop within the timeout (0 = a consistent snapshot).
static uint32_t pauseThreads(void)
{
    const char *env = getenv("CXY_MEM_PAUSE_MS");
    long timeoutMs = env != NULL ? atol(env) : PAUSE_DEFAULT_MS;
    uint64_t deadline = nowNs() + (uint64_t)timeoutMs * 1000000ull;

    __atomic_store_n(&pauseRequested, 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&pauseLock);
    uint32_t running;
    while ((running = threadsStillRunning()) > 0 && nowNs() < deadline) {
        // Wake up often: a thread that went idle after the request is not running
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 2000000;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&pauseCond, &pauseLock, &until);
    }
    pthread_mutex_unlock(&pauseLock);
    return running;
}

static void resumeThreads(void)
{
    pthread_mutex_lock(&pauseLock);
    __atomic_store_n(&pauseRequested, 0, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&pauseCond);
    pthread_mutex_unlock(&pauseLock);
}

static inline void *recPayload(const TraceRec *rec)
{
    return (char *)rec + sizeof(TraceRec) + (rec->kind != KIND_BUFFER ? OBJ_HEADER_SIZE : 0);
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
    safepoint(list);

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
    TraceList *self = threadListGet(); // the thread that frees, not the owner
    if (self != NULL)
        safepoint(self);
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
    else if (rec->kind == KIND_ZOMBIE) {
        list->zombies--;
        list->zombieBytes -= rec->size;
    }
    else {
        list->objects--;
        list->objectBytes -= rec->size;
    }
    pthread_mutex_unlock(&list->lock);
    rec->list = NULL;
}

// The object was destroyed but weak references keep the block: it is no longer live.
// Called before the owners' weak count is released, so the block is still there.
void __cxy_mem_trace_zombie(TraceRec *rec)
{
    TraceList *list = rec->list;
    if (list == NULL)
        return;
    pthread_mutex_lock(&list->lock);
    if (rec->kind == KIND_OBJECT) {
        rec->kind = KIND_ZOMBIE;
        list->objects--;
        list->objectBytes -= rec->size;
        list->zombies++;
        list->zombieBytes += rec->size;
    }
    pthread_mutex_unlock(&list->lock);
}

// Destroyed objects that weak references still keep: count and bytes
void __cxy_mem_trace_zombies(uint64_t out[2])
{
    out[0] = out[1] = 0;
    pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next) {
        pthread_mutex_lock(&l->lock);
        out[0] += l->zombies;
        out[1] += l->zombieBytes;
        pthread_mutex_unlock(&l->lock);
    }
    pthread_mutex_unlock(&listsLock);
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
            if (rec->birthTick > minGen && rec->kind != KIND_ZOMBIE)
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

// The latest lifecycle epoch of `ptr`, newest event first, ending at its alloc
static uint32_t ringEpoch(const void *ptr, const TraceEvent *epoch[64])
{
    uint32_t current = __atomic_load_n(&ringIdx, __ATOMIC_RELAXED);
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
    return epochCount;
}

static void ringDumpFd(int fd, const void *ptr, bool sym)
{
    dprintf(fd, "\n--- Lifecycle Timeline for %p in __cxy_mem_ring ---\n", ptr);
    const TraceEvent *epoch[64];
    uint32_t epochCount = ringEpoch(ptr, epoch);

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
// Optional hooks of a report: which entries to list, and a note for the end of an
// entry's line (written into `buf`, starting with a space, empty for none)
typedef struct {
    bool (*keep)(const LiveEntry *entry, void *ctx);
    void (*note)(const LiveEntry *entry, void *ctx, char *buf, size_t size);
    void *ctx;
} ReportHooks;

static const char *objectTypeName(const void *payload);

// One live allocation as a JSON object. Buffers have no refs and no type.
static void jsonLiveEntry(JsonWriter *json, const LiveEntry *e)
{
    jsonBeginObject(json);
    jsonFieldPtr(json, "ptr", e->ptr);
    jsonFieldString(json, "kind", e->buffer ? "buffer" : "object");
    jsonFieldUint(json, "size", e->size);
    if (e->buffer) {
        jsonFieldNull(json, "refs");
        jsonFieldNull(json, "type");
    }
    else {
        jsonFieldUint(json, "refs", __cxy_mem_header_refs ? __cxy_mem_header_refs(e->ptr) : 0);
        jsonFieldString(json, "type", objectTypeName(e->ptr));
    }
    jsonFieldString(json, "file", e->path);
    jsonFieldUint(json, "line", e->line);
    jsonFieldUint(json, "gen", e->gen);
    jsonEndObject(json);
}

static void liveReportJson(JsonWriter *json, const LiveEntry *snap, size_t count, size_t limit)
{
    uint64_t totalBytes = 0;
    for (size_t i = 0; i < count; i++)
        totalBytes += snap[i].size;
    size_t shown = (limit == 0 || limit > count) ? count : limit;
    jsonBeginObject(json);
    jsonKey(json, "allocations");
    jsonBeginArray(json);
    for (size_t i = 0; i < shown; i++)
        jsonLiveEntry(json, &snap[i]);
    jsonEndArray(json);
    jsonFieldUint(json, "count", count);
    jsonFieldUint(json, "bytes", totalBytes);
    jsonFieldBool(json, "limited", shown < count);
    jsonEndObject(json);
}

static void liveReportFd(int fd,
                         uint64_t minGen,
                         size_t limit,
                         bool sym,
                         bool leakStyle,
                         const ReportHooks *hooks,
                         JsonWriter *json)
{
    size_t count = 0;
    LiveEntry *snap = liveSnapshot(minGen, &count);
    if (hooks != NULL && hooks->keep != NULL) {
        size_t kept = 0;
        for (size_t i = 0; i < count; i++) {
            if (hooks->keep(&snap[i], hooks->ctx))
                snap[kept++] = snap[i];
        }
        count = kept;
    }
    if (json != NULL) {
        liveReportJson(json, snap, count, limit);
        free(snap);
        return;
    }
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
        char note[192] = "";
        if (hooks != NULL && hooks->note != NULL)
            hooks->note(e, hooks->ctx, note, sizeof(note));
        if (e->buffer) {
            dprintf(fd,
                    "%sptr=%p size=%llu buffer @ %s:%llu%s\n",
                    leakStyle ? "[LEAK] " : "",
                    e->ptr,
                    (unsigned long long)e->size,
                    e->path ? e->path : "<unknown>",
                    (unsigned long long)e->line,
                    note);
            continue;
        }
        uint32_t refs = __cxy_mem_header_refs ? __cxy_mem_header_refs(e->ptr) : 0;
        dprintf(fd,
                "%sptr=%p size=%llu refs=%u @ %s:%llu%s\n",
                leakStyle ? "[LEAK] " : "",
                e->ptr,
                (unsigned long long)e->size,
                (unsigned)refs,
                e->path ? e->path : "<unknown>",
                (unsigned long long)e->line,
                note);

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

static void jsonErrorTo(JsonWriter *json, const char *message);

typedef struct {
    LiveEntry entry;
    uint32_t refs;
} RefEntry;

// Most references first, the newest first among equals
static int compareRefsDesc(const void *lhs, const void *rhs)
{
    const RefEntry *l = lhs, *r = rhs;
    if (l->refs != r->refs)
        return l->refs < r->refs ? 1 : -1;
    return compareByGenDesc(&l->entry, &r->entry);
}

// `refs [n]`: the live objects with the most references. A very high count is a
// hot shared object, or a retain that is never released.
static void refsReportFd(int fd, size_t limit, JsonWriter *json)
{
    size_t count = 0;
    LiveEntry *snap = liveSnapshot(0, &count);
    RefEntry *items = malloc((count + 1) * sizeof(RefEntry));
    if (items == NULL) {
        free(snap);
        if (json != NULL)
            jsonErrorTo(json, "out of memory");
        else
            dprintf(fd, "error: out of memory\n");
        return;
    }
    size_t n = 0;
    uint64_t totalRefs = 0;
    for (size_t i = 0; i < count; i++) {
        if (snap[i].buffer)
            continue;
        uint32_t refs = __cxy_mem_header_refs ? __cxy_mem_header_refs(snap[i].ptr) : 0;
        items[n++] = (RefEntry){.entry = snap[i], .refs = refs};
        totalRefs += refs;
    }
    free(snap);
    qsort(items, n, sizeof(RefEntry), compareRefsDesc);
    size_t shown = (limit == 0 || limit > n) ? n : limit;
    uint32_t maxRefs = n > 0 ? items[0].refs : 0;

    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "objects");
        jsonBeginArray(json);
        for (size_t i = 0; i < shown; i++)
            jsonLiveEntry(json, &items[i].entry);
        jsonEndArray(json);
        jsonFieldUint(json, "count", n);
        jsonFieldUint(json, "maxRefs", maxRefs);
        jsonFieldUint(json, "totalRefs", totalRefs);
        jsonFieldBool(json, "limited", shown < n);
        jsonEndObject(json);
    }
    else if (n == 0) {
        dprintf(fd, "no live objects\n");
    }
    else {
        for (size_t i = 0; i < shown; i++) {
            const RefEntry *r = &items[i];
            const char *type = objectTypeName(r->entry.ptr);
            dprintf(fd,
                    "ptr=%p size=%llu refs=%u @ %s:%llu%s%s%s\n",
                    r->entry.ptr,
                    (unsigned long long)r->entry.size,
                    (unsigned)r->refs,
                    r->entry.path ? r->entry.path : "<unknown>",
                    (unsigned long long)r->entry.line,
                    type ? " (" : "",
                    type ? type : "",
                    type ? ")" : "");
        }
        dprintf(fd,
                "Total: %llu objects, max refs %u%s\n",
                (unsigned long long)n,
                (unsigned)maxRefs,
                shown < n ? ", showing the highest" : "");
    }
    free(items);
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
// A report answers as text, or as JSON when `json` is a writer
static void jsonErrorTo(JsonWriter *json, const char *message)
{
    jsonBeginObject(json);
    jsonFieldString(json, "error", message);
    jsonEndObject(json);
}

// Ends a reply: the document is complete and valid, then the line ends
static void jsonReplyEnd(JsonWriter *w, int fd)
{
    if (jsonFinish(w))
        (void)!write(fd, "\n", 1);
}

static void jsonErrorFd(int fd, const char *message, const char *cmd)
{
    JsonWriter w;
    jsonInitFd(&w, fd);
    jsonBeginObject(&w);
    jsonFieldString(&w, "error", message);
    if (cmd != NULL)
        jsonFieldString(&w, "command", cmd);
    jsonEndObject(&w);
    jsonReplyEnd(&w, fd);
}

static void sitesReportFd(int fd, uint64_t minGen, size_t limit, JsonWriter *json)
{
    size_t count = 0;
    LiveEntry *snap = liveSnapshot(minGen, &count);
    if (count == 0) {
        if (json != NULL) {
            jsonBeginObject(json);
            jsonKey(json, "sites");
            jsonBeginArray(json);
            jsonEndArray(json);
            jsonFieldUint(json, "liveAllocations", 0);
            jsonFieldUint(json, "liveBytes", 0);
            jsonFieldUint(json, "siteCount", 0);
            jsonFieldBool(json, "limited", false);
            jsonEndObject(json);
        }
        else {
            dprintf(fd, "no live allocations\n");
        }
        free(snap);
        return;
    }

    qsort(snap, count, sizeof(LiveEntry), compareSiteKey);
    SiteEntry *sites = malloc(count * sizeof(SiteEntry));
    if (sites == NULL) {
        if (json != NULL)
            jsonErrorTo(json, "out of memory");
        else
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
    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "sites");
        jsonBeginArray(json);
        for (size_t i = 0; i < shown; i++) {
            jsonBeginObject(json);
            jsonFieldString(json, "file", sites[i].path);
            jsonFieldUint(json, "line", sites[i].line);
            jsonFieldUint(json, "count", sites[i].count);
            jsonFieldUint(json, "bytes", sites[i].bytes);
            jsonEndObject(json);
        }
        jsonEndArray(json);
        jsonFieldUint(json, "liveAllocations", count);
        jsonFieldUint(json, "liveBytes", totalBytes);
        jsonFieldUint(json, "siteCount", nSites);
        jsonFieldBool(json, "limited", shown < nSites);
        jsonEndObject(json);
        free(sites);
        free(snap);
        return;
    }
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

/*
 * Destroyed objects whose block weak references still keep, newest registration
 * first on each list. At exit one is a weak reference that was never released.
 * `weak` is how many weak references are left.
 */
static void zombiesReportFd(int fd, size_t limit, bool exitStyle, JsonWriter *json)
{
    uint64_t total[2];
    __cxy_mem_trace_zombies(total);
    if (exitStyle && total[0] == 0)
        return;
    if (exitStyle)
        dprintf(fd, "=== Weak references keep destroyed objects alive ===\n");

    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "zombies");
        jsonBeginArray(json);
    }
    size_t shown = 0;
    pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next) {
        pthread_mutex_lock(&l->lock);
        for (const TraceRec *rec = l->head; rec != NULL; rec = rec->next) {
            if (rec->kind != KIND_ZOMBIE || (limit != 0 && shown >= limit))
                continue;
            // weakFlags is the second word of the header in front of the payload
            const uint32_t *flags = (const uint32_t *)((const char *)recPayload(rec) - OBJ_HEADER_SIZE + 4);
            if (json != NULL) {
                jsonBeginObject(json);
                jsonFieldPtr(json, "ptr", recPayload(rec));
                jsonFieldUint(json, "size", rec->size);
                jsonFieldUint(json, "weak", *flags >> 8);
                jsonFieldString(json, "file", rec->site ? rec->site->file : NULL);
                jsonFieldUint(json, "line", rec->site ? rec->site->line : 0);
                jsonEndObject(json);
                shown++;
                continue;
            }
            dprintf(fd,
                    "%sptr=%p size=%llu weak=%u @ %s:%llu\n",
                    exitStyle ? "[ZOMBIE] " : "",
                    recPayload(rec),
                    (unsigned long long)rec->size,
                    (unsigned)(*flags >> 8),
                    rec->site ? rec->site->file : "<unknown>",
                    (unsigned long long)(rec->site ? rec->site->line : 0));
            shown++;
        }
        pthread_mutex_unlock(&l->lock);
    }
    pthread_mutex_unlock(&listsLock);
    if (json != NULL) {
        jsonEndArray(json);
        jsonFieldUint(json, "total", total[0]);
        jsonFieldUint(json, "bytes", total[1]);
        jsonFieldBool(json, "limited", shown < total[0]);
        jsonEndObject(json);
        return;
    }
    dprintf(fd,
            "Total: %llu zombies (%llu bytes)\n",
            (unsigned long long)total[0],
            (unsigned long long)total[1]);
}

static void summaryFd(int fd, JsonWriter *json)
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

    uint64_t zombies[2];
    __cxy_mem_trace_zombies(zombies);
    if (json != NULL) {
        // every key is always there, a consumer does not have to guess
        jsonBeginObject(json);
        jsonKey(json, "objects");
        jsonBeginObject(json);
        jsonFieldUint(json, "count", count);
        jsonFieldUint(json, "bytes", bytes);
        jsonEndObject(json);
        jsonKey(json, "buffers");
        jsonBeginObject(json);
        jsonFieldUint(json, "count", buffers);
        jsonFieldUint(json, "bytes", bufferBytes);
        jsonEndObject(json);
        jsonKey(json, "zombies");
        jsonBeginObject(json);
        jsonFieldUint(json, "count", zombies[0]);
        jsonFieldUint(json, "bytes", zombies[1]);
        jsonEndObject(json);
        jsonFieldUint(json, "objectsAllocated", gen);
        jsonFieldUint(json, "traceEvents", __atomic_load_n(&ringIdx, __ATOMIC_RELAXED));
        jsonFieldUint(json, "mark", __atomic_load_n(&markGen, __ATOMIC_RELAXED));
        jsonEndObject(json);
        return;
    }

    // only shown when there is one, programs without weak references print what they did
    char zombieLine[96] = "";
    if (zombies[0] > 0)
        snprintf(zombieLine,
                 sizeof(zombieLine),
                 "zombies: %llu destroyed, %llu bytes\n",
                 (unsigned long long)zombies[0],
                 (unsigned long long)zombies[1]);
    dprintf(fd,
            "live: %llu allocations, %llu bytes\n"
            "buffers: %llu live, %llu bytes\n"
            "%s"
            "allocations since start: %llu\n"
            "trace events: %u\n"
            "mark: %llu\n",
            (unsigned long long)count,
            (unsigned long long)bytes,
            (unsigned long long)buffers,
            (unsigned long long)bufferBytes,
            zombieLine,
            (unsigned long long)gen,
            (unsigned)__atomic_load_n(&ringIdx, __ATOMIC_RELAXED),
            (unsigned long long)__atomic_load_n(&markGen, __ATOMIC_RELAXED));
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

// Sums every thread's arrays into freshly allocated tables. A forked child passes
// `locked` false: the lock may have been held by a thread that is not in the copy.
static void sumCountersEx(SiteCount **sites, TypeCount **types, bool locked)
{
    uint32_t ns = siteTableSize(), nt = typeTableSize();
    *sites = calloc(ns + 1, sizeof(SiteCount));
    *types = calloc(nt + 1, sizeof(TypeCount));
    if (*sites == NULL || *types == NULL)
        return;

    if (locked)
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
    if (locked)
        pthread_mutex_unlock(&countersLock);
}

static void sumCounters(SiteCount **sites, TypeCount **types)
{
    sumCountersEx(sites, types, true);
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
static void typesReportFd(int fd, JsonWriter *json)
{
    SiteCount *sites = NULL;
    TypeCount *types = NULL;
    sumCounters(&sites, &types);
    uint32_t nt = typeTableSize();
    if (types == NULL || nt == 0) {
        if (json != NULL)
            jsonErrorTo(json, "no type information, was the program built with --memory-trace?");
        else
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

    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "types");
        jsonBeginArray(json);
        for (size_t i = 0; i < n; i++) {
            const TypeCount *t = &types[rank[i].index];
            const TypeRecord *info = __cxy_types[rank[i].index];
            jsonBeginObject(json);
            jsonFieldString(json, "name", info != NULL ? info->name : NULL);
            jsonFieldUint(json, "allocs", t->allocs);
            jsonFieldUint(json, "frees", t->frees);
            jsonFieldUint(json, "live", t->allocs > t->frees ? t->allocs - t->frees : 0);
            jsonFieldUint(json, "bytes", t->bytes);
            jsonEndObject(json);
        }
        jsonEndArray(json);
        jsonFieldUint(json, "total", n);
        jsonEndObject(json);
        free(rank);
        free(sites);
        free(types);
        return;
    }

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
static void siteCountsReportFd(int fd, size_t limit, JsonWriter *json)
{
    SiteCount *sites = NULL;
    TypeCount *types = NULL;
    sumCounters(&sites, &types);
    uint32_t ns = siteTableSize();
    if (sites == NULL || ns == 0) {
        if (json != NULL)
            jsonErrorTo(json, "no site information, was the program built with --memory-trace?");
        else
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
    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "sites");
        jsonBeginArray(json);
        for (size_t i = 0; i < shown; i++) {
            const SiteCount *s = &sites[rank[i].index];
            const SiteRecord *info = __cxy_sites[rank[i].index];
            jsonBeginObject(json);
            jsonFieldString(json, "file", info != NULL ? info->file : NULL);
            jsonFieldUint(json, "line", info != NULL ? info->line : 0);
            jsonFieldUint(json, "column", info != NULL ? info->column : 0);
            jsonFieldString(json, "function", info != NULL ? info->function : NULL);
            jsonFieldUint(json, "allocs", s->allocs);
            jsonFieldUint(json, "bytes", s->bytes);
            jsonEndObject(json);
        }
        jsonEndArray(json);
        jsonFieldUint(json, "total", n);
        jsonFieldBool(json, "limited", shown < n);
        jsonEndObject(json);
        free(rank);
        free(sites);
        free(types);
        return;
    }
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

/* ------------------------------------------------------------------- edges */

/*
 * `edges <ptr>`: the owned references of one object, as reported by its
 * TypeInfo.visit glue (class handles, and the buffers its raw pointer fields own). Only targets the
 * registry knows are printed. The object is read while the program runs, so a
 * concurrent free can make this report garbage: it is a debugging aid until the
 * paused snapshot of the exact graph exists.
 */
typedef struct TraceVisitor {
    void (*edge)(struct TraceVisitor *v, const void *from, const void *to);
    uint64_t (*bufferBytes)(struct TraceVisitor *v, const void *ptr);
    void (*borrow)(struct TraceVisitor *v, const void *from, const void *to);
    void *ctx;
} TraceVisitor; // mirrors __Visitor in runtime/mm_trace.h

typedef struct {
    int fd;
    const void *self;
    const char *selfName; // how the source is printed when it is `self`
    LiveEntry *sorted;    // registry snapshot ordered by address
    size_t count;
    size_t edges;
    JsonWriter *json; // edges are written as JSON objects into an open array
} EdgePrinter;

static int compareByPtr(const void *lhs, const void *rhs)
{
    const void *l = ((const LiveEntry *)lhs)->ptr, *r = ((const LiveEntry *)rhs)->ptr;
    return l < r ? -1 : (l > r ? 1 : 0);
}

static const LiveEntry *findEntry(const EdgePrinter *p, const void *ptr)
{
    LiveEntry key = {.ptr = (void *)ptr};
    return bsearch(&key, p->sorted, p->count, sizeof(LiveEntry), compareByPtr);
}

static uint64_t bufferBytesOf(TraceVisitor *v, const void *ptr)
{
    const LiveEntry *entry = findEntry(v->ctx, ptr);
    return entry != NULL && entry->buffer ? entry->size : 0;
}

static void printEdge(TraceVisitor *v, const void *from, const void *to)
{
    EdgePrinter *p = v->ctx;
    const LiveEntry *target = findEntry(p, to);
    if (target == NULL)
        return;
    const LiveEntry *source = from == p->self ? NULL : findEntry(p, from);
    if (p->json != NULL) {
        const char *fromKind = from == p->self ? (p->selfName ? "global" : "object")
                                               : (source && source->buffer ? "buffer" : "object");
        jsonBeginObject(p->json);
        jsonFieldPtr(p->json, "from", from);
        jsonFieldString(p->json, "fromKind", fromKind);
        jsonFieldPtr(p->json, "to", to);
        jsonFieldString(p->json, "toKind", target->buffer ? "buffer" : "object");
        jsonFieldUint(p->json, "size", target->size);
        jsonFieldString(p->json, "file", target->path);
        jsonFieldUint(p->json, "line", target->line);
        jsonEndObject(p->json);
        p->edges++;
        return;
    }
    char src[64];
    if (from == p->self)
        snprintf(src, sizeof(src), "%s", p->selfName ? p->selfName : "self");
    else
        snprintf(src, sizeof(src), "%s %p", source && source->buffer ? "buffer" : "object", from);
    dprintf(p->fd,
            "  %s -> %p %s size=%llu @ %s:%llu\n",
            src,
            to,
            target->buffer ? "buffer" : "object",
            (unsigned long long)target->size,
            target->path ? target->path : "<unknown>",
            (unsigned long long)target->line);
    p->edges++;
}

#define HEADER_LEGACY_FLAG 0x08U

typedef void (*VisitFn)(void *obj, void *visitor);

// The edge visitor generated for the class of the object at `payload`, or NULL for
// an object without one (old-style allocation, class with no TypeInfo.visit)
static VisitFn objectVisitFn(const void *payload, const char **typeName)
{
    // The object header sits 16 bytes before the payload: strong, flags, type
    const uint32_t *flags = (const uint32_t *)((const char *)payload - OBJ_HEADER_SIZE + 4);
    const TypeRecord *const *type = (const TypeRecord *const *)((const char *)payload - 8);
    if ((*flags & HEADER_LEGACY_FLAG) || *type == NULL)
        return NULL;
    if (typeName != NULL)
        *typeName = (*type)->name;
    return (VisitFn)(*type)->visit;
}

static const char *objectTypeName(const void *payload)
{
    const char *name = NULL;
    objectVisitFn(payload, &name);
    return name;
}

// Set in the forked child before it runs a command
static JsonWriter *graphJson;
static uint32_t graphApproximate; // threads that did not reach a safepoint

// A one-line failure: plain text, or a JSON error document
static void reportError(int fd, JsonWriter *json, const char *text)
{
    if (json != NULL)
        jsonErrorTo(json, text);
    else
        dprintf(fd, "%s\n", text);
}

static void edgesReportFd(int fd, const char *arg, JsonWriter *json)
{
    if (arg == NULL) {
        reportError(fd, json, "usage: edges <ptr>");
        return;
    }
    void *ptr = (void *)(uintptr_t)strtoull(arg, NULL, 0);

    EdgePrinter p = {.fd = fd, .self = ptr, .json = json};
    p.sorted = liveSnapshot(0, &p.count);
    qsort(p.sorted, p.count, sizeof(LiveEntry), compareByPtr);
    const LiveEntry *entry = findEntry(&p, ptr);
    char message[160];
    if (entry == NULL) {
        snprintf(message, sizeof(message), "no live allocation at %p", ptr);
        reportError(fd, json, message);
        free(p.sorted);
        return;
    }
    if (entry->buffer) {
        snprintf(message,
                 sizeof(message),
                 "%p is a buffer, its edges are reported by its owner (try `edges <owner>`)",
                 ptr);
        reportError(fd, json, message);
        free(p.sorted);
        return;
    }

    const char *typeName = NULL;
    VisitFn visit = objectVisitFn(ptr, &typeName);
    if (visit == NULL) {
        snprintf(message, sizeof(message), "%p has no edge visitor", ptr);
        reportError(fd, json, message);
        free(p.sorted);
        return;
    }

    if (json != NULL) {
        jsonBeginObject(json);
        jsonFieldPtr(json, "object", ptr);
        jsonFieldString(json, "type", typeName);
        jsonKey(json, "edges");
        jsonBeginArray(json);
    }
    else {
        dprintf(fd, "edges of %p (%s)\n", ptr, typeName ? typeName : "<unknown>");
    }
    TraceVisitor visitor = {.edge = printEdge, .bufferBytes = bufferBytesOf, .ctx = &p};
    visit(ptr, &visitor);
    if (json != NULL) {
        jsonEndArray(json);
        jsonFieldUint(json, "total", p.edges);
        jsonFieldUint(json, "approximate", graphApproximate);
        jsonEndObject(json);
    }
    else {
        dprintf(fd, "Total: %llu edges\n", (unsigned long long)p.edges);
    }
    free(p.sorted);
}

/*
 * `dangling`: a `&T` field that points at something the registry no longer knows,
 * the owner was destroyed while the borrow was still stored. The timeline of the
 * target shows where it was allocated and where it was freed.
 */
typedef struct {
    EdgePrinter printer;
    const void *owner;
    const char *ownerType;
    size_t found;
    JsonWriter *json;
} DanglingScan;


static void jsonDanglingBorrow(DanglingScan *scan, const LiveEntry *owner, const void *from, const void *to)
{
    JsonWriter *json = scan->json;
    jsonBeginObject(json);
    jsonFieldPtr(json, "owner", from);
    jsonFieldString(json, "ownerType", scan->ownerType);
    jsonFieldString(json, "file", owner ? owner->path : NULL);
    jsonFieldUint(json, "line", owner ? owner->line : 0);
    jsonFieldPtr(json, "target", to);
    jsonKey(json, "events");
    jsonBeginArray(json);
    const TraceEvent *epoch[64];
    uint32_t n = ringEpoch(to, epoch);
    for (uint32_t i = 0; i < n; i++) {
        const TraceEvent *ev = epoch[n - 1 - i];
        jsonBeginObject(json);
        jsonFieldString(json, "op", ev->op);
        jsonFieldUint(json, "refs", ev->refs);
        jsonFieldString(json, "file", ev->path);
        jsonFieldUint(json, "line", ev->line);
        jsonEndObject(json);
    }
    jsonEndArray(json);
    jsonEndObject(json);
}

static void reportDanglingBorrow(TraceVisitor *v, const void *from, const void *to)
{
    DanglingScan *scan = v->ctx;
    if (findEntry(&scan->printer, to) != NULL)
        return;
    const LiveEntry *owner = findEntry(&scan->printer, scan->owner);
    if (scan->json != NULL) {
        jsonDanglingBorrow(scan, owner, from, to);
        scan->found++;
        return;
    }
    dprintf(scan->printer.fd,
            "dangling: %s %p @ %s:%llu borrows %p, which is no longer live\n",
            scan->ownerType ? scan->ownerType : "object",
            from,
            owner && owner->path ? owner->path : "<unknown>",
            (unsigned long long)(owner ? owner->line : 0),
            to);
    ringDumpFd(scan->printer.fd, to, false);
    scan->found++;
}

// The scan reads borrows only: owned references and buffers are visited too, they
// have to be answered but are not looked at
static void ignoreEdge(TraceVisitor *v, const void *from, const void *to)
{
    (void)v;
    (void)from;
    (void)to;
}

static uint64_t noBufferBytes(TraceVisitor *v, const void *ptr)
{
    (void)v;
    (void)ptr;
    return 0;
}

static void danglingReportFd(int fd, JsonWriter *json)
{
    DanglingScan scan = {.printer = {.fd = fd}, .json = json};
    scan.printer.sorted = liveSnapshot(0, &scan.printer.count);
    qsort(scan.printer.sorted, scan.printer.count, sizeof(LiveEntry), compareByPtr);
    TraceVisitor visitor = {.edge = ignoreEdge,
                            .bufferBytes = noBufferBytes,
                            .borrow = reportDanglingBorrow,
                            .ctx = &scan};
    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "dangling");
        jsonBeginArray(json);
    }
    for (size_t i = 0; i < scan.printer.count; i++) {
        const LiveEntry *entry = &scan.printer.sorted[i];
        if (entry->buffer)
            continue;
        const char *typeName = NULL;
        VisitFn visit = objectVisitFn(entry->ptr, &typeName);
        if (visit == NULL)
            continue;
        scan.owner = entry->ptr;
        scan.ownerType = typeName;
        visit(entry->ptr, &visitor);
    }
    if (json != NULL) {
        jsonEndArray(json);
        jsonFieldUint(json, "total", scan.found);
        jsonFieldUint(json, "approximate", graphApproximate);
        jsonEndObject(json);
        free(scan.printer.sorted);
        return;
    }
    dprintf(fd, "Total: %llu dangling borrows\n", (unsigned long long)scan.found);
    free(scan.printer.sorted);
}

/* -------------------------------------------------------------------- roots */

// A global variable that holds owning references, emitted by the compiler in
// trace builds. Mirrors __RootInfo in runtime/mm.h.
typedef struct {
    const char *name;
    void *addr;
    uint64_t size;
    void (*visit)(void *visitor, void *root);
} RootRecord;

__attribute__((weak)) extern const RootRecord __cxy_roots[];
__attribute__((weak)) extern const uint32_t __cxy_root_count;

/*
 * `roots`: every global that owns something, and what it owns directly. A global
 * is read where it lives, so like `edges` this is racy until the paused snapshot.
 */
static void rootsReportFd(int fd, JsonWriter *json)
{
    uint32_t count = &__cxy_root_count != NULL ? __cxy_root_count : 0;
    if (count == 0 && json == NULL) {
        dprintf(fd, "no roots: no global variable owns a class, buffer or closure\n");
        return;
    }

    EdgePrinter p = {.fd = fd, .json = json};
    p.sorted = liveSnapshot(0, &p.count);
    qsort(p.sorted, p.count, sizeof(LiveEntry), compareByPtr);
    TraceVisitor visitor = {.edge = printEdge, .bufferBytes = bufferBytesOf, .ctx = &p};

    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "roots");
        jsonBeginArray(json);
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        const RootRecord *root = &__cxy_roots[i];
        if (json != NULL) {
            jsonBeginObject(json);
            jsonFieldString(json, "name", root->name);
            jsonFieldPtr(json, "ptr", root->addr);
            jsonFieldUint(json, "size", root->size);
            jsonKey(json, "edges");
            jsonBeginArray(json);
        }
        else {
            dprintf(fd, "global %s (%p, %llu bytes)\n", root->name, root->addr, (unsigned long long)root->size);
        }
        p.self = root->addr;
        p.selfName = root->name;
        p.edges = 0;
        root->visit(&visitor, root->addr);
        if (json != NULL) {
            jsonEndArray(json);
            jsonEndObject(json);
        }
        else if (p.edges == 0) {
            dprintf(fd, "  (owns nothing right now)\n");
        }
        total += p.edges;
    }
    if (json != NULL) {
        jsonEndArray(json);
        jsonFieldUint(json, "total", count);
        jsonFieldUint(json, "edges", total);
        jsonFieldUint(json, "approximate", graphApproximate);
        jsonEndObject(json);
    }
    else {
        dprintf(fd, "Total: %u roots, %llu edges\n", count, (unsigned long long)total);
    }
    free(p.sorted);
}

/* ------------------------------------------------------------ query server */

static void helpFd(int fd)
{
    dprintf(fd,
            "commands (append 'nosym' to skip symbolization, 'json' for the answer as one "
            "line of JSON):\n"
            "  summary              live count/bytes and counters\n"
            "  sites [n]            live allocations grouped by site (default %d)\n"
            "  list [n]             most recent live allocations with stacks (default %d)\n"
            "  leaks                full report, same as the exit-time leak report\n"
            "  ring <ptr>           lifecycle timeline of one pointer\n"
            "  mark                 remember the current allocation generation and counters\n"
            "  baseline             what `mark` remembered\n"
            "  refs [n]             live objects with the most references (default %d)\n"
            "  snapshot json        counters and graph findings in one paused copy (JSON only)\n"
            "  edges <ptr>          what this object owns (exact, from the visit glue)\n"
            "  roots                global variables that own something, and what they own\n"
            "  dangling             `&T` fields whose target was destroyed\n"
            "  zombies [n]          destroyed objects that weak references still keep\n"
            "  types                cumulative allocs/frees/bytes per class\n"
            "  sitecounts [n]       cumulative allocations per allocation point\n"
            "  diff [n]             sites of allocations made since `mark` still alive\n"
            "  difflist [n]         like `list`, but only allocations since `mark`\n"
            "ownership graph (exact edges, built in a forked copy of the process):\n"
            "  holders <ptr>        who owns this object, and its externalRefs\n"
            "  path <ptr>           shortest chain from a root to this object\n"
            "  contains <y> <x>     is x owned, directly or indirectly, by y\n"
            "  retained [n]         roots ranked by what only they keep alive\n"
            "  cycles [n]           leaked cycles: groups that only own each other\n"
            "  tree <ptr> [depth]   what this object owns (default depth 3)\n"
            "json: every command above has a JSON form (`sites 5 json`, `path <ptr> json`) "
            "except `ring`,\n"
            "      `leaks` and `help`, which answer {\"error\": ...}. `snapshot` answers only as JSON.\n",
            CXY_QUERY_DEFAULT_LIMIT,
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
 * Who owns what. Every live object and buffer is a node, and so is every named
 * global root. An edge is an owned reference, as reported by the visit glue the
 * compiler generated from the field types: nothing is guessed from memory
 * contents. The graph is built from a forked copy of the process when a command
 * asks for it, so the program is never stalled and the child sees a consistent
 * snapshot of the registry.
 *
 * `externalRefs = strong - incoming edges` of an object is how many of its
 * references are held outside the heap: by a stack, a coroutine, or a global the
 * compiler did not name. Nodes with no incoming edge are the roots.
 */

#define GRAPH_NONE UINT32_MAX
#define GRAPH_MULTI UINT32_MAX

typedef struct {
    uintptr_t start;
    uint64_t size;
    const char *path;
    uint64_t line;
    const char *name; // roots: the global's name
    const char *type; // objects: the class name, when the object has TypeInfo
    uint32_t refs;    // objects: the strong count when the graph was built
    bool raw;         // a buffer, not an object
    bool root;        // a named global
} GraphNode;

typedef struct {
    uint32_t from, to;
} GraphEdge;

typedef struct {
    uintptr_t start;
    uint32_t id;
} GraphKey;

typedef struct {
    GraphNode *nodes;
    size_t count, cap;
    GraphKey *keys; // every node ordered by address
    size_t keyCount;
    GraphEdge *edges;
    size_t edgeCount, edgeCap;
    uint32_t *outStart, *outEdge;
    uint32_t *inStart, *inEdge;
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

// The node that starts at exactly `addr`, or GRAPH_NONE. Edges always name the
// start of an object, a buffer or a root, never an address inside one.
static uint32_t graphFind(const Graph *g, uintptr_t addr)
{
    size_t lo = 0, hi = g->keyCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g->keys[mid].start < addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < g->keyCount && g->keys[lo].start == addr)
        return g->keys[lo].id;
    return GRAPH_NONE;
}

static bool graphAddEdge(Graph *g, uint32_t from, uint32_t to)
{
    if (g->edgeCount == g->edgeCap) {
        size_t cap = g->edgeCap ? g->edgeCap * 2 : 4096;
        GraphEdge *edges = realloc(g->edges, cap * sizeof(GraphEdge));
        if (edges == NULL)
            return false;
        g->edges = edges;
        g->edgeCap = cap;
    }
    g->edges[g->edgeCount++] = (GraphEdge){.from = from, .to = to};
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

typedef struct {
    Graph *g;
    bool failed;
} CollectCtx;

// An edge counts when both ends are nodes of the graph: a reference to memory the
// registry does not know (static data, an object made before tracing started) is
// not an edge
static void collectEdge(TraceVisitor *v, const void *from, const void *to)
{
    CollectCtx *c = v->ctx;
    uint32_t f = graphFind(c->g, (uintptr_t)from), t = graphFind(c->g, (uintptr_t)to);
    if (f == GRAPH_NONE || t == GRAPH_NONE)
        return;
    if (!graphAddEdge(c->g, f, t))
        c->failed = true;
}

static uint64_t collectBufferBytes(TraceVisitor *v, const void *ptr)
{
    const Graph *g = ((CollectCtx *)v->ctx)->g;
    uint32_t id = graphFind(g, (uintptr_t)ptr);
    return id != GRAPH_NONE && g->nodes[id].raw && !g->nodes[id].root ? g->nodes[id].size : 0;
}

// Runs in the forked child: the registry is a private, consistent copy, no lock
// is needed and the visit functions read the copy of the program's memory
static bool graphBuild(Graph *g)
{
    for (const TraceList *l = allLists; l != NULL; l = l->next) {
        for (const TraceRec *rec = l->head; rec != NULL; rec = rec->next) {
            // a zombie owns nothing any more, its fields were dropped
            if (rec->kind == KIND_ZOMBIE)
                continue;
            GraphNode node = {.start = (uintptr_t)recPayload(rec),
                              .size = rec->size,
                              .path = rec->site ? rec->site->file : NULL,
                              .line = rec->site ? rec->site->line : 0,
                              .raw = rec->kind == KIND_BUFFER};
            if (!node.raw && __cxy_mem_header_refs)
                node.refs = __cxy_mem_header_refs((void *)node.start);
            if (!node.raw)
                node.type = objectTypeName((const void *)node.start);
            if (graphAddNode(g, node) == GRAPH_NONE)
                return false;
        }
    }

    uint32_t rootCount = &__cxy_root_count != NULL ? __cxy_root_count : 0;
    for (uint32_t i = 0; i < rootCount; i++) {
        const RootRecord *root = &__cxy_roots[i];
        GraphNode node = {.start = (uintptr_t)root->addr,
                          .size = root->size,
                          .name = root->name,
                          .raw = true,
                          .root = true};
        if (graphAddNode(g, node) == GRAPH_NONE)
            return false;
    }

    g->keys = malloc((g->count ? g->count : 1) * sizeof(GraphKey));
    if (g->keys == NULL)
        return false;
    for (size_t i = 0; i < g->count; i++)
        g->keys[i] = (GraphKey){.start = g->nodes[i].start, .id = (uint32_t)i};
    g->keyCount = g->count;
    qsort(g->keys, g->keyCount, sizeof(GraphKey), compareGraphKey);

    CollectCtx ctx = {.g = g};
    TraceVisitor visitor = {.edge = collectEdge, .bufferBytes = collectBufferBytes, .ctx = &ctx};
    size_t objects = g->count - rootCount;
    for (size_t id = 0; id < objects && !ctx.failed; id++) {
        const GraphNode *n = &g->nodes[id];
        VisitFn visit = n->raw ? NULL : objectVisitFn((const void *)n->start, NULL);
        if (visit != NULL)
            visit((void *)n->start, &visitor);
    }
    for (uint32_t i = 0; i < rootCount && !ctx.failed; i++)
        __cxy_roots[i].visit(&visitor, __cxy_roots[i].addr);
    return !ctx.failed && graphIndex(g);
}

static void printGraphNode(int fd, const Graph *g, uint32_t id)
{
    const GraphNode *n = &g->nodes[id];
    if (n->root) {
        dprintf(fd, "global %s (%p, %llu bytes)", n->name, (void *)n->start, (unsigned long long)n->size);
    }
    else if (!n->raw) {
        dprintf(fd,
                "%p size=%llu refs=%u @ %s:%llu",
                (void *)n->start,
                (unsigned long long)n->size,
                (unsigned)n->refs,
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
static void printChain(int fd, const Graph *g, const uint32_t *ids, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dprintf(fd, "%*s", (int)(2 + i * 2), "");
        if (i > 0)
            dprintf(fd, "-> ");
        printGraphNode(fd, g, ids[i]);
        dprintf(fd, "\n");
    }
}

// The fields of a node, written into an open JSON object. Fields that do not
// apply to the kind of node are null.
static void jsonGraphNodeFields(JsonWriter *json, const Graph *g, uint32_t id)
{
    const GraphNode *n = &g->nodes[id];
    jsonFieldString(json, "kind", n->root ? "global" : (n->raw ? "buffer" : "object"));
    jsonFieldPtr(json, "ptr", (void *)n->start);
    jsonFieldUint(json, "size", n->size);
    jsonFieldString(json, "name", n->name);
    if (n->raw)
        jsonFieldNull(json, "refs");
    else
        jsonFieldUint(json, "refs", n->refs);
    jsonFieldString(json, "type", n->type);
    jsonFieldString(json, "file", n->path);
    jsonFieldUint(json, "line", n->line);
}

static void jsonGraphNode(JsonWriter *json, const Graph *g, uint32_t id)
{
    jsonBeginObject(json);
    jsonGraphNodeFields(json, g, id);
    jsonEndObject(json);
}

// `"<key>": [node, ...]`
static void jsonGraphNodes(JsonWriter *json, const char *key, const Graph *g, const uint32_t *ids, size_t len)
{
    jsonKey(json, key);
    jsonBeginArray(json);
    for (size_t i = 0; i < len; i++)
        jsonGraphNode(json, g, ids[i]);
    jsonEndArray(json);
}

static void jsonGraphEnd(JsonWriter *json)
{
    jsonFieldUint(json, "approximate", graphApproximate);
    jsonEndObject(json);
}

static uint32_t graphResolve(int fd, Graph *g, const char *arg, JsonWriter *json)
{
    if (arg == NULL) {
        reportError(fd, json, "usage: <command> <ptr>");
        return GRAPH_NONE;
    }
    uintptr_t addr = (uintptr_t)strtoull(arg, NULL, 0);
    uint32_t id = graphFind(g, addr);
    if (id == GRAPH_NONE) {
        char message[320];
        snprintf(message, sizeof(message), "%s is not the start of a live object, buffer or global root", arg);
        reportError(fd, json, message);
    }
    return id;
}

static void graphHolders(int fd, const Graph *g, uint32_t id, JsonWriter *json)
{
    if (json != NULL) {
        const GraphNode *n = &g->nodes[id];
        size_t holders = g->inStart[id + 1] - g->inStart[id];
        jsonBeginObject(json);
        jsonKey(json, "node");
        jsonGraphNode(json, g, id);
        jsonKey(json, "holders");
        jsonBeginArray(json);
        size_t listed = 0;
        for (uint32_t k = g->inStart[id]; k < g->inStart[id + 1] && listed < 50; k++, listed++)
            jsonGraphNode(json, g, g->edges[g->inEdge[k]].from);
        jsonEndArray(json);
        jsonFieldUint(json, "holderCount", holders);
        if (n->raw)
            jsonFieldNull(json, "externalRefs");
        else
            jsonFieldUint(json, "externalRefs", n->refs > holders ? n->refs - holders : 0);
        jsonFieldBool(json, "inconsistent", !n->raw && n->refs < holders);
        jsonGraphEnd(json);
        return;
    }
    printGraphNode(fd, g, id);
    dprintf(fd, "\n");
    size_t holders = 0;
    for (uint32_t k = g->inStart[id]; k < g->inStart[id + 1]; k++) {
        const GraphEdge *e = &g->edges[g->inEdge[k]];
        if (holders++ < 50) {
            dprintf(fd, "  held by ");
            printGraphNode(fd, g, e->from);
            dprintf(fd, "\n");
        }
    }
    if (holders > 50)
        dprintf(fd, "  ... %llu more\n", (unsigned long long)(holders - 50));
    if (holders == 0)
        dprintf(fd, "  not owned by any object, buffer or named global\n");

    // externalRefs: the references the glue cannot account for are held by a
    // stack, a coroutine, or a global the compiler did not name
    const GraphNode *n = &g->nodes[id];
    if (!n->raw && n->refs > holders)
        dprintf(fd,
                "  externalRefs = %llu (%u reference(s), %llu held by heap objects)\n",
                (unsigned long long)(n->refs - holders),
                (unsigned)n->refs,
                (unsigned long long)holders);
    else if (!n->raw && n->refs < holders)
        dprintf(fd,
                "  note: %llu edges but only %u reference(s), the graph is approximate here\n",
                (unsigned long long)holders,
                (unsigned)n->refs);
}

// Shortest chain from a node nothing tracked points at, down to `target`
static void graphPath(int fd, const Graph *g, uint32_t target, JsonWriter *json)
{
    size_t n = g->count;
    uint32_t *queue = malloc(n * sizeof(uint32_t)), *prev = malloc(n * sizeof(uint32_t));
    uint32_t *ids = malloc(n * sizeof(uint32_t));
    uint8_t *seen = calloc(n, 1);
    if (!queue || !prev || !ids || !seen) {
        reportError(fd, json, json ? "out of memory" : "error: out of memory");
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
        if (json != NULL) {
            jsonBeginObject(json);
            jsonFieldBool(json, "found", false);
            jsonFieldString(json, "reason", "cycle");
            jsonFieldNull(json, "depth");
            jsonFieldNull(json, "heldFromOutside");
            jsonGraphNodes(json, "path", g, NULL, 0);
            jsonGraphEnd(json);
        }
        else {
            dprintf(fd, "no root: every chain of holders ends in a cycle\n");
        }
        goto done;
    }

    size_t len = 0;
    uint32_t v = root;
    ids[len++] = v;
    while (v != target) {
        v = g->edges[prev[v]].to;
        ids[len++] = v;
    }
    if (json != NULL) {
        jsonBeginObject(json);
        jsonFieldBool(json, "found", true);
        jsonFieldNull(json, "reason");
        jsonFieldUint(json, "depth", len - 1);
        jsonFieldBool(json, "heldFromOutside", !g->nodes[root].root);
        jsonGraphNodes(json, "path", g, ids, len);
        jsonGraphEnd(json);
        goto done;
    }
    dprintf(fd, "retention path (%llu holders deep):\n", (unsigned long long)(len - 1));
    printChain(fd, g, ids, len);
    if (!g->nodes[root].root)
        dprintf(fd, "  (the first object is held from outside the heap: a stack, a coroutine or an unnamed global)\n");

done:
    free(queue);
    free(prev);
    free(ids);
    free(seen);
}

// Is `inner` held, directly or through other objects and buffers, by `outer`
static void jsonContainsDoc(JsonWriter *json, bool contains, bool same, const Graph *g, const uint32_t *chain, size_t len)
{
    jsonBeginObject(json);
    jsonFieldBool(json, "contains", contains);
    jsonFieldBool(json, "same", same);
    jsonFieldUint(json, "steps", len > 0 ? len - 1 : 0);
    jsonGraphNodes(json, "path", g, chain, len);
    jsonGraphEnd(json);
}

static void graphContains(int fd, const Graph *g, uint32_t outer, uint32_t inner, JsonWriter *json)
{
    if (outer == inner) {
        if (json != NULL)
            jsonContainsDoc(json, false, true, g, NULL, 0);
        else
            dprintf(fd, "same allocation\n");
        return;
    }
    size_t n = g->count;
    uint32_t *queue = malloc(n * sizeof(uint32_t)), *prev = malloc(n * sizeof(uint32_t));
    uint32_t *ids = malloc(n * sizeof(uint32_t));
    uint8_t *seen = calloc(n, 1);
    if (!queue || !prev || !ids || !seen) {
        reportError(fd, json, json ? "out of memory" : "error: out of memory");
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
        if (json != NULL)
            jsonContainsDoc(json, false, false, g, NULL, 0);
        else
            dprintf(fd, "no, %p does not hold %p\n", (void *)g->nodes[outer].start, (void *)g->nodes[inner].start);
        goto done;
    }

    // Walk back from `inner`, then print from `outer` down
    size_t len = 0;
    for (uint32_t v = inner; v != outer; v = g->edges[prev[v]].from)
        ids[len++] = v;
    uint32_t *chain = queue; // queue is no longer needed
    chain[0] = outer;
    for (size_t i = 0; i < len; i++)
        chain[i + 1] = ids[len - 1 - i];
    if (json != NULL) {
        jsonContainsDoc(json, true, false, g, chain, len + 1);
        goto done;
    }
    dprintf(fd, "yes, held through %llu step(s):\n", (unsigned long long)len);
    printChain(fd, g, chain, len + 1);

done:
    free(queue);
    free(prev);
    free(ids);
    free(seen);
}

static void jsonTreeNode(JsonWriter *json, const Graph *g, uint32_t id, int depth, int maxDepth, uint8_t *seen)
{
    uint32_t children = g->outStart[id + 1] - g->outStart[id];
    bool shownAbove = seen[id];
    bool cut = depth >= maxDepth;
    seen[id] = 1;
    jsonBeginObject(json);
    jsonGraphNodeFields(json, g, id);
    jsonFieldBool(json, "seen", shownAbove);
    jsonFieldBool(json, "truncated", !shownAbove && cut && children > 0);
    jsonFieldUint(json, "moreChildren", !shownAbove && !cut && children > 20 ? children - 20 : 0);
    jsonKey(json, "children");
    jsonBeginArray(json);
    if (!shownAbove && !cut) {
        uint32_t shown = 0;
        for (uint32_t k = g->outStart[id]; k < g->outStart[id + 1] && shown < 20; k++, shown++)
            jsonTreeNode(json, g, g->edges[g->outEdge[k]].to, depth + 1, maxDepth, seen);
    }
    jsonEndArray(json);
    jsonEndObject(json);
}

static void graphTreeNode(int fd,
                          const Graph *g,
                          uint32_t id,
                          int depth,
                          int maxDepth,
                          uint8_t *seen)
{
    dprintf(fd, "%*s", 2 + depth * 2, "");
    if (depth > 0)
        dprintf(fd, "-> ");
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
        graphTreeNode(fd, g, e->to, depth + 1, maxDepth, seen);
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

typedef struct {
    const char *path;
    uint64_t line, objs, bytes;
} TopSite;

// The sites that account for most of `ids` (reorders it), largest first. Returns
// how many of `best` are filled.
static size_t topSites(const Graph *g, uint32_t *ids, size_t count, size_t top, TopSite best[8])
{
    memset(best, 0, 8 * sizeof(TopSite));
    if (top > 8)
        top = 8;

    sortGraph = g;
    qsort(ids, count, sizeof(uint32_t), compareNodeSite);
    for (size_t i = 0; i < count;) {
        const GraphNode *first = &g->nodes[ids[i]];
        TopSite s = {.path = first->path, .line = first->line};
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
    size_t found = 0;
    while (found < top && best[found].objs > 0)
        found++;
    return found;
}

static void jsonTopSites(JsonWriter *json, const TopSite *sites, size_t count)
{
    jsonKey(json, "sites");
    jsonBeginArray(json);
    for (size_t i = 0; i < count; i++) {
        jsonBeginObject(json);
        jsonFieldString(json, "file", sites[i].path);
        jsonFieldUint(json, "line", sites[i].line);
        jsonFieldUint(json, "objects", sites[i].objs);
        jsonFieldUint(json, "bytes", sites[i].bytes);
        jsonEndObject(json);
    }
    jsonEndArray(json);
}

static void printTopSites(int fd, const Graph *g, uint32_t *ids, size_t count, size_t top)
{
    TopSite best[8];
    size_t found = topSites(g, ids, count, top, best);
    for (size_t j = 0; j < found; j++) {
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

// A named global counts only for what it owns, not for its own storage
static inline uint64_t nodeBytes(const Graph *g, uint32_t v)
{
    return g->nodes[v].root ? 0 : g->nodes[v].size;
}

/*
 * Roots are the nodes nothing in the heap points at: named globals, and objects
 * held from outside (a stack, a coroutine). For each, `excl` is what only that
 * root keeps alive (freed if it goes away), `reach` also counts objects other
 * roots hold too. A cache is a root with a large `excl`.
 */
static void jsonRetainedEnd(JsonWriter *json,
                            uint64_t objects,
                            uint64_t edges,
                            uint64_t roots,
                            uint64_t holding,
                            bool limited,
                            uint64_t unrooted,
                            uint64_t unrootedBytes,
                            const TopSite *sites,
                            size_t siteCount)
{
    jsonEndArray(json);
    jsonFieldUint(json, "objects", objects);
    jsonFieldUint(json, "edges", edges);
    jsonFieldUint(json, "rootCount", roots);
    jsonFieldUint(json, "holding", holding);
    jsonFieldBool(json, "limited", limited);
    jsonKey(json, "unrooted");
    jsonBeginObject(json);
    jsonFieldUint(json, "objects", unrooted);
    jsonFieldUint(json, "bytes", unrootedBytes);
    jsonTopSites(json, sites, siteCount);
    jsonEndObject(json);
    jsonGraphEnd(json);
}

static void graphRetained(int fd, const Graph *g, size_t limit, JsonWriter *json)
{
    size_t n = g->count;
    if (n == 0) {
        if (json != NULL) {
            jsonBeginObject(json);
            jsonKey(json, "roots");
            jsonBeginArray(json);
            jsonRetainedEnd(json, 0, 0, 0, 0, false, 0, 0, NULL, 0);
        }
        else {
            dprintf(fd, "no live allocations\n");
        }
        return;
    }
    uint32_t *claim = calloc(n, sizeof(uint32_t)), *stamp = calloc(n, sizeof(uint32_t));
    uint32_t *stack = malloc(n * sizeof(uint32_t)), *ids = malloc(n * sizeof(uint32_t));
    RootInfo *roots = malloc(n * sizeof(RootInfo));
    if (!claim || !stamp || !stack || !ids || !roots) {
        reportError(fd, json, json ? "out of memory" : "error: out of memory");
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
            if (!g->nodes[v].root)
                info->objs++;
            info->bytes += nodeBytes(g, v);
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
    size_t rootNodes = 0;
    for (uint32_t v = 0; v < n; v++) {
        if (g->nodes[v].root) {
            rootNodes++;
            continue;
        }
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
    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "roots");
        jsonBeginArray(json);
    }
    else {
        dprintf(fd, "%12s %10s %12s %10s  %s\n", "excl.bytes", "excl.objs", "reach.bytes", "reach.objs", "root");
    }
    size_t shown = 0, holding = 0;
    for (size_t i = 0; i < rootCount; i++) {
        const RootInfo *info = &roots[i];
        if (info->objs < (g->nodes[info->id].root ? 1 : 2))
            continue; // holds nothing
        holding++;
        if (shown >= limit)
            continue;
        shown++;
        if (json == NULL) {
            dprintf(fd,
                    "%12llu %10llu %12llu %10llu  ",
                    (unsigned long long)info->exBytes,
                    (unsigned long long)info->exObjs,
                    (unsigned long long)info->bytes,
                    (unsigned long long)info->objs);
            printGraphNode(fd, g, info->id);
            dprintf(fd, "\n");
        }

        size_t count = 0;
        for (uint32_t v = 0; v < n; v++) {
            if (claim[v] == info->slot && !g->nodes[v].root)
                ids[count++] = v;
        }
        if (json != NULL) {
            TopSite sites[8];
            size_t found = topSites(g, ids, count, 3, sites);
            jsonBeginObject(json);
            jsonKey(json, "node");
            jsonGraphNode(json, g, info->id);
            jsonFieldUint(json, "exclusiveBytes", info->exBytes);
            jsonFieldUint(json, "exclusiveObjects", info->exObjs);
            jsonFieldUint(json, "reachBytes", info->bytes);
            jsonFieldUint(json, "reachObjects", info->objs);
            jsonTopSites(json, sites, found);
            jsonEndObject(json);
        }
        else {
            printTopSites(fd, g, ids, count, 3);
        }
    }
    if (json != NULL) {
        size_t count = 0;
        for (uint32_t v = 0; v < n; v++) {
            if (claim[v] == 0 && !g->nodes[v].root)
                ids[count++] = v;
        }
        TopSite sites[8];
        size_t found = unrooted > 0 ? topSites(g, ids, count, 3, sites) : 0;
        jsonRetainedEnd(json,
                        n - rootNodes,
                        g->edgeCount,
                        rootCount,
                        holding,
                        holding > shown,
                        unrooted,
                        unrootedBytes,
                        sites,
                        found);
        goto done;
    }
    dprintf(fd,
            "Total: %llu objects, %llu edges, %llu roots (%llu hold other objects)%s\n",
            (unsigned long long)(n - rootNodes),
            (unsigned long long)g->edgeCount,
            (unsigned long long)rootCount,
            (unsigned long long)holding,
            holding > shown ? ", showing the largest" : "");

    if (unrooted > 0) {
        dprintf(fd,
                "\nunrooted: %llu objects, %llu bytes held only through cycles (see `cycles`)\n",
                (unsigned long long)unrooted,
                (unsigned long long)unrootedBytes);
        size_t count = 0;
        for (uint32_t v = 0; v < n; v++) {
            if (claim[v] == 0 && !g->nodes[v].root)
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

static const uint64_t *cycleBytes;

static int compareCycleBytes(const void *lhs, const void *rhs)
{
    uint64_t l = cycleBytes[*(const uint32_t *)lhs], r = cycleBytes[*(const uint32_t *)rhs];
    return l < r ? 1 : (l > r ? -1 : 0);
}

/*
 * Leaked cycles. A group of nodes that own each other (a strongly connected
 * component with more than one node, or a node that owns itself) is garbage when
 * no node outside the group points into it and none of its objects has a
 * reference from outside the heap. Reference counting can never free it.
 */
// Strongly connected components: `comp[v]` is the component of node v, numbered
// in the order Tarjan's algorithm closes them. Iterative: the graph can be deeper
// than the stack.
static bool graphComponents(const Graph *g, uint32_t *comp, uint32_t *compCountOut)
{
    size_t n = g->count;
    uint32_t *index = calloc(n, sizeof(uint32_t)), *low = calloc(n, sizeof(uint32_t));
    uint32_t *pos = malloc(n * sizeof(uint32_t));
    uint32_t *stack = malloc(n * sizeof(uint32_t)), *calls = malloc(n * sizeof(uint32_t));
    uint8_t *onStack = calloc(n, 1);
    bool ok = index && low && pos && stack && calls && onStack;
    if (!ok)
        goto done;

    uint32_t counter = 0, compCount = 0;
    size_t sp = 0, cp = 0;
    for (uint32_t s = 0; s < n; s++) {
        if (index[s] != 0)
            continue;
        index[s] = low[s] = ++counter;
        stack[sp++] = s;
        onStack[s] = 1;
        pos[s] = g->outStart[s];
        calls[cp++] = s;
        while (cp > 0) {
            uint32_t v = calls[cp - 1];
            if (pos[v] < g->outStart[v + 1]) {
                uint32_t w = g->edges[g->outEdge[pos[v]++]].to;
                if (index[w] == 0) {
                    index[w] = low[w] = ++counter;
                    stack[sp++] = w;
                    onStack[w] = 1;
                    pos[w] = g->outStart[w];
                    calls[cp++] = w;
                }
                else if (onStack[w] && index[w] < low[v]) {
                    low[v] = index[w];
                }
                continue;
            }
            if (low[v] == index[v]) {
                uint32_t x;
                do {
                    x = stack[--sp];
                    onStack[x] = 0;
                    comp[x] = compCount;
                } while (x != v);
                compCount++;
            }
            cp--;
            if (cp > 0 && low[v] < low[calls[cp - 1]])
                low[calls[cp - 1]] = low[v];
        }
    }
    *compCountOut = compCount;

done:
    free(index);
    free(low);
    free(pos);
    free(stack);
    free(calls);
    free(onStack);
    return ok;
}

static void graphCycles(int fd, const Graph *g, size_t limit, JsonWriter *json)
{
    size_t n = g->count;
    uint32_t *comp = malloc((n ? n : 1) * sizeof(uint32_t));
    uint32_t compCount = 0;
    if (comp == NULL || !graphComponents(g, comp, &compCount)) {
        reportError(fd, json, json ? "out of memory" : "error: out of memory");
        free(comp);
        return;
    }

    uint32_t *size = calloc(compCount, sizeof(uint32_t)), *order = malloc(compCount * sizeof(uint32_t));
    uint64_t *bytes = calloc(compCount, sizeof(uint64_t));
    uint8_t *selfLoop = calloc(compCount, 1), *entered = calloc(compCount, 1);
    uint8_t *external = calloc(compCount, 1);
    if (!size || !order || !bytes || !selfLoop || !entered || !external) {
        reportError(fd, json, json ? "out of memory" : "error: out of memory");
        free(size);
        free(order);
        free(bytes);
        free(selfLoop);
        free(entered);
        free(external);
        free(comp);
        return;
    }

    for (uint32_t v = 0; v < n; v++) {
        uint32_t c = comp[v];
        size[c]++;
        bytes[c] += nodeBytes(g, v);
        uint32_t incoming = g->inStart[v + 1] - g->inStart[v];
        for (uint32_t k = g->inStart[v]; k < g->inStart[v + 1]; k++) {
            uint32_t u = g->edges[g->inEdge[k]].from;
            if (u == v)
                selfLoop[c] = 1;
            else if (comp[u] != c)
                entered[c] = 1;
        }
        if (!g->nodes[v].raw && g->nodes[v].refs > incoming)
            external[c] = 1;
    }

    size_t leaked = 0;
    uint64_t leakedBytes = 0;
    for (uint32_t c = 0; c < compCount; c++) {
        if ((size[c] > 1 || selfLoop[c]) && !entered[c] && !external[c]) {
            order[leaked++] = c;
            leakedBytes += bytes[c];
        }
    }
    cycleBytes = bytes;
    qsort(order, leaked, sizeof(uint32_t), compareCycleBytes);

    size_t shown = limit < leaked ? limit : leaked;
    if (json != NULL) {
        jsonBeginObject(json);
        jsonKey(json, "cycles");
        jsonBeginArray(json);
    }
    for (size_t i = 0; i < shown; i++) {
        uint32_t c = order[i];
        uint32_t members = 0;
        if (json != NULL) {
            jsonBeginObject(json);
            jsonFieldUint(json, "nodes", size[c]);
            jsonFieldUint(json, "bytes", bytes[c]);
            jsonFieldUint(json, "moreMembers", size[c] > 12 ? size[c] - 12 : 0);
            jsonKey(json, "members");
            jsonBeginArray(json);
            for (uint32_t v = 0; v < n && members < 12; v++) {
                if (comp[v] == c) {
                    jsonGraphNode(json, g, v);
                    members++;
                }
            }
            jsonEndArray(json);
            jsonEndObject(json);
            continue;
        }
        dprintf(fd, "cycle: %u nodes, %llu bytes\n", size[c], (unsigned long long)bytes[c]);
        for (uint32_t v = 0; v < n; v++) {
            if (comp[v] != c)
                continue;
            if (members++ == 12) {
                dprintf(fd, "  ... %u more\n", size[c] - 12);
                break;
            }
            dprintf(fd, "  ");
            printGraphNode(fd, g, v);
            dprintf(fd, "\n");
        }
    }
    if (json != NULL) {
        jsonEndArray(json);
        jsonFieldUint(json, "total", leaked);
        jsonFieldUint(json, "bytes", leakedBytes);
        jsonFieldBool(json, "limited", leaked > shown);
        jsonGraphEnd(json);
    }
    else {
        dprintf(fd,
                "Total: %llu leaked cycles, %llu bytes%s\n",
                (unsigned long long)leaked,
                (unsigned long long)leakedBytes,
                leaked > shown ? ", showing the largest" : "");
    }

    free(size);
    free(order);
    free(bytes);
    free(selfLoop);
    free(entered);
    free(external);
    free(comp);
}

static void snapshotReport(int fd, JsonWriter *json);

// Runs in the forked child
static void runGraphCommand(int fd, const char *cmd, const char *arg1, const char *arg2)
{
    // These read the paused copy too, but need no graph
    JsonWriter *json = graphJson;
    if (strcmp(cmd, "edges") == 0) {
        edgesReportFd(fd, arg1, json);
        return;
    }
    if (strcmp(cmd, "roots") == 0) {
        rootsReportFd(fd, json);
        return;
    }
    if (strcmp(cmd, "dangling") == 0) {
        danglingReportFd(fd, json);
        return;
    }
    if (strcmp(cmd, "snapshot") == 0) {
        snapshotReport(fd, json);
        return;
    }

    Graph g = {0};
    if (!graphBuild(&g)) {
        reportError(fd, json, json ? "out of memory while building the graph" : "error: out of memory while building the graph");
        return;
    }

    if (strcmp(cmd, "retained") == 0) {
        graphRetained(fd, &g, parseLimit(arg1), json);
    }
    else if (strcmp(cmd, "cycles") == 0) {
        graphCycles(fd, &g, parseLimit(arg1), json);
    }
    else {
        uint32_t id = graphResolve(fd, &g, arg1, json);
        if (id != GRAPH_NONE) {
            if (strcmp(cmd, "holders") == 0) {
                graphHolders(fd, &g, id, json);
            }
            else if (strcmp(cmd, "path") == 0) {
                graphPath(fd, &g, id, json);
            }
            else if (strcmp(cmd, "contains") == 0) {
                uint32_t inner = graphResolve(fd, &g, arg2, json);
                if (inner != GRAPH_NONE)
                    graphContains(fd, &g, id, inner, json);
            }
            else {
                int depth = arg2 ? atoi(arg2) : 3;
                depth = depth < 0 ? 0 : (depth > 8 ? 8 : depth);
                uint8_t *seen = calloc(g.count, 1);
                if (seen != NULL) {
                    if (json != NULL) {
                        jsonBeginObject(json);
                        jsonFieldUint(json, "depth", depth);
                        jsonKey(json, "tree");
                        jsonTreeNode(json, &g, id, 0, depth, seen);
                        jsonGraphEnd(json);
                    }
                    else {
                        graphTreeNode(fd, &g, id, 0, depth, seen);
                    }
                    free(seen);
                }
            }
        }
    }
}

static void graphCrashHandler(int sig)
{
    static const char msg[] = "error: the scan crashed while reading memory\n";
    static const char jsonMsg[] = "\n{\"error\":\"the scan crashed while reading memory\"}\n";
    if (graphJson != NULL)
        (void)!write(graphFd, jsonMsg, sizeof(jsonMsg) - 1);
    else
        (void)!write(graphFd, msg, sizeof(msg) - 1);
    _exit(128 + sig);
}

/*
 * Graph commands run in a forked child so the program keeps running and the
 * scan sees a stable copy. The table lock is held across the fork so the copy
 * is consistent. The child never takes it again and leaves with _exit() so
 * none of the program's exit handlers run.
 */
static void graphCommand(int fd, const char *cmd, const char *arg1, const char *arg2, bool asJson)
{
    uint32_t running = pauseThreads();
    lockAllLists();
    pid_t pid = fork();
    if (pid == 0) {
        // The child owns the locks its parent held at the fork, and the program
        // is not paused there: give both back before using the registry
        __atomic_store_n(&pauseRequested, 0, __ATOMIC_RELEASE);
        unlockAllLists();
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
        JsonWriter writer;
        if (asJson) {
            jsonInitFd(&writer, fd);
            graphJson = &writer;
        }
        graphApproximate = running;
        runGraphCommand(fd, cmd, arg1, arg2);
        if (asJson)
            jsonReplyEnd(&writer, fd);
        _exit(0);
    }
    unlockAllLists();
    resumeThreads();
    if (pid < 0) {
        if (asJson)
            jsonErrorFd(fd, "fork failed", cmd);
        else
            dprintf(fd, "error: fork failed: %s\n", strerror(errno));
        return;
    }
    if (running > 0 && !asJson)
        dprintf(fd,
                "note: approximate, %u active thread(s) did not reach a safepoint in time "
                "(CXY_MEM_PAUSE_MS)\n",
                running);

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
            if (asJson)
                jsonErrorFd(fd, "scan timed out (CXY_MEM_SCAN_SECONDS)", cmd);
            else
                dprintf(fd, "error: scan timed out (CXY_MEM_SCAN_SECONDS)\n");
            return;
        }
        usleep(10000);
    }
}

/* ------------------------------------------------------- baseline, snapshot */

/*
 * A CounterSet is every number a rule needs, taken at one moment: totals, the
 * cumulative counters per type and per site, and per site how many objects are alive
 * now and how many of those were born after the last `mark`. `mark` stores one as
 * the baseline (it belongs to the program, nothing is written to disk), `snapshot`
 * takes one in a forked copy together with the graph.
 */
typedef struct {
    uint64_t allocs, bytes;    // cumulative
    uint64_t live, liveBytes;  // alive now
    uint64_t fresh;            // alive now and born after the mark
} SiteStat;

typedef struct {
    uint64_t tick, timeMs, markGen;
    uint64_t objects, objectBytes, buffers, bufferBytes;
    uint64_t zombies, zombieBytes, objectsAllocated;
    uint32_t typeCount, siteCount;
    TypeCount *types;
    SiteStat *sites;
} CounterSet;

static void freeCounterSet(CounterSet *s)
{
    free(s->types);
    free(s->sites);
    memset(s, 0, sizeof(*s));
}

// `locked` is false in a forked child, where the registry is a private copy
static bool collectCounters(CounterSet *s, bool locked)
{
    memset(s, 0, sizeof(*s));
    uint32_t ns = siteTableSize(), nt = typeTableSize();
    SiteCount *sums = NULL;
    TypeCount *types = NULL;
    sumCountersEx(&sums, &types, locked);
    s->sites = calloc(ns + 1, sizeof(SiteStat));
    if (sums == NULL || types == NULL || s->sites == NULL) {
        free(sums);
        free(types);
        freeCounterSet(s);
        return false;
    }
    s->types = types;
    s->typeCount = nt;
    s->siteCount = ns;
    s->markGen = __atomic_load_n(&markGen, __ATOMIC_RELAXED);
    s->tick = __atomic_load_n(&tick, __ATOMIC_RELAXED);
    s->timeMs = nowNs() / 1000000ull;

    if (locked)
        pthread_mutex_lock(&listsLock);
    for (TraceList *l = allLists; l != NULL; l = l->next) {
        if (locked)
            pthread_mutex_lock(&l->lock);
        s->objects += l->objects;
        s->objectBytes += l->objectBytes;
        s->buffers += l->buffers;
        s->bufferBytes += l->bufferBytes;
        s->zombies += l->zombies;
        s->zombieBytes += l->zombieBytes;
        s->objectsAllocated += l->objectAllocs;
        for (const TraceRec *rec = l->head; rec != NULL && ns > 0; rec = rec->next) {
            if (rec->kind == KIND_ZOMBIE || rec->site == NULL || rec->site->index >= ns)
                continue;
            SiteStat *st = &s->sites[rec->site->index];
            st->live++;
            st->liveBytes += rec->size;
            if (rec->birthTick > s->markGen)
                st->fresh++;
        }
        if (locked)
            pthread_mutex_unlock(&l->lock);
    }
    if (locked)
        pthread_mutex_unlock(&listsLock);

    for (uint32_t i = 0; i < ns; i++) {
        s->sites[i].allocs = sums[i].allocs;
        s->sites[i].bytes = sums[i].bytes;
    }
    free(sums);
    return true;
}

static void jsonCountPair(JsonWriter *json, const char *key, uint64_t count, uint64_t bytes)
{
    jsonKey(json, key);
    jsonBeginObject(json);
    jsonFieldUint(json, "count", count);
    jsonFieldUint(json, "bytes", bytes);
    jsonEndObject(json);
}

// The fields of a CounterSet, written into an open object. Types and sites that
// never allocated and have nothing alive are left out.
static void jsonCounterSet(JsonWriter *json, const CounterSet *s)
{
    jsonFieldUint(json, "tick", s->tick);
    jsonFieldUint(json, "timeMs", s->timeMs);
    jsonFieldUint(json, "mark", s->markGen);
    jsonCountPair(json, "objects", s->objects, s->objectBytes);
    jsonCountPair(json, "buffers", s->buffers, s->bufferBytes);
    jsonCountPair(json, "zombies", s->zombies, s->zombieBytes);
    jsonFieldUint(json, "objectsAllocated", s->objectsAllocated);

    jsonKey(json, "types");
    jsonBeginArray(json);
    for (uint32_t i = 0; i < s->typeCount; i++) {
        const TypeCount *t = &s->types[i];
        if (t->allocs == 0)
            continue;
        const TypeRecord *info = __cxy_types[i];
        jsonBeginObject(json);
        jsonFieldString(json, "name", info != NULL ? info->name : NULL);
        jsonFieldUint(json, "allocs", t->allocs);
        jsonFieldUint(json, "frees", t->frees);
        jsonFieldUint(json, "live", t->allocs > t->frees ? t->allocs - t->frees : 0);
        jsonFieldUint(json, "bytes", t->bytes);
        jsonEndObject(json);
    }
    jsonEndArray(json);

    jsonKey(json, "sites");
    jsonBeginArray(json);
    for (uint32_t i = 0; i < s->siteCount; i++) {
        const SiteStat *st = &s->sites[i];
        if (st->allocs == 0 && st->live == 0)
            continue;
        const SiteRecord *site = __cxy_sites[i];
        jsonBeginObject(json);
        jsonFieldString(json, "file", site != NULL ? site->file : NULL);
        jsonFieldUint(json, "line", site != NULL ? site->line : 0);
        jsonFieldUint(json, "column", site != NULL ? site->column : 0);
        jsonFieldString(json, "function", site != NULL ? site->function : NULL);
        jsonFieldUint(json, "allocs", st->allocs);
        jsonFieldUint(json, "bytes", st->bytes);
        jsonFieldUint(json, "live", st->live);
        jsonFieldUint(json, "liveBytes", st->liveBytes);
        jsonFieldUint(json, "aliveSinceMark", st->fresh);
        jsonEndObject(json);
    }
    jsonEndArray(json);
}

static CounterSet baseline;
static bool hasBaseline;

// `mark`: remember the allocation clock for `diff`, and the counters for `analyze`
static void markCommand(int fd, JsonWriter *json)
{
    uint64_t gen = __atomic_load_n(&tick, __ATOMIC_RELAXED);
    __atomic_store_n(&markGen, gen, __ATOMIC_RELAXED);
    CounterSet now;
    if (collectCounters(&now, true)) {
        freeCounterSet(&baseline);
        baseline = now;
        hasBaseline = true;
    }
    if (json != NULL) {
        jsonBeginObject(json);
        jsonFieldUint(json, "mark", gen);
        jsonFieldUint(json, "tick", hasBaseline ? baseline.tick : gen);
        jsonFieldUint(json, "timeMs", hasBaseline ? baseline.timeMs : 0);
        jsonFieldBool(json, "baseline", hasBaseline);
        jsonEndObject(json);
    }
    else {
        dprintf(fd, "mark %llu\n", (unsigned long long)gen);
    }
}

static void baselineReport(int fd, JsonWriter *json)
{
    if (json != NULL) {
        jsonBeginObject(json);
        jsonFieldBool(json, "exists", hasBaseline);
        if (hasBaseline)
            jsonCounterSet(json, &baseline);
        jsonEndObject(json);
    }
    else if (!hasBaseline) {
        dprintf(fd, "no baseline, `mark` takes one\n");
    }
    else {
        dprintf(fd,
                "baseline: tick %llu, %llu live objects (%llu bytes), %llu allocated since start\n",
                (unsigned long long)baseline.tick,
                (unsigned long long)baseline.objects,
                (unsigned long long)baseline.objectBytes,
                (unsigned long long)baseline.objectsAllocated);
    }
}

// Objects with more references than the graph has edges into them: the rest is
// held outside the heap (a stack, a coroutine, an unnamed global)
static void jsonGraphExternal(JsonWriter *json, const Graph *g)
{
    uint64_t objects = 0, refs = 0;
    for (uint32_t v = 0; v < g->count; v++) {
        const GraphNode *n = &g->nodes[v];
        uint32_t incoming = g->inStart[v + 1] - g->inStart[v];
        if (!n->raw && n->refs > incoming) {
            objects++;
            refs += n->refs - incoming;
        }
    }
    jsonBeginObject(json);
    jsonFieldUint(json, "objects", objects);
    jsonFieldUint(json, "refs", refs);
    jsonEndObject(json);
}

// Runs in the forked child: counters and graph come from the same paused copy, so
// the numbers agree with each other
static void snapshotReport(int fd, JsonWriter *json)
{
    CounterSet now;
    if (!collectCounters(&now, false)) {
        jsonErrorTo(json, "out of memory");
        return;
    }
    jsonBeginObject(json);
    jsonFieldUint(json, "version", 1);
    jsonFieldUint(json, "pid", (uint64_t)getpid());
    jsonCounterSet(json, &now);
    jsonFieldBool(json, "hasBaseline", hasBaseline);
    jsonFieldUint(json, "approximate", graphApproximate);

    Graph g = {0};
    jsonKey(json, "graph");
    if (!graphBuild(&g)) {
        jsonErrorTo(json, "out of memory while building the graph");
    }
    else {
        jsonBeginObject(json);
        jsonFieldUint(json, "nodes", g.count);
        jsonFieldUint(json, "edges", g.edgeCount);
        jsonKey(json, "external");
        jsonGraphExternal(json, &g);
        jsonKey(json, "retained");
        graphRetained(fd, &g, 5, json);
        jsonKey(json, "cycles");
        graphCycles(fd, &g, 5, json);
        jsonEndObject(json);
    }
    jsonKey(json, "dangling");
    danglingReportFd(fd, json);
    jsonEndObject(json);
}

/* -------------------------------------------------------------------- json */

/*
 * `json` after a command asks for the answer as one JSON document on one line.
 * Commands get a JSON form one at a time, the others answer with an error that
 * says so, so a client can tell "not converted yet" from "unknown".
 */
static const char *const knownCommands[] = {
    "summary", "sites",   "list",     "leaks", "zombies",  "types",  "sitecounts", "ring",
    "mark",    "diff",    "difflist", "holders", "path",   "contains", "retained",
    "cycles",  "tree",    "edges",    "roots", "dangling", "help",   "baseline", "snapshot",
    "refs",    NULL};

// Commands answered from the paused, forked copy of the registry
static bool isGraphCommand(const char *cmd)
{
    static const char *const names[] = {"holders", "path",  "contains", "retained", "cycles",
                                        "tree",    "edges", "roots",    "dangling", "snapshot",
                                        NULL};
    for (int i = 0; names[i] != NULL; i++) {
        if (strcmp(cmd, names[i]) == 0)
            return true;
    }
    return false;
}

static bool isKnownCommand(const char *cmd)
{
    for (int i = 0; knownCommands[i] != NULL; i++) {
        if (strcmp(cmd, knownCommands[i]) == 0)
            return true;
    }
    return false;
}

// Answers `cmd` as JSON. False when the command has no JSON form (yet).
static bool jsonCommand(int fd, const char *cmd, char *args[])
{
    if (cmd == NULL) {
        jsonErrorFd(fd, "no command", NULL);
        return true;
    }
    if (!isKnownCommand(cmd)) {
        jsonErrorFd(fd, "unknown command", cmd);
        return true;
    }
    if (isGraphCommand(cmd)) {
        graphCommand(fd, cmd, args[1], args[2], true);
        return true;
    }
    JsonWriter w;
    jsonInitFd(&w, fd);
    uint64_t mark = __atomic_load_n(&markGen, __ATOMIC_RELAXED);
    if (strcmp(cmd, "mark") == 0)
        markCommand(fd, &w);
    else if (strcmp(cmd, "baseline") == 0)
        baselineReport(fd, &w);
    else if (strcmp(cmd, "refs") == 0)
        refsReportFd(fd, parseLimit(args[1]), &w);
    else if (strcmp(cmd, "summary") == 0)
        summaryFd(fd, &w);
    else if (strcmp(cmd, "sites") == 0)
        sitesReportFd(fd, 0, parseLimit(args[1]), &w);
    else if (strcmp(cmd, "diff") == 0)
        sitesReportFd(fd, mark, parseLimit(args[1]), &w);
    else if (strcmp(cmd, "list") == 0)
        liveReportFd(fd, 0, parseLimit(args[1]), false, false, NULL, &w);
    else if (strcmp(cmd, "difflist") == 0)
        liveReportFd(fd, mark, parseLimit(args[1]), false, false, NULL, &w);
    else if (strcmp(cmd, "zombies") == 0)
        zombiesReportFd(fd, parseLimit(args[1]), false, &w);
    else if (strcmp(cmd, "types") == 0)
        typesReportFd(fd, &w);
    else if (strcmp(cmd, "sitecounts") == 0)
        siteCountsReportFd(fd, parseLimit(args[1]), &w);
    else
        return false;
    jsonReplyEnd(&w, fd);
    return true;
}

static void handleCommand(int fd, char *line)
{
    char *args[4] = {NULL};
    int nargs = 0;
    bool sym = true;
    bool json = false;
    for (char *tok = strtok(line, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
        if (strcmp(tok, "nosym") == 0)
            sym = false;
        else if (strcmp(tok, "json") == 0)
            json = true;
        else if (nargs < 4)
            args[nargs++] = tok;
    }
    if (json && !jsonCommand(fd, nargs > 0 ? args[0] : NULL, args)) {
        jsonErrorFd(fd, "no json output for this command", args[0]);
        return;
    }
    if (json)
        return;
    if (nargs == 0 || strcmp(args[0], "help") == 0) {
        helpFd(fd);
        return;
    }

    const char *cmd = args[0];
    uint64_t mark = __atomic_load_n(&markGen, __ATOMIC_RELAXED);
    if (strcmp(cmd, "summary") == 0)
        summaryFd(fd, NULL);
    else if (strcmp(cmd, "sites") == 0)
        sitesReportFd(fd, 0, parseLimit(args[1]), NULL);
    else if (strcmp(cmd, "list") == 0)
        liveReportFd(fd, 0, parseLimit(args[1]), sym, false, NULL, NULL);
    else if (strcmp(cmd, "leaks") == 0)
        liveReportFd(fd, 0, 0, sym, true, NULL, NULL);
    else if (strcmp(cmd, "zombies") == 0)
        zombiesReportFd(fd, parseLimit(args[1]), false, NULL);
    else if (strcmp(cmd, "types") == 0)
        typesReportFd(fd, NULL);
    else if (strcmp(cmd, "sitecounts") == 0)
        siteCountsReportFd(fd, parseLimit(args[1]), NULL);
    else if (strcmp(cmd, "ring") == 0) {
        if (args[1] == NULL) {
            dprintf(fd, "usage: ring <ptr>\n");
            return;
        }
        ringDumpFd(fd, (void *)(uintptr_t)strtoull(args[1], NULL, 0), sym);
    }
    else if (strcmp(cmd, "mark") == 0)
        markCommand(fd, NULL);
    else if (strcmp(cmd, "baseline") == 0)
        baselineReport(fd, NULL);
    else if (strcmp(cmd, "refs") == 0)
        refsReportFd(fd, parseLimit(args[1]), NULL);
    else if (strcmp(cmd, "snapshot") == 0)
        dprintf(fd, "snapshot answers as JSON only, try `snapshot json`\n");
    else if (strcmp(cmd, "diff") == 0)
        sitesReportFd(fd, mark, parseLimit(args[1]), NULL);
    else if (strcmp(cmd, "difflist") == 0)
        liveReportFd(fd, mark, parseLimit(args[1]), sym, false, NULL, NULL);
    else if (isGraphCommand(cmd))
        graphCommand(fd, cmd, args[1], args[2], false);
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
    // A client that goes away while the answer is written must not kill the program
    // being debugged: the write fails with EPIPE, the writer notices and stops
    sigset_t noPipe;
    sigemptyset(&noPipe);
    sigaddset(&noPipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &noPipe, NULL);

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

/* -------------------------------------------------------------- exit report */

typedef struct {
    const Graph *g;
    const uint8_t *reached; // owned, directly or not, by a named global
    const uint32_t *comp;   // strongly connected component of each node
    const uint32_t *compSize;
} ExitCtx;

// Marks everything owned, directly or not, by a named global
static void markReachable(const Graph *g, uint8_t *reached)
{
    uint32_t *stack = malloc((g->count ? g->count : 1) * sizeof(uint32_t));
    if (stack == NULL)
        return;
    size_t sp = 0;
    for (uint32_t v = 0; v < g->count; v++) {
        if (g->nodes[v].root) {
            reached[v] = 1;
            stack[sp++] = v;
        }
    }
    while (sp > 0) {
        uint32_t v = stack[--sp];
        for (uint32_t k = g->outStart[v]; k < g->outStart[v + 1]; k++) {
            uint32_t w = g->edges[g->outEdge[k]].to;
            if (!reached[w]) {
                reached[w] = 1;
                stack[sp++] = w;
            }
        }
    }
    free(stack);
}

// What is left after the program's finalizers is a leak, unless a named global
// still owns it (a cache or a stream that lives until exit)
static bool isLeak(const LiveEntry *e, void *arg)
{
    ExitCtx *c = arg;
    uint32_t id = graphFind(c->g, (uintptr_t)e->ptr);
    return id == GRAPH_NONE || !c->reached[id];
}

// Says why a leaked block is still alive, from its place in the graph
static void leakNote(const LiveEntry *e, void *arg, char *buf, size_t size)
{
    const ExitCtx *c = arg;
    const Graph *g = c->g;
    buf[0] = '\0';
    uint32_t id = graphFind(g, (uintptr_t)e->ptr);
    if (id == GRAPH_NONE)
        return;

    // Another member of its cycle, or a leaked block outside it, owns it
    uint32_t cycleHolder = GRAPH_NONE, leakedHolder = GRAPH_NONE;
    bool selfOwned = false;
    for (uint32_t k = g->inStart[id]; k < g->inStart[id + 1]; k++) {
        uint32_t from = g->edges[g->inEdge[k]].from;
        if (from == id)
            selfOwned = true;
        else if (c->comp[from] == c->comp[id])
            cycleHolder = cycleHolder == GRAPH_NONE ? from : cycleHolder;
        else if (!c->reached[from])
            leakedHolder = leakedHolder == GRAPH_NONE ? from : leakedHolder;
    }

    uint32_t members = c->compSize[c->comp[id]];
    if (members > 1 || selfOwned) {
        if (cycleHolder != GRAPH_NONE)
            snprintf(buf,
                     size,
                     "  <- cycle of %u nodes, held by %p",
                     members,
                     (void *)g->nodes[cycleHolder].start);
        else
            snprintf(buf, size, "  <- owns itself");
    }
    else if (leakedHolder != GRAPH_NONE) {
        snprintf(buf,
                 size,
                 "  <- owned by the leaked %s %p",
                 g->nodes[leakedHolder].raw ? "buffer" : "object",
                 (void *)g->nodes[leakedHolder].start);
    }
    else if (!g->nodes[id].raw) {
        snprintf(buf,
                 size,
                 "  <- no owner, %u reference(s) held outside the heap",
                 (unsigned)g->nodes[id].refs);
    }
    else {
        snprintf(buf, size, "  <- no owner");
    }
}

// Called by the program's shutdown sequence after all finalizers ran. Leaked
// cycles are leaks like any other object nothing owns, `cycles` names them.
void __cxy_mem_trace_report(void)
{
    if (!__atomic_load_n(&started, __ATOMIC_ACQUIRE))
        return;
    fflush(stdout);

    Graph g = {0};
    ExitCtx ctx = {.g = &g};
    uint8_t *reached = NULL;
    uint32_t *comp = NULL, *compSize = NULL;
    uint64_t retained = 0;
    if (graphBuild(&g)) {
        uint32_t compCount = 0;
        reached = calloc(g.count ? g.count : 1, 1);
        comp = malloc((g.count ? g.count : 1) * sizeof(uint32_t));
        if (reached != NULL && comp != NULL && graphComponents(&g, comp, &compCount)) {
            compSize = calloc(compCount ? compCount : 1, sizeof(uint32_t));
            for (uint32_t v = 0; compSize != NULL && v < g.count; v++)
                compSize[comp[v]]++;
        }
        if (reached != NULL && compSize != NULL) {
            markReachable(&g, reached);
            ctx.reached = reached;
            ctx.comp = comp;
            ctx.compSize = compSize;
            for (uint32_t v = 0; v < g.count; v++) {
                if (reached[v] && !g.nodes[v].root)
                    retained++;
            }
        }
        else {
            free(reached);
            reached = NULL;
        }
    }

    // Without a graph every live block is reported, as before
    ReportHooks hooks = {.keep = isLeak, .note = leakNote, .ctx = &ctx};
    liveReportFd(STDOUT_FILENO, 0, 0, true, true, reached != NULL ? &hooks : NULL, NULL);
    if (reached != NULL && retained > 0) {
        size_t leaks = 0;
        for (uint32_t v = 0; v < g.count; v++) {
            if (!reached[v] && !g.nodes[v].root)
                leaks++;
        }
        if (leaks > 0)
            dprintf(STDOUT_FILENO, "(%llu blocks owned by named globals are not counted)\n", (unsigned long long)retained);
    }
    zombiesReportFd(STDOUT_FILENO, 0, true, NULL);
    free(reached);
    free(comp);
    free(compSize);
    free(g.nodes);
    free(g.keys);
    free(g.edges);
    free(g.outStart);
    free(g.outEdge);
    free(g.inStart);
    free(g.inEdge);
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
