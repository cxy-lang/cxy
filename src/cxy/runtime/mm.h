// Memory manager core: object header, reference counts, allocation, release.
// Pasted after prologue.h. Design: docs/stage/memory-manager.md

typedef struct __TypeInfo __TypeInfo;
typedef struct __Site __Site;

// One record per class, emitted by the compiler (phase 1b). Unused until then.
struct __TypeInfo {
    const char *name;
    uint64_t size;
    uint32_t align, kind;
    void (*deinit)(void *obj);
    void (*dropFields)(void *obj);
    void (*visit)(void *obj, void *visitor);
    uint32_t index;
};

// An allocation point, emitted as a constant in trace builds (phase 1c)
struct __Site {
    const char *file;
    uint32_t line, column;
    const char *function;
    uint32_t index;
};

// weakFlags: bits 0-3 flags, bits 4-7 live canary (debug checks), bits 8-31 weak
// count (phase 8). Trace data never lives in this header (it sits before it).
typedef struct __attribute__((aligned(16))) __ObjHeader {
    uint32_t strong;
    uint32_t weakFlags;
    union {
        const __TypeInfo *type;
        sptr_dctor legacyDtor; // objects from __smart_ptr_alloc, until phase 1b
    };
} __ObjHeader;

#define __MM_F_DYING 0x01U
#define __MM_F_CONSTRUCTED 0x02U
#define __MM_F_SHARED 0x04U
#define __MM_F_LEGACY 0x08U
#define __MM_CANARY 0x60U
#define __MM_CANARY_MASK 0xF0U
#define __MM_DEAD 0xDEADDEADU

// Counts saturate instead of wrapping: above the limit an object is never freed
#define __MM_RC_LIMIT 0x40000000U
#define __MM_RC_SATURATED 0xC0000000U

#define __MM_FAULT_OVERFLOW 1
#define __MM_FAULT_UNDERFLOW 2
#define __MM_FAULT_RETAIN_DYING 3

// Layout checks: the trace library and generated code assume these offsets
_Static_assert(sizeof(__ObjHeader) == 16, "object header must stay 16 bytes");
_Static_assert(__builtin_offsetof(__ObjHeader, strong) == 0, "strong offset");
_Static_assert(__builtin_offsetof(__ObjHeader, weakFlags) == 4, "weakFlags offset");
_Static_assert(__builtin_offsetof(__ObjHeader, type) == 8, "type offset");
_Static_assert(_Alignof(__ObjHeader) == 16, "object header must be 16-byte aligned");

#define __MM_HEADER(obj) ((__ObjHeader *)((char *)(obj) - sizeof(__ObjHeader)))

#if defined(ENABLE_MEMORY_TRACE)
struct __TraceList;

// Registry entry in front of every object and buffer in trace builds. Everything
// after it (__ObjHeader and payload for objects, the payload for buffers) is
// identical in both builds, only the distance to the start of the block differs.
typedef struct __attribute__((aligned(16))) __TraceHeader {
    const __Site *site;
    uint64_t birthTick;
    struct __TraceHeader *prev, *next;
    struct __TraceList *list; // owned by libcxy-trace, null when not registered
    uint64_t size;            // payload bytes
    uint32_t kind;            // __MM_KIND_*
    uint32_t reserved;
    uint64_t stackId;         // level 2
} __TraceHeader;

_Static_assert(sizeof(__TraceHeader) == 64, "trace header must stay 64 bytes");
_Static_assert(_Alignof(__TraceHeader) == 16, "trace header must be 16-byte aligned");

#define __MM_KIND_OBJECT 0
#define __MM_KIND_BUFFER 1
#define __MM_PREFIX sizeof(__TraceHeader)
#define __MM_TRACE(h) ((__TraceHeader *)((char *)(h) - sizeof(__TraceHeader)))
#else
#define __MM_PREFIX 0
#endif

