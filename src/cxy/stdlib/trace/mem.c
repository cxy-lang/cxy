#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#define TABLE_CAPACITY 65536

typedef struct AllocRecord {
    void *ptr;
    const char *alloc_path;
    uint64_t alloc_line;
    uint64_t alloc_tid;
    struct timeval alloc_time;
    struct AllocRecord *next;
} AllocRecord;

typedef struct FreedRecord {
    void *ptr;
    const char *alloc_path;
    uint64_t alloc_line;
    uint64_t alloc_tid;
    const char *freed_path;
    uint64_t freed_line;
    uint64_t freed_tid;
    struct timeval freed_time;
    struct FreedRecord *next;
} FreedRecord;

static AllocRecord *g_active_table[TABLE_CAPACITY];
static FreedRecord *g_freed_table[TABLE_CAPACITY];
static pthread_mutex_t g_mem_trace_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_total_allocs = 0;
static uint64_t g_total_frees = 0;
static FILE *g_mem_log_file = NULL;
static bool g_initialized = false;

static inline uint32_t ptr_hash(void *ptr) {
    uintptr_t v = (uintptr_t)ptr;
    v = ((v >> 16) ^ v) * 0x45d9f3b;
    v = ((v >> 16) ^ v) * 0x45d9f3b;
    v = (v >> 16) ^ v;
    return (uint32_t)(v % TABLE_CAPACITY);
}

static inline uint64_t get_current_tid(void) {
    uint64_t tid = 0;
#if defined(__APPLE__)
    pthread_threadid_np(NULL, &tid);
#else
    tid = (uint64_t)pthread_self();
#endif
    return tid;
}

static FILE *get_mem_log_file(void) {
    if (g_mem_log_file == NULL) {
        char path[256];
        snprintf(path, sizeof(path), "/tmp/cxy-app-trace-%d.log", (int)getpid());
        g_mem_log_file = fopen(path, "a");
        if (g_mem_log_file == NULL) {
            g_mem_log_file = stderr;
        }
    }
    return g_mem_log_file;
}

static void check_leaks_at_exit(void) {
    pthread_mutex_lock(&g_mem_trace_mtx);
    FILE *f = get_mem_log_file();

    uint64_t active_count = 0;
    for (size_t i = 0; i < TABLE_CAPACITY; i++) {
        AllocRecord *cur = g_active_table[i];
        while (cur != NULL) {
            active_count++;
            fprintf(f, "[MEM LEAK] [tid=%llu] ptr=%p allocated @ %s:%llu\n",
                    (unsigned long long)cur->alloc_tid,
                    cur->ptr, cur->alloc_path, (unsigned long long)cur->alloc_line);
            fprintf(stderr, "[MEM LEAK] [tid=%llu] ptr=%p allocated @ %s:%llu\n",
                    (unsigned long long)cur->alloc_tid,
                    cur->ptr, cur->alloc_path, (unsigned long long)cur->alloc_line);
            cur = cur->next;
        }
    }

    fprintf(f, "[MEM SUMMARY] Allocs: %llu, Frees: %llu, Leaks: %llu\n",
            (unsigned long long)g_total_allocs,
            (unsigned long long)g_total_frees,
            (unsigned long long)active_count);
    fflush(f);
    if (g_mem_log_file != NULL && g_mem_log_file != stderr && g_mem_log_file != stdout) {
        fclose(g_mem_log_file);
        g_mem_log_file = NULL;
    }
    pthread_mutex_unlock(&g_mem_trace_mtx);
}

