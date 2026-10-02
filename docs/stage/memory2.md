# Comprehensive Memory Management & ARC Architecture Design (v2)

## 1. Executive Summary & Goals

This document specifies the complete overhaul of Cxy's Automatic Reference Counting (ARC) runtime, allocator architecture, compiler dataflow passes, and diagnostic subsystems.

### Core Objectives
1. **Zero Unnecessary Synchronization**: Move from sequential consistency (`seq_cst`) to relaxed retains and release-acquire destruction fences (matching Swift/LLVM ARC).
2. **Minimal Header Overhead & Strict Alignment**: Guarantee 16-byte aligned headers and payloads with zero wasted padding.
3. **Type Descriptor Specialization**: Migrate destructor dispatch from per-object function pointers to static class descriptors and vtables.
4. **Comprehensive Diagnostic & Tracing Subsystem**: Low-overhead lock-free ring-buffer tracing, callstack capture via `libunwind` / `backtrace`, canary poisoning, and leak attribution.
5. **Full Compiler Middle-End Integration**: MIR/IR optimization passes to coalesce, pair, and hoist retains/releases.

---

## 2. Runtime Header Architecture & Allocator Overhaul

### 2.1 Object Header Layout (`__mem`)

```c
typedef struct __attribute__((aligned(16))) __mem {
    _Atomic uint32_t refs;       // Strong reference count
    uint32_t flags;             // Magic tag (0xAEAEAE00) + Type Tag / Status flags
    sptr_dctor dctor;           // Destructor pointer (or class metadata pointer)
} __mem;
```

- **Header Size**: Exactly 16 bytes.
- **Payload Alignment**: Always 16-byte aligned from `malloc`, supporting vector/SIMD, `int128`, and hardware cachelines.

### 2.2 Memory Ordering & Atomic Semantics
- **Retain (`__smart_ptr_get`)**:
  - Emits `__atomic_fetch_add(&mem->refs, 1, __ATOMIC_RELAXED)`.
  - Zero memory barrier overhead on x86/ARM.
- **Release (`__smart_ptr_drop`)**:
  - Decrements with `__atomic_fetch_sub(&mem->refs, 1, __ATOMIC_RELEASE)`.
  - When reaching zero, executes `__atomic_thread_fence(__ATOMIC_ACQUIRE)` to synchronize with all prior threads before invoking `mem->dctor(ptr)` and `free(mem)`.

### 2.3 Debug vs. Release Header Modes
- **Release (`-DNDEBUG`)**:
  - Strips magic/canary checks.
  - Retain/release compiles directly to single assembly instructions (`lock incl`, `lock decl`).
- **Debug / ASan**:
  - Validates `(mem->flags & 0xFFFFFF00U) == 0xAEAEAE00U` on every retain/drop.
  - Poisons freed memory headers with `0xDEADDEADU`.

### 2.4 Weak Reference & Side-Table Architecture (Swift-Style)

To support weak references without cycle leaks or bloating every standard object header:

#### A. Side-Table Allocation on Demand
1. Objects start with an **inline 16-byte header**:
   - `strong_refs` (28 bits) + `unowned_refs` (32 bits) + `flags` (4 bits).
   - If `flags & HAS_SIDE_TABLE == 0`, counts are stored directly inline.
2. When the first `weak` reference is taken on an object:
   - A separate `HeapObjectSideTableEntry` is dynamically allocated.
   - The inline header sets `HAS_SIDE_TABLE = 1` and stores the 64-bit side-table pointer.
   - The side-table contains:
     ```c
     typedef struct HeapObjectSideTableEntry {
         void *object;
         _Atomic uint32_t strong_refs;
         _Atomic uint32_t unowned_refs;
         _Atomic uint32_t weak_refs;
     } HeapObjectSideTableEntry;
     ```