#if defined(ENABLE_MEMORY_TRACE)
// Implemented in stdlib/trace/mem.c (libcxy-trace)
extern void __cxy_mem_trace_start(void);
// Entries are registered after the allocation and unregistered before the free
extern void __cxy_mem_trace_register(__TraceHeader *th);
extern void __cxy_mem_trace_unregister(__TraceHeader *th);
extern void __cxy_mem_trace_event(void *ptr, const char *op, uint32_t refs, const char *path, uint64_t line);
extern void __cxy_mem_trace_dump(void *ptr);
// Cumulative per-type and per-site counters, indexes come from __TypeInfo / __Site
extern void __cxy_mem_trace_count(uint32_t type, uint32_t site, uint64_t bytes);
extern void __cxy_mem_trace_type_free(uint32_t type);

// Lets the trace library read refcounts without knowing the header layout
__attribute__((used)) uint32_t __cxy_mem_header_refs(void *ptr)
{
    return ptr ? __MM_HEADER(ptr)->strong : 0;
}

extern void __cxy_mem_trace_report(void);
#define __cxy_start_runtime() __cxy_mem_trace_start()
#define __cxy_report_runtime() __cxy_mem_trace_report()
#else
#define __cxy_start_runtime()
#define __cxy_report_runtime()
#endif

// Single exit sequence (destructor priorities are not portable to Mach-O):
// thread finalizers, module finalizers, then the leak report.
__attribute__((destructor)) static void __cxy_shutdown(void)
{
    __cxy_run_thread_finis();
    for (unsigned i = __cxy_process_fini_count; i > 0; i--)
        __cxy_process_finis[i - 1]();
    __cxy_report_runtime();
}

static inline void __cxy_panic_mem(sptr ptr, const char *file, uint64_t line, const char *msg)
{
    __ObjHeader *mem = ptr ? __MM_HEADER(ptr) : nullptr;
    const char *reason = "Wild Pointer / Corrupted Header";
    if (ptr == nullptr) {
        reason = "Null Pointer Dereference";
    }
    else if (((unsigned long)ptr & 0x0F) != 0) {
        reason = "Misaligned / Interior Pointer (Offset into object)";
    }
    else if (mem != nullptr && mem->weakFlags == __MM_DEAD) {
        reason = "USE-AFTER-FREE / DOUBLE-DROP (Object already destroyed and freed)";
    }

    char buf[256];
    write(2, "panic: ", 7);
    write(2, msg, strlen(msg));
    int len = sprintf(buf, " [%s] ptr=%p\n  @ %s:%llu:0\n",
                      reason,
                      ptr,
                      file ? file : "<unknown>",
                      (unsigned long long)line);
    write(2, buf, len);
#if defined(ENABLE_MEMORY_TRACE)
    __cxy_mem_trace_dump(ptr);
#endif
    abort();
}

// Runtime unit tests observe faults instead of aborting
#if defined(CXY_RUNTIME_TEST)
static void (*__mm_fault_hook)(int kind, void *obj);
#endif

// Slow path of a count fault. The count is saturated first so that nothing can
// free the object afterwards. Overflow aborts only in strict mode, underflow and
// retaining a dying object abort in debug builds.
__attribute__((noinline, cold)) static void __mm_fault(__ObjHeader *h, int kind)
{
    if (kind != __MM_FAULT_RETAIN_DYING)
        __atomic_store_n(&h->strong, __MM_RC_SATURATED, __ATOMIC_RELAXED);
#if defined(CXY_RUNTIME_TEST)
    if (__mm_fault_hook != nullptr) {
        __mm_fault_hook(kind, (char *)h + sizeof(__ObjHeader));
        return;
    }
#endif
    const char *msg = nullptr;
#if defined(CXY_RC_STRICT)
    if (kind == __MM_FAULT_OVERFLOW)
        msg = "reference count overflow";
#endif
#ifndef NDEBUG
    if (kind == __MM_FAULT_UNDERFLOW)
        msg = "reference count underflow (object released too many times)";
    else if (kind == __MM_FAULT_RETAIN_DYING)
        msg = "retain of an object that is being destroyed";
#endif
    if (msg != nullptr)
        __cxy_panic_mem((char *)h + sizeof(__ObjHeader), nullptr, 0, msg);
}

