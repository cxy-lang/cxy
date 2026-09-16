#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

static FILE *g_coro_trace_file = NULL;
static pthread_mutex_t g_coro_trace_mtx = PTHREAD_MUTEX_INITIALIZER;

static void close_coro_trace(void) {
    if (g_coro_trace_file != NULL && g_coro_trace_file != stdout && g_coro_trace_file != stderr) {
        fclose(g_coro_trace_file);
        g_coro_trace_file = NULL;
    }
}

static FILE *get_coro_trace_file(void) {
    if (g_coro_trace_file == NULL) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/cxy-app-trace-%d.log", (int)getpid());
        g_coro_trace_file = fopen(path, "a");
        if (g_coro_trace_file == NULL) {
            g_coro_trace_file = stderr;
        } else {
            atexit(close_coro_trace);
        }
    }
    return g_coro_trace_file;
}

void __trace_coro_custom(
    const char *tag,
    const char *path,
    uint64_t line,
    const char *coro_name,
    int32_t coro_id,
    int32_t sched_id,
    void *coro_ptr,
    const char *origin_file,
    uint64_t origin_line,
    uint64_t tid
) {
    pthread_mutex_lock(&g_coro_trace_mtx);
    FILE *f = get_coro_trace_file();

    struct timeval tv;
    gettimeofday(&tv, NULL);

    if (coro_ptr != NULL) {
        const char *name = coro_name ? coro_name : "anon";
        fprintf(f, "[%ld.%06ld] [tid=%llu] [sched=%d] coro(%-7.7s: '%s' id=%d ptr=%p origin=%s:%llu) @ %s:%llu\n",
                (long)tv.tv_sec, (long)tv.tv_usec,
                (unsigned long long)tid,
                sched_id,
                tag ? tag : "event",
                name,
                coro_id,
                coro_ptr,
                origin_file ? origin_file : "unknown",
                (unsigned long long)origin_line,
                path ? path : "unknown",
                (unsigned long long)line);
    } else {
        fprintf(f, "[%ld.%06ld] coro(%-7.7s: (null)) @ %s:%llu\n",
                (long)tv.tv_sec, (long)tv.tv_usec,
                tag ? tag : "event",
                path ? path : "unknown",
                (unsigned long long)line);
    }
    fflush(f);
    pthread_mutex_unlock(&g_coro_trace_mtx);
}