#### B. Weak Pointer Lifecycle (`weak var x: Class?`)
- **Strong destruction (`strong_refs == 0`)**:
  - `dctor(ptr)` runs and frees class members/resources.
  - The payload memory can be deallocated immediately, leaving only the side-table entry alive.
- **Weak reading (`let strong = weakRef`)**:
  - Atomically tests `sideTable->strong_refs > 0` and increments `strong_refs` (`fetch_add`).
  - If `strong_refs == 0`, returns `null`.
- **Side-table cleanup (`weak_refs == 0 && unowned_refs == 0 && strong_refs == 0`)**:
  - The side-table itself is freed when all weak references are dropped.

---

## 3. Advanced Tracing, Diagnostics & Callstack Sampling

### 3.1 Lock-Free Ring Buffer Trace Engine
When `ENABLE_MEMORY_TRACE` or `-DTRACE_MEMORY` is active:
- Maintain a thread-safe ring buffer recording memory lifecycle events:
  ```c
  typedef enum {
      MEM_EVENT_ALLOC,
      MEM_EVENT_RETAIN,
      MEM_EVENT_RELEASE,
      MEM_EVENT_FREE
  } MemEventType;

  typedef struct {
      uint64_t timestamp;
      void *ptr;
      uint32_t fromRefs;
      uint32_t toRefs;
      uint32_t threadId;
      void *callstack[8];      // Up to 8 stack frames from libunwind
      const char *file;
      uint32_t line;
  } MemTraceEvent;
  ```

### 3.2 Stack Capture via `libunwind` / `backtrace`
- Capture up to 8 stack frames using `__builtin_return_address` or `unw_backtrace` / `backtrace()`.
- On assertion failure, panic, or detected leak, print the full history of retain/release events with callstacks for the offending pointer.

### 3.3 Leak Summary Report at Process Exit
- Register an `atexit` handler in debug tracing builds.
- Walk active allocations table and report:
  - Exact leak count and total leaked bytes.
  - Allocation location and stack trace for every live root pointer.

---

## 4. Compiler ARC Optimizations (Middle-End)

### 4.1 Retain / Release Pairing & Coalescing
- **Redundant Retain-Drop Elimination**:
  When a local variable is copied into a temporary and immediately consumed/dropped in the same basic block without escaping, eliminate both `__smart_ptr_get` and `__smart_ptr_drop`.
- **Call Argument Borrowing**:
  When passing an active local variable to a function by value where the callee does not escape ownership, borrow the reference instead of inserting a retain-drop pair.

### 4.2 Cast Chain Invariants
- Retain/drop operations apply directly to the concrete root object (`__smart_ptr_get(a)`), with type-casts (`(BaseResource *)`) applied purely as zero-cost pointer reinterpretations on the result.

---

## 5. Implementation Tasks

### Phase 1: Prologue & Allocator Modernization
- [x] Update `__mem` in `src/cxy/runtime/prologue.h` to 16-byte aligned layout with `flags`.
- [x] Implement relaxed atomics for retain and acquire-release fences for drop.
- [x] Add `#ifdef DEBUG` / `#ifdef NDEBUG` branch isolation for magic checks.

### Phase 2: Diagnostic & Unwind Tracing Subsystem
- [x] Add `libunwind` / `backtrace` callstack capture to `__cxy_default_memory_trace`.
- [x] Add lock-free ring-buffer trace recording in debug mode.
- [x] Implement `atexit` leak summarizer in runtime prologue.

### Phase 3: Weak Reference & Side-Table Subsystem
- [ ] Add `weak` keyword token and type modifier in AST (`flgWeak`).
- [ ] Implement `HeapObjectSideTableEntry` and on-demand side-table elevation.
- [ ] Implement `__cxy_weak_init`, `__cxy_weak_load_strong`, and `__cxy_weak_drop`.

### Phase 4: Compiler Optimizations & Header Specialization
- [ ] Implement retain-release coalescing pass in `src/cxy/lang/middle/memory/manage.c`.
- [ ] Implement call-site retain-drop borrowing optimizations.