void __trace_memory_custom(const char *tag, const char *path, uint64_t line, void *ptr) {
    if (ptr == NULL) return;

    pthread_mutex_lock(&g_mem_trace_mtx);
    if (!g_initialized) {
        g_initialized = true;
        atexit(check_leaks_at_exit);
    }

    FILE *f = get_mem_log_file();
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t tid = get_current_tid();
    uint32_t idx = ptr_hash(ptr);

    if (tag != NULL && strcmp(tag, "alloc") == 0) {
        g_total_allocs++;
        AllocRecord *rec = (AllocRecord *)malloc(sizeof(AllocRecord));
        rec->ptr = ptr;
        rec->alloc_path = path ? path : "unknown";
        rec->alloc_line = line;
        rec->alloc_tid = tid;
        rec->alloc_time = tv;
        rec->next = g_active_table[idx];
        g_active_table[idx] = rec;

        fprintf(f, "[%ld.%06ld] [tid=%llu] mem(alloc: %p) @ %s:%llu\n",
                (long)tv.tv_sec, (long)tv.tv_usec,
                (unsigned long long)tid,
                ptr, path ? path : "unknown", (unsigned long long)line);
    }
    else if (tag != NULL && strcmp(tag, "freed") == 0) {
        g_total_frees++;
        AllocRecord **curr = &g_active_table[idx];
        AllocRecord *found = NULL;
        while (*curr != NULL) {
            if ((*curr)->ptr == ptr) {
                found = *curr;
                *curr = found->next;
                break;
            }
            curr = &(*curr)->next;
        }

        if (found == NULL) {
            // Check for double free in freed table
            FreedRecord *fr = g_freed_table[idx];
            bool is_double_free = false;
            while (fr != NULL) {
                if (fr->ptr == ptr) {
                    is_double_free = true;
                    fprintf(f, "[DOUBLE FREE DETECTED] [tid=%llu] ptr=%p!\n  Allocated [tid=%llu] @ %s:%llu\n  First freed [tid=%llu] @ %s:%llu\n  Second freed [tid=%llu] @ %s:%llu\n",
                            (unsigned long long)tid,
                            ptr,
                            (unsigned long long)fr->alloc_tid, fr->alloc_path, (unsigned long long)fr->alloc_line,
                            (unsigned long long)fr->freed_tid, fr->freed_path, (unsigned long long)fr->freed_line,
                            (unsigned long long)tid, path ? path : "unknown", (unsigned long long)line);
                    fprintf(stderr, "[DOUBLE FREE DETECTED] [tid=%llu] ptr=%p!\n  Allocated [tid=%llu] @ %s:%llu\n  First freed [tid=%llu] @ %s:%llu\n  Second freed [tid=%llu] @ %s:%llu\n",
                            (unsigned long long)tid,
                            ptr,
                            (unsigned long long)fr->alloc_tid, fr->alloc_path, (unsigned long long)fr->alloc_line,
                            (unsigned long long)fr->freed_tid, fr->freed_path, (unsigned long long)fr->freed_line,
                            (unsigned long long)tid, path ? path : "unknown", (unsigned long long)line);
                    break;
                }
                fr = fr->next;
            }
            if (!is_double_free) {
                fprintf(f, "[INVALID FREE] [tid=%llu] ptr=%p freed @ %s:%llu was not actively tracked\n",
                        (unsigned long long)tid, ptr, path ? path : "unknown", (unsigned long long)line);
            }
        } else {
            // Record in freed table
            FreedRecord *frec = (FreedRecord *)malloc(sizeof(FreedRecord));
            frec->ptr = ptr;
            frec->alloc_path = found->alloc_path;
            frec->alloc_line = found->alloc_line;
            frec->alloc_tid = found->alloc_tid;
            frec->freed_path = path ? path : "unknown";
            frec->freed_line = line;
            frec->freed_tid = tid;
            frec->freed_time = tv;
            frec->next = g_freed_table[idx];
            g_freed_table[idx] = frec;
            free(found);

            fprintf(f, "[%ld.%06ld] [tid=%llu] mem(freed: %p) @ %s:%llu\n",
                    (long)tv.tv_sec, (long)tv.tv_usec,
                    (unsigned long long)tid,
                    ptr, path ? path : "unknown", (unsigned long long)line);
        }
    }
    else {
        fprintf(f, "[%ld.%06ld] [tid=%llu] mem(%-5.5s: %p) @ %s:%llu\n",
                (long)tv.tv_sec, (long)tv.tv_usec,
                (unsigned long long)tid,
                tag ? tag : "event",
                ptr, path ? path : "unknown", (unsigned long long)line);
    }
    fflush(f);
    pthread_mutex_unlock(&g_mem_trace_mtx);
}
