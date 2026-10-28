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
#include <dlfcn.h>
#include <execinfo.h>

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
    void *ptr; // NULL = empty slot, LIVE_TOMBSTONE = deleted
    uint64_t size;
    const char *path;
    uint64_t line;
    uint64_t gen; // allocation order, for most-recent-first reports
} LiveEntry;

typedef struct {
    const char *path;
    uint64_t line;
    uint64_t count;
    uint64_t bytes;
} SiteEntry;

#define LIVE_TOMBSTONE ((void *)1)

static TraceEvent ring[CXY_TRACE_RING_SIZE];
static uint32_t ringIdx = 0;

static LiveEntry *live = NULL;
static size_t liveCap = 0;   // power of two
static size_t liveCount = 0; // live entries
static size_t liveUsed = 0;  // live entries + tombstones
static uint64_t liveGen = 0;
static uint64_t liveBytes = 0;
static pthread_mutex_t liveLock = PTHREAD_MUTEX_INITIALIZER;

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

static void liveRehash(size_t newCap)
{
    LiveEntry *old = live;
    size_t oldCap = liveCap;
    live = calloc(newCap, sizeof(LiveEntry));
    if (live == NULL) {
        // Out of memory while tracing: keep the old table
        live = old;
        return;
    }
    liveCap = newCap;
    liveUsed = liveCount;
    for (size_t i = 0; i < oldCap; i++) {
        if (old[i].ptr == NULL || old[i].ptr == LIVE_TOMBSTONE)
            continue;
        size_t idx = liveHash(old[i].ptr) & (liveCap - 1);
        while (live[idx].ptr != NULL)
            idx = (idx + 1) & (liveCap - 1);
        live[idx] = old[i];
    }
    free(old);
}

static void liveInsert(void *ptr, uint64_t size, const char *path, uint64_t line)
{
    if ((liveUsed + 1) * 4 >= liveCap * 3)
        liveRehash(liveCap ? liveCap * 2 : 1024);
    if (live == NULL)
        return;

    size_t idx = liveHash(ptr) & (liveCap - 1);
    while (live[idx].ptr != NULL && live[idx].ptr != LIVE_TOMBSTONE)
        idx = (idx + 1) & (liveCap - 1);
    if (live[idx].ptr == NULL)
        liveUsed++;
    live[idx] = (LiveEntry){.ptr = ptr, .size = size, .path = path, .line = line, .gen = ++liveGen};
    liveCount++;
    liveBytes += size;
}

static void liveRemove(void *ptr)
{
    if (live == NULL)
        return;
    size_t idx = liveHash(ptr) & (liveCap - 1);
    while (live[idx].ptr != NULL) {
        if (live[idx].ptr == ptr) {
            live[idx].ptr = LIVE_TOMBSTONE;
            liveCount--;
            liveBytes -= live[idx].size;
            return;
        }
        idx = (idx + 1) & (liveCap - 1);
    }
}

static int compareByGenDesc(const void *lhs, const void *rhs)
{
    uint64_t l = ((const LiveEntry *)lhs)->gen, r = ((const LiveEntry *)rhs)->gen;
    return l < r ? 1 : (l > r ? -1 : 0);
}

// Copies live entries newer than minGen (most recent first); caller frees
static LiveEntry *liveSnapshot(uint64_t minGen, size_t *count)
{
    pthread_mutex_lock(&liveLock);
    size_t n = liveCount;
    LiveEntry *snap = n ? malloc(n * sizeof(LiveEntry)) : NULL;
    size_t j = 0;
    if (snap != NULL) {
        for (size_t i = 0; i < liveCap && j < n; i++) {
            if (live[i].ptr != NULL && live[i].ptr != LIVE_TOMBSTONE && live[i].gen > minGen)
                snap[j++] = live[i];
        }
    }
    pthread_mutex_unlock(&liveLock);

    if (snap != NULL)
        qsort(snap, j, sizeof(LiveEntry), compareByGenDesc);
    *count = j;
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
    pthread_mutex_lock(&liveLock);
    uint64_t count = liveCount, bytes = liveBytes, gen = liveGen;
    pthread_mutex_unlock(&liveLock);
    dprintf(fd,
            "live: %llu allocations, %llu bytes\n"
            "allocations since start: %llu\n"
            "trace events: %u\n"
            "mark: %llu\n",
            (unsigned long long)count,
            (unsigned long long)bytes,
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
            "  diff [n]             sites of allocations made since `mark` still alive\n"
            "  difflist [n]         like `list`, but only allocations since `mark`\n",
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
    else if (strcmp(cmd, "ring") == 0) {
        if (args[1] == NULL) {
            dprintf(fd, "usage: ring <ptr>\n");
            return;
        }
        ringDumpFd(fd, (void *)(uintptr_t)strtoull(args[1], NULL, 0), sym);
    }
    else if (strcmp(cmd, "mark") == 0) {
        pthread_mutex_lock(&liveLock);
        uint64_t gen = liveGen;
        pthread_mutex_unlock(&liveLock);
        __atomic_store_n(&markGen, gen, __ATOMIC_RELAXED);
        dprintf(fd, "mark %llu\n", (unsigned long long)gen);
    }
    else if (strcmp(cmd, "diff") == 0)
        sitesReportFd(fd, mark, parseLimit(args[1]));
    else if (strcmp(cmd, "difflist") == 0)
        liveReportFd(fd, mark, parseLimit(args[1]), sym, false);
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
    if (__atomic_compare_exchange_n(&started, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        startQueryServer();
}

void __cxy_mem_trace_alloc(void *ptr, uint64_t size, const char *path, uint64_t line)
{
    if (ptr == NULL || !__atomic_load_n(&started, __ATOMIC_ACQUIRE))
        return;

    pthread_mutex_lock(&liveLock);

    liveInsert(ptr, size, path, line);
    pthread_mutex_unlock(&liveLock);
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

void __cxy_mem_trace_free(void *ptr)
{
    if (ptr == NULL)
        return;
    pthread_mutex_lock(&liveLock);
    liveRemove(ptr);
    pthread_mutex_unlock(&liveLock);
}

void __cxy_mem_trace_dump(void *ptr)
{
    fflush(stdout);
    ringDumpFd(STDOUT_FILENO, ptr, true);
}