/* --------------------------------------------------------------- freeing */

#if !defined(NDEBUG)
#if defined(__APPLE__)
extern unsigned long malloc_size(const void *ptr);
#define __MM_BLOCK_SIZE(p) malloc_size(p)
#elif defined(__linux__)
extern unsigned long malloc_usable_size(void *ptr);
#define __MM_BLOCK_SIZE(p) malloc_usable_size(p)
#else
#define __MM_BLOCK_SIZE(p) sizeof(__ObjHeader)
#endif

#if defined(__SANITIZE_ADDRESS__)
#define __MM_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define __MM_ASAN 1
#endif
#endif

#if !defined(__MM_ASAN)
// Freed blocks wait here (oldest freed first) so a late use still sees poison
#define __MM_QUARANTINE_SLOTS 128
static void *__mm_quarantine[__MM_QUARANTINE_SLOTS];
static unsigned __mm_quarantine_pos;
#endif
#endif

// Frees an object block. Debug builds poison it and delay the real free.
static inline void __mm_free_block(__ObjHeader *h)
{
    void *base = (char *)h - __MM_PREFIX;
#if !defined(NDEBUG)
    unsigned long total = __MM_BLOCK_SIZE(base);
    unsigned long used = __MM_PREFIX + sizeof(__ObjHeader);
    if (total > used)
        memset((char *)base + used, 0xDD, total - used);
    h->weakFlags = __MM_DEAD;
#if !defined(__MM_ASAN)
    unsigned slot = __atomic_fetch_add(&__mm_quarantine_pos, 1, __ATOMIC_RELAXED) %
                    __MM_QUARANTINE_SLOTS;
    base = __atomic_exchange_n(&__mm_quarantine[slot], base, __ATOMIC_ACQ_REL);
    if (base == nullptr)
        return;
#endif
#endif
    free(base);
}

static inline void __mm_destroy(__ObjHeader *h, void *obj)
{
    __atomic_fetch_or(&h->weakFlags, __MM_F_DYING, __ATOMIC_RELAXED);
    if (h->weakFlags & __MM_F_LEGACY) {
        if (h->legacyDtor != nullptr)
            h->legacyDtor(obj);
    }
    else if (h->type != nullptr) {
        if (h->type->deinit != nullptr)
            h->type->deinit(obj);
        if (h->type->dropFields != nullptr)
            h->type->dropFields(obj);
    }
#if defined(ENABLE_MEMORY_TRACE)
    if (!(h->weakFlags & __MM_F_LEGACY) && h->type != nullptr)
        __cxy_mem_trace_type_free(h->type->index);
    __cxy_mem_trace_unregister(__MM_TRACE(h));
#endif
    __mm_free_block(h);
}

/* ------------------------------------------------------------- allocation */

// Block with a live header, strong = 1. The payload is not initialized. Trace
// builds register it, `site` may be null.
static inline void *__mm_alloc_block(uint64_t size, uint32_t flags, const __Site *site)
{
    char *base = (char *)malloc(__MM_PREFIX + sizeof(__ObjHeader) + size);
    if (base == nullptr)
        return nullptr;
    __ObjHeader *h = (__ObjHeader *)(base + __MM_PREFIX);
    h->weakFlags = __MM_CANARY | flags;
    __atomic_store_n(&h->strong, 1, __ATOMIC_RELAXED);
    h->type = nullptr;
#if defined(ENABLE_MEMORY_TRACE)
    __TraceHeader *th = (__TraceHeader *)base;
    memset(th, 0, sizeof(__TraceHeader));
    th->site = site;
    th->size = size;
    th->kind = __MM_KIND_OBJECT;
    __cxy_mem_trace_register(th);
#else
    (void)site;
#endif
    return (char *)h + sizeof(__ObjHeader);
}

