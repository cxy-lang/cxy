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
#define __MM_FAULT_DANGLING_REF 4

// Layout checks: the trace library and generated code assume these offsets
_Static_assert(sizeof(__ObjHeader) == 16, "object header must stay 16 bytes");
_Static_assert(__builtin_offsetof(__ObjHeader, strong) == 0, "strong offset");
_Static_assert(__builtin_offsetof(__ObjHeader, weakFlags) == 4, "weakFlags offset");
_Static_assert(__builtin_offsetof(__ObjHeader, type) == 8, "type offset");
_Static_assert(_Alignof(__ObjHeader) == 16, "object header must be 16-byte aligned");

#define __MM_HEADER(obj) ((__ObjHeader *)((char *)(obj) - sizeof(__ObjHeader)))

// Memory trace hooks. Builds with memory trace add mm_trace.h after this file: it
// defines the hooks and puts a registry entry of __MM_PREFIX bytes in front of every
// block. Without it the hooks compile to nothing.
#if defined(ENABLE_MEMORY_TRACE)
#define __MM_PREFIX 64
static inline void __mm_trace_alloc(void *base, uint64_t size, const __Site *site);
static inline void __mm_trace_destroy(__ObjHeader *h);
static inline void __mm_trace_dump(void *ptr);
static inline void __mm_trace_report(void);
#else
#define __MM_PREFIX 0
#define __cxy_start_runtime()
static inline void __mm_trace_alloc(void *base, uint64_t size, const __Site *site)
{
    (void)base;
    (void)size;
    (void)site;
}
static inline void __mm_trace_destroy(__ObjHeader *h)
{
    (void)h;
}
static inline void __mm_trace_dump(void *ptr)
{
    (void)ptr;
}
static inline void __mm_trace_report(void) {}
#endif

// Single exit sequence (destructor priorities are not portable to Mach-O):
// thread finalizers, module finalizers, then the leak report.
__attribute__((destructor)) static void __cxy_shutdown(void)
{
    __cxy_run_thread_finis();
    for (unsigned i = __cxy_process_fini_count; i > 0; i--)
        __cxy_process_finis[i - 1]();
    __mm_trace_report();
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
    __mm_trace_dump(ptr);
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
    __mm_trace_destroy(h);
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
    __mm_trace_alloc(base, size, site);
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

/* ------------------------------------------------- borrowed references */

// A `&T` read from a field or returned by a call carries no count. Debug builds
// check the header before the borrow is used, release builds read it as is.
#ifndef NDEBUG
__attribute__((noinline, cold)) static void __mm_dangling(void *obj, const char *file, uint64_t line)
{
#if defined(CXY_RUNTIME_TEST)
    if (__mm_fault_hook != nullptr) {
        __mm_fault_hook(__MM_FAULT_DANGLING_REF, obj);
        return;
    }
#endif
    __cxy_panic_mem(obj, file, line, "borrowed reference used after its owner was destroyed");
}

static inline void *__mm_check_live(void *obj, const char *file, uint64_t line)
{
    if (obj == nullptr)
        return obj;
    __ObjHeader *h = __MM_HEADER(obj);
    if (unlikely((h->weakFlags & __MM_CANARY_MASK) != __MM_CANARY))
        __mm_dangling(obj, file, line);
    return obj;
}
#define __mm_ref(p, file, line) ((__typeof__(p))__mm_check_live((p), (file), (line)))
#else
#define __mm_ref(p, file, line) (p)
#endif

/* ------------------------------------------------- container storage */

// Storage owned by exactly one container. Release builds call libc directly: no
// header, no bookkeeping. Builds with memory trace use the forms in mm_trace.h,
// which register the block with its allocation point.
#if !defined(ENABLE_MEMORY_TRACE)
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

// Builds with memory trace define the real `_trace` forms in mm_trace.h. Without
// it, path and line are always literals from codegen, so dropping them is safe
#if !defined(ENABLE_MEMORY_TRACE)
#define __mm_alloc_trace(type, site) __mm_alloc(type, nullptr)
#define __smart_ptr_alloc_trace(size, dctor, path, line) __smart_ptr_alloc(size, dctor)
#define __smart_ptr_get_trace(ptr, path, line) __smart_ptr_get(ptr)
#define __smart_ptr_drop_trace(ptr, path, line) __smart_ptr_drop(ptr)
#endif
