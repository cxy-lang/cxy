// Memory trace support. Added to the generated C only in builds with memory trace,
// after mm.h: the registry entry in front of every block, the hooks into
// libcxy-trace, the edge visitors and the root table. mm.h calls the `__mm_trace_*`
// hooks that are defined here, and compiles them to nothing without this file.

struct __TraceList;

// Registry entry in front of every object and buffer. Everything after it
// (__ObjHeader and payload for objects, the payload for buffers) is identical in
// both builds, only the distance to the start of the block differs.
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
_Static_assert(sizeof(__TraceHeader) == __MM_PREFIX, "__MM_PREFIX in mm.h must match");
_Static_assert(_Alignof(__TraceHeader) == 16, "trace header must be 16-byte aligned");

#define __MM_KIND_OBJECT 0
#define __MM_KIND_BUFFER 1
#define __MM_TRACE(h) ((__TraceHeader *)((char *)(h) - sizeof(__TraceHeader)))

// Implemented in stdlib/trace/mem.c (libcxy-trace)
extern void __cxy_mem_trace_start(void);
// Entries are registered after the allocation and unregistered before the free
extern void __cxy_mem_trace_register(__TraceHeader *th);
extern void __cxy_mem_trace_unregister(__TraceHeader *th);
extern void __cxy_mem_trace_event(void *ptr, const char *op, uint32_t refs, const char *path, uint64_t line);
extern void __cxy_mem_trace_dump(void *ptr);
extern void __cxy_mem_trace_report(void);
// Cumulative per-type and per-site counters, indexes come from __TypeInfo / __Site
extern void __cxy_mem_trace_count(uint32_t type, uint32_t site, uint64_t bytes);
extern void __cxy_mem_trace_type_free(uint32_t type);

#define __cxy_start_runtime() __cxy_mem_trace_start()

// The hooks mm.h calls
static inline void __mm_trace_alloc(void *base, uint64_t size, const __Site *site)
{
    __TraceHeader *th = (__TraceHeader *)base;
    memset(th, 0, sizeof(__TraceHeader));
    th->site = site;
    th->size = size;
    th->kind = __MM_KIND_OBJECT;
    __cxy_mem_trace_register(th);
}

static inline void __mm_trace_destroy(__ObjHeader *h)
{
    if (!(h->weakFlags & __MM_F_LEGACY) && h->type != nullptr)
        __cxy_mem_trace_type_free(h->type->index);
    __cxy_mem_trace_unregister(__MM_TRACE(h));
}

static inline void __mm_trace_dump(void *ptr)
{
    __cxy_mem_trace_dump(ptr);
}

static inline void __mm_trace_report(void)
{
    __cxy_mem_trace_report();
}

// Lets the trace library read refcounts without knowing the header layout
__attribute__((used)) uint32_t __cxy_mem_header_refs(void *ptr)
{
    return ptr ? __MM_HEADER(ptr)->strong : 0;
}

/* ------------------------------------------------------------ edge visitors */

// A class's TypeInfo.visit reports every reference the object owns as an edge, the
// trace library supplies the callbacks. The glue is generated from the field
// types, containers need no tracing code.
typedef struct __Visitor {
    void (*edge)(struct __Visitor *v, const void *from, const void *to);
    // Bytes of the registered buffer that starts at `ptr`, 0 if there is none
    uint64_t (*bufferBytes)(struct __Visitor *v, const void *ptr);
    // A `&T` field: a borrow, the owner of `from` does not own `to`
    void (*borrow)(struct __Visitor *v, const void *from, const void *to);
    void *ctx;
} __Visitor;

static inline void __visit_edge(void *visitor, void *from, void *to)
{
    if (to != nullptr)
        ((__Visitor *)visitor)->edge((__Visitor *)visitor, from, to);
}

static inline void __visit_borrow(void *visitor, void *from, void *to)
{
    __Visitor *v = (__Visitor *)visitor;
    if (to != nullptr && v->borrow != nullptr)
        v->borrow(v, from, to);
}

// A global variable that holds owning references (a class handle, an optional,
// a struct with such fields). The generated `visit` reports them as edges from
// the global's own address, so they can be named: `global cache -> Vector -> ...`.
typedef struct __RootInfo {
    const char *name;
    void *addr;
    uint64_t size;
    void (*visit)(void *visitor, void *root);
} __RootInfo;

// A raw pointer field: when it points at a registered buffer the owner owns it,
// and `elem` (null when elements own nothing) reports what each slot of the
// buffer owns. Unused slots are zero in the containers, so they report nothing.
static inline void __visit_buffer(void *visitor,
                                  void *owner,
                                  void *buf,
                                  uint64_t elemSize,
                                  void (*elem)(void *visitor, void *buf, void *slot))
{
    if (buf == nullptr)
        return;
    __Visitor *v = (__Visitor *)visitor;
    uint64_t bytes = v->bufferBytes(v, buf);
    if (bytes == 0)
        return;
    v->edge(v, owner, buf);
    if (elem == nullptr || elemSize == 0)
        return;
    for (uint64_t off = 0; off + elemSize <= bytes; off += elemSize)
        elem(visitor, buf, (char *)buf + off);
}

/* ------------------------------------------------------- container storage */

// A buffer is a registry entry followed by the caller's bytes: a block obtained
// here must be released with __free_buffer, never with libc free. The backend
// calls the `_trace` forms with the site (hidden parameter of a
// `@memtrace(callsite)` caller, or the call expression's own).
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

/* ------------------------------------------------- shims for generated code */

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