// Typed allocation: strong = 1, payload zeroed. `site` is used by trace builds.
static inline void *__mm_alloc(const __TypeInfo *type, const __Site *site)
{
    void *obj = __mm_alloc_block(type->size, 0, site);
    if (obj == nullptr)
        return nullptr;
    memset(obj, 0, type->size);
    __MM_HEADER(obj)->type = type;
    return obj;
}

static inline void *__mm_retain(void *obj)
{
    if (obj == nullptr)
        return nullptr;
    __ObjHeader *h = __MM_HEADER(obj);
#ifndef NDEBUG
    if (unlikely((h->weakFlags & __MM_CANARY_MASK) != __MM_CANARY)) {
        __cxy_panic_mem(obj, __FILE__, __LINE__, "invalid pointer retain");
    }
    if (unlikely(h->weakFlags & __MM_F_DYING))
        __mm_fault(h, __MM_FAULT_RETAIN_DYING);
#endif
    uint32_t old = __atomic_fetch_add(&h->strong, 1, __ATOMIC_RELAXED);
    if (unlikely(old >= __MM_RC_LIMIT))
        __mm_fault(h, __MM_FAULT_OVERFLOW);
    return obj;
}

// Returns true when this release destroyed the object
static inline bool __mm_release(void *obj)
{
    if (obj == nullptr)
        return false;
    __ObjHeader *h = __MM_HEADER(obj);
#ifndef NDEBUG
    if (unlikely((h->weakFlags & __MM_CANARY_MASK) != __MM_CANARY)) {
        __cxy_panic_mem(obj, __FILE__, __LINE__, "invalid smart pointer drop");
    }
#endif
    uint32_t old = __atomic_fetch_sub(&h->strong, 1, __ATOMIC_RELEASE);
    if (likely(old == 1)) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        __mm_destroy(h, obj);
        return true;
    }
    if (unlikely(old == 0))
        __mm_fault(h, __MM_FAULT_UNDERFLOW);
    else if (unlikely(old > __MM_RC_LIMIT))
        __atomic_store_n(&h->strong, __MM_RC_SATURATED, __ATOMIC_RELAXED);
    return false;
}

/* ------------------------------------------------- container storage */

// Storage owned by exactly one container. Release builds call libc directly: no
// header, no bookkeeping. Trace builds register the block with the allocation
// point; the backend calls the `_trace` forms with the site (hidden parameter
// of a `@memtrace(callsite)` caller, or the call expression's own).
#if defined(ENABLE_MEMORY_TRACE)
// A buffer is a registry entry followed by the caller's bytes: a block obtained
// here must be released with __free_buffer, never with libc free.
static inline void *__buffer_register(void *base, uint64_t bytes, const __Site *site)
{
    if (base == nullptr)
        return nullptr;
    __TraceHeader *th = (__TraceHeader *)base;
    th->site = site;
    th->size = bytes;
    th->kind = __MM_KIND_BUFFER;
    th->prev = nullptr;
    th->next = nullptr;
    th->list = nullptr;
    th->stackId = 0;
    __cxy_mem_trace_register(th);
    if (site != nullptr)
        __cxy_mem_trace_count(0xFFFFFFFFU, site->index, bytes);
    return (char *)base + sizeof(__TraceHeader);
}

static inline void *__alloc_buffer_trace(uint64_t bytes, const __Site *site)
{
    return __buffer_register(malloc(sizeof(__TraceHeader) + bytes), bytes, site);
}

static inline void *__calloc_buffer_trace(uint64_t bytes, const __Site *site)
{
    return __buffer_register(calloc(1, sizeof(__TraceHeader) + bytes), bytes, site);
}

// The grown block is charged to the call that grew it
static inline void *__realloc_buffer_trace(void *old, uint64_t bytes, const __Site *site)
{
    if (old == nullptr)
        return __alloc_buffer_trace(bytes, site);
    __TraceHeader *th = __MM_TRACE(old);
    __cxy_mem_trace_unregister(th);
    void *grown = realloc(th, sizeof(__TraceHeader) + bytes);
    if (grown == nullptr) {
        __cxy_mem_trace_register(th);
        return nullptr;
    }
    return __buffer_register(grown, bytes, site);
}

static inline void __free_buffer(void *ptr)
{
    if (ptr == nullptr)
        return;
    __TraceHeader *th = __MM_TRACE(ptr);
    __cxy_mem_trace_unregister(th);
    free(th);
}
#else
static inline void *__alloc_buffer(uint64_t bytes)
{
    return malloc(bytes);
}

static inline void *__calloc_buffer(uint64_t bytes)
{
    return calloc(1, bytes);
}

static inline void *__realloc_buffer(void *old, uint64_t bytes)
{
    return realloc(old, bytes);
}

static inline void __free_buffer(void *ptr)
{
    free(ptr);
}
#endif

/* ------------------------------------------------- shims for generated code */

static inline sptr __smart_ptr_alloc(uint64_t size, sptr_dctor dctor)
{
    void *obj = __mm_alloc_block(size, __MM_F_LEGACY, nullptr);
    if (obj != nullptr)
        __MM_HEADER(obj)->legacyDtor = dctor;
    return obj;
}

static inline sptr __smart_ptr_get(sptr ptr)
{
    return __mm_retain(ptr);
}

static inline bool __smart_ptr_drop(sptr ptr)
{
    return __mm_release(ptr);
}

#if defined(ENABLE_MEMORY_TRACE)
#ifdef TRACE_MEMORY
extern void TRACE_MEMORY(sptr ptr, const char *op, const char *path, uint64_t line);
#define CXY_MEMORY_TRACE(ptr, op, path, line) TRACE_MEMORY(ptr, op, path, line)
#else
#define CXY_MEMORY_TRACE(ptr, op, path, line) \
    __cxy_mem_trace_event(ptr, op, __cxy_mem_header_refs(ptr), path, line)
#endif

static inline sptr __smart_ptr_alloc_trace(uint64_t size, sptr_dctor dctor, const char *path, uint64_t line)
{
    sptr ptr = __smart_ptr_alloc(size, dctor);
    CXY_MEMORY_TRACE(ptr, "alloc", path, line);
    return ptr;
}

static inline sptr __mm_alloc_trace(const __TypeInfo *type, const __Site *site)
{
    sptr ptr = __mm_alloc(type, site);
    __cxy_mem_trace_count(type->index, site->index, type->size);
    CXY_MEMORY_TRACE(ptr, "alloc", site->file, site->line);
    return ptr;
}

static inline sptr __smart_ptr_get_trace(sptr ptr, const char *path, uint64_t line)
{
    if (ptr == nullptr)
        return nullptr;
#ifndef NDEBUG
    if (unlikely((__MM_HEADER(ptr)->weakFlags & __MM_CANARY_MASK) != __MM_CANARY)) {
        __cxy_panic_mem(ptr, path, line, "invalid pointer retain");
    }
#endif
    sptr res = __smart_ptr_get(ptr);
    CXY_MEMORY_TRACE(res, "get", path, line);
    return res;
}

static inline bool __smart_ptr_drop_trace(sptr ptr, const char *path, uint64_t line)
{
    if (ptr == nullptr)
        return false;
#ifndef NDEBUG
    if (unlikely((__MM_HEADER(ptr)->weakFlags & __MM_CANARY_MASK) != __MM_CANARY)) {
        __cxy_panic_mem(ptr, path, line, "invalid smart pointer drop");
    }
#endif
    CXY_MEMORY_TRACE(ptr, "drop", path, line);
    return __smart_ptr_drop(ptr);
}
#else
// path/line are always literals from codegen, so dropping them is safe
#define __mm_alloc_trace(type, site) __mm_alloc(type, nullptr)
#define __smart_ptr_alloc_trace(size, dctor, path, line) __smart_ptr_alloc(size, dctor)
#define __smart_ptr_get_trace(ptr, path, line) __smart_ptr_get(ptr)
#define __smart_ptr_drop_trace(ptr, path, line) __smart_ptr_drop(ptr)
#endif
