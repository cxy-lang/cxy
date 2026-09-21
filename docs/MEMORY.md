# Cxy Memory Management & RAII Design

This document details the architecture, type semantics, and implementation of the memory management and RAII subsystem in Cxy, operating on the IR pipeline (`MemoryMgmt` and `Finalize`) with programmatic helper generation in `sema`.

---

## 1. Overview & Goals

1. **Explicit, Deterministic RAII on IR**:
   Memory management operates directly on the lowered 3-address mutable-local IR (`IrNode`), turning abstract `ScopeEnter`/`ScopeExit` pairs into concrete drop, copy, and move operations before code generation.
2. **Elimination of Comptime Macro Shims**:
   Replace legacy `.cxy` comptime templates (e.g. `__tuple_copy`, `__tuple_dctor`, `__union_copy`, `__union_dctor` in `builtins.cxy`) with programmatically synthesized, compiler-managed helper functions in `sema` that are typed and memoized per distinct type.
3. **Precise Destructor Binding**:
   Heap allocations (`__bc(MemAlloc, Type, null)`) are lowered to `__smart_ptr_alloc(sizeof(Type), dctor)` with the exact destructor function pointer passed into the runtime header, guaranteeing safe recursive deallocation.
4. **Leak-Free Reassignments**:
   Reassigning to an already-initialized destructible variable automatically drops the old value prior to writing the new value.

---

## 2. Type Classification & Destructibility

A type is classified as **destructible** or **requiring copy management** based on its structural composition:

| Type Category | Destructible Conditions | Copy Behavior | Destructor Mechanism |
|---|---|---|---|
| **Class** | Always destructible (heap-allocated reference) | `__smart_ptr_get(ptr)` (ref count ++) | `__smart_ptr_drop(ptr)` (invokes class destructor if ref count reaches 0) |
| **Struct** | Destructible if `flgReferenceMembers` or `flgImplementsDeinit` | Memberwise copy + recursive element copy, or user `op__copy` | Calls user `op__destructor` / `deinit` followed by drops of reference members |
| **Tuple** | Destructible if `flgReferenceMembers` (any member is destructible) | Recursive copy of each member | Programmatically generated `__tuple_dctor_<T>` dropping each destructible element |
| **Tagged Union** | Destructible if `flgReferenceMembers` (any variant is destructible) | Variant switch + recursive copy of active variant | Programmatically generated `__union_dctor_<T>` switching on `tag` and dropping active variant |
| **Optional (`T?`)** | Destructible if `T` is destructible | Copies `ok` flag; if `ok`, recursively copies `val` | Checks `ok` flag; if `true`, drops `val` |
| **Primitive / Pointer** | Never destructible | Plain bitwise / value copy | No-op |

### Flags
- `flgReferenceMembers`: Computed during semantic analysis when a struct, tuple, or union contains at least one field of class, reference, or destructible type.
- `flgImplementsDeinit`: Set on structs and classes that declare a `deinit` / `op__destructor` method.

---

### 2.1 Ownership & Action Matrix: When to Copy, When to Drop

| Program Construct / Syntax | Type & Ownership Context | When to Copy (`copy src -> dst`) | When to Drop (`drop x` / `drop-if`) | Why / Mechanism |
|---|---|---|---|---|
| **Explicit Copy (`__copy!(x)`)** | Any destructible type | **Always** (invokes copy helper / `__smart_ptr_get`) | No | Explicit user copy request |
| **By-Value Local (`var x: T`)** | Owned destructible struct, tuple, union, class | On initialization if RHS is copied | **On `ScopeExit`** (unconditional if `Assigned`, `drop-if` if `MaybeAssigned`) | Local owns resource and must clean up when exiting scope |
| **Reassignment (`x = new_val`)** | Already-initialized destructible variable (`x`) | No (moves `new_val` into `x`) | **Before overwrite** (`drop x` or `drop-if x`) | Prevents resource leak by destroying previous value before overwrite |
| **By-Value Parameter (`func f(x: T)`)** | Owned destructible parameter | At call site (caller copies or moves argument) | **On function `ScopeExit 0`** | Callee assumes ownership of by-value argument and must drop it on return |
| **Borrowed Reference (`x: &T`)** | Borrowed parameter / variable | **Never** | **Never** | Non-owning reference; caller retains ownership |
| **Raw Pointer (`p: ^T`, `ptr: ^void`)** | Non-owning raw pointer | **Never** | **Never** | Raw pointers have no lifecycle management |
| **`this` Parameter (`this: &This`)** | Borrowed instance reference | **Never** | **Never** | Borrowed instance reference owned by caller |
| **Moved Variable (`&&x`)** | Variable in `irVarMoved` state | **Never** | **Never** (state transitions to `Moved`) | Ownership transferred to new owner; original variable must not drop |
| **Return Slot (`__ret`)** | Value returned to caller | No | **Never** (marked `flgReturned`) | Transfers ownership of returned object to caller |
| **Compiler Temporary (`__tN`)** | Unmoved temporary owning value | No | **Eagerly** (after consuming statement) | Eagerly freed to prevent memory buildup in loops |

---

## 3. Programmatic Helper Generation in `sema`

Rather than relying on comptime template macro evaluation in userland, `sema` synthesizes and memoizes concrete AST/IR helper functions for composite types that require memory management:

### 3.1 Struct and Class Destructors (`op__destructor`)

For any struct or class that is destructible (`isDestructible(type)` is true, due to `flgImplementsDeinit` and/or `flgReferenceMembers`):

The compiler generates (or finalizes) `TypeName_op__destructor(this: &TypeName): void` with the following strict execution order:
1. **User `deinit`**: If the struct or class declares a `deinit` method, `this.deinit()` is called first.
2. **Recursive Member Destruction**: Enumerates all struct/class fields in declaration order. For each field `this.field` whose type is destructible, emits `drop this.field` (or calls the member's destructor).
3. **Base Class Destructor (for derived classes)**: If the class inherits from a base class, invokes `BaseClass_op__destructor((&BaseClass)this)`.

```cxy
func TypeName_op__destructor(this: &TypeName): void {
    // 1. Call user deinit if implemented:
    this.deinit()

    // 2. Drop destructible fields in declaration order:
    drop this.memberA
    drop this.memberB

    // 3. Chain to base class destructor (if derived class):
    BaseClass_op__destructor((&BaseClass)this)
}
```

### 3.2 Tuple Helpers

For every distinct tuple type `(T0, T1, ...)` where `hasReferenceMembers(tupleType)` is true:

1. **Destructor (`tuple_dctor`)**:
   ```cxy
   func __tuple_dctor(tup: &(T0, T1, ...)): void {
       // Emitted only for members Ti that are destructible:
       drop tup.0
       drop tup.1
       ...
   }
   ```
2. **Copy Helper (`tuple_copy`)**:
   ```cxy
   func __tuple_copy(tup: &(T0, T1, ...)): (T0, T1, ...) {
       return (__copy!(tup.0), __copy!(tup.1), ...)
   }
   ```

### 3.3 Tagged Union Helpers

For every distinct tagged union `A | B | C` where `hasReferenceMembers(unionType)` is true:

1. **Destructor (`union_dctor`)**:
   ```cxy
   func __union_dctor(u: &(A | B | C)): void {
       switch (u.tag) {
           0 => drop u.val._0   // if A is destructible
           1 => drop u.val._1   // if B is destructible
           ...
           ... => {}
       }
   }
   ```
2. **Copy Helper (`union_copy`)**:
   ```cxy
   func __union_copy(u: &(A | B | C)): A | B | C {
       var res: A | B | C
       res.tag = u.tag
       switch (u.tag) {
           0 => res.val._0 = __copy!(u.val._0)
           1 => res.val._1 = __copy!(u.val._1)
           ...
       }
       return res
   }
   ```

---

## 4. Class Allocation & Destructor Registration

When creating a class instance:
1. The class destructor function (`ClassName_op__destructor`) is resolved or synthesized (combining user `deinit` logic and recursive field drops).
2. The class allocation is lowered:
   ```c
   // Without memory tracing:
   __smart_ptr_alloc(sizeof(ClassName), (sptr_dctor)ClassName_op__destructor)

   // With memory tracing (options.withMemoryTrace == true):
   __smart_ptr_alloc_trace(sizeof(ClassName), (sptr_dctor)ClassName_op__destructor, file!, line!)
   ```
3. When the reference count reaches 0 in `__smart_ptr_drop`, the runtime calls the destructor callback before freeing the allocation block.

---

## 5. The `MemoryMgmt` Pass on IR

The `MemoryMgmt` pass walks each `IrFunction`'s basic block graph and performs scope analysis and RAII insertion:

```
[ Lowered IR with ScopeEnter / ScopeExit ]
                   │
                   ▼
       [ MemoryMgmt Pass ]
  - Track variable states per scope
  - Insert unconditional drops at static exits
  - Synthesize drop flags for divergent branches
  - Insert drops before reassignments
  - Drop compiler temporaries eagerly
                   │
                   ▼
     [ RAII-Complete IR with Scope Markers ]
                   │
                   ▼
       [ Finalize Pass ]
  - Strip ScopeEnter / ScopeExit markers
  - Ensure forward declarations for all referenced helpers
  - Validate single terminator per basic block
                   │
                   ▼
     [ Flat RAII-Complete IR ]
```

### 5.1 Variable State Lattice

For every local variable declared within a scope:
- **`Uninitialized`**: Variable declared (`var x: T;`), no value stored.
- **`Assigned`**: Variable holds a valid, initialized value that must be dropped on exit.
- **`Moved`**: Ownership has been transferred (`&&x`); variable must not be dropped.
- **`MaybeAssigned`**: Variable is initialized along some branches and uninitialized/moved along others.

### 5.2 Drop Flags & Inline `drop-if` Synthesis

When one or more variables have a `MaybeAssigned` state at a `ScopeExit` across divergent execution paths:
1. `MemoryMgmt` allocates an inplace bitfield struct variable `__drop_flags` at function entry (initialized to zero):
   ```cxy
   struct {
       x: i64: 1
       y: i64: 1
       ...
   } __drop_flags = {0}
   ```
2. Emits `__drop_flags.x = 1` at each assignment/initialization site.
3. Emits `__drop_flags.x = 0` at each move site.
4. **Inline `drop-if` Instruction (`irDropIf` / `drop-if x, cond`)**:
   Instead of shattering the CFG into multiple trivial basic blocks per variable, `MemoryMgmt` emits an inline `drop-if` instruction in the current block:
   ```
   drop-if x, __drop_flags.x
   ```
   - **Static Assigned State**: Emits standard unconditional `drop x` (no flag overhead).
   - **MaybeAssigned State**: Emits `drop-if x, __drop_flags.x`.
   - **Codegen (`generate.c`)**:
     - `drop x` -> `TypeName_op__destructor(&x);` (or `__smart_ptr_drop(x);`)
     - `drop-if x, flag` -> `if (flag) TypeName_op__destructor(&x);` (or `if (flag) __smart_ptr_drop(x);`)

This keeps cleanup linear and avoids basic block explosion, mirroring C++ RAII / Rust inline drop-flag patterns.

### 5.3 Reassignment Rule
When assigning to an already-initialized destructible variable:
```cxy
var x = String("First")
x = String("Second")
```
Lowers to:
```
var x: *String
x = String_new("First")
...
var __t0: *String
__t0 = String_new("Second")
drop x           // Drop old value before overwriting
x = &&__t0
```

### 5.4 Chained Cleanup Blocks (Scope Unwinding without Drop Duplication)

When unwinding multiple scopes across control-flow edges (`continue`, `break`, `return`), `MemoryMgmt` avoids duplicating destructor calls by chaining through shared scope cleanup blocks:

```
[ Scope 2 (Inner if) ] ── drop x ──► [ Scope 1 Cleanup (Loop body) ] ── drop z ──► [ for_update ]
                                                                                   │
                                     [ Scope 1 Return Cleanup ]      ── drop z ──► [ Function Exit ] ── drop s ──► return
```

- An early exit in `Scope 2` drops its own variables (`drop x`) and jumps directly to `Scope 1`'s cleanup block.
- `Scope 1`'s cleanup block drops `z` and jumps to the ultimate target (`for_update` or the single function `exit`).
- The single function `exit` block is the only place where function-level locals (`s`) are dropped before returning.

### 5.5 Eager Temporary Cleanup
Compiler-synthesized temporaries (`__tN`, `__optN`) are dropped immediately after the statement that consumes them, rather than waiting for block exit.

### 5.6 Worked Example: Nested Loops, Conditional Initialization & Cross-Scope Unwinding

Consider nested loops with cross-scope conditional assignments, early `continue`, `break`, and `return`:

```cxy
func main() {
  var s: String = null               // s starts null at function entry

  for i in 0..5 {
    var outer: String = null         // outer starts null on each outer iteration

    for j in 0..5 {
      var inner = String("Inner")

      if (i == 1 && j == 1) {
        outer = String("Found Outer") // outer conditionally assigned inside inner loop
        s = String("Found S")         // s conditionally assigned
        continue
      }

      if (j == 3) {
        break
      }

      if (i == 4 && j == 2) {
        return                        // early return when s / outer may be initialized or null
      }
    }
  }
}
```

#### Scope Hierarchy:
```
Scope 0 (Function Scope):                [ s ]
  └── Scope 1 (Outer Loop Iteration):    [ outer ]
        └── Scope 2 (Inner Loop Iteration): [ inner ]
```

#### Drop Flag Lifecycle & Chaining Rules:
1. **Entry Allocation**: `s` and `outer` have `MaybeAssigned` states, so `__drop_flags` is allocated and zeroed at function entry:
   ```cxy
   struct { s: i64: 1; outer: i64: 1 } __drop_flags = {0}
   ```
2. **Loop Iteration Reset**: At the top of each outer loop iteration (`_L2_outer_body`), `__drop_flags.outer = 0` resets `outer`'s state for the fresh iteration.
3. **Reassignment Protection**: Reassigning `outer = ...` or `s = ...` emits `drop-if outer, __drop_flags.outer` before overwriting to prevent leaks if already initialized in an earlier iteration.
4. **Chained Cleanup Landing Pads**:
   - `_L11_cleanup_inner_loop` unconditionally drops `inner` and advances `j`.
   - `_L14_cleanup_outer_loop` drops `outer` conditionally (`drop-if outer, __drop_flags.outer`) and advances `i`.
   - Early `return` (`_L13`) chains LIFO: `drop inner` $\rightarrow$ `drop-if outer` $\rightarrow$ single function `_L16_exit` (`drop-if s; return`).

#### Lowered IR:
```
func main(): void {
_L0_entry:
  struct {
    s: i64: 1
    outer: i64: 1
  } __drop_flags
  __drop_flags.s = 0
  __drop_flags.outer = 0

  var s: *String
  s = null

  var i: i64
  i = 0
  goto _L1_outer_cond

_L1_outer_cond:
  if (i < 5) goto _L2_outer_body else goto _L3_outer_exit

_L2_outer_body:
  __drop_flags.outer = 0              // Reset outer's drop flag for this iteration
  var outer: *String
  outer = null

  var j: i64
  j = 0
  goto _L4_inner_cond

_L4_inner_cond:
  if (j < 5) goto _L5_inner_body else goto _L6_inner_exit

_L5_inner_body:
  var inner: *String
  inner = String_new("Inner")

  // if (i == 1 && j == 1)
  if ((i == 1) && (j == 1)) goto _L7_if_assign else goto _L8_if_assign_end

_L7_if_assign:
  drop-if outer, __drop_flags.outer   // Drop previous value if already initialized
  outer = String_new("Found Outer")
  __drop_flags.outer = 1              // Mark outer as initialized

  drop-if s, __drop_flags.s           // Drop previous value if already initialized
  s = String_new("Found S")
  __drop_flags.s = 1                  // Mark s as initialized

  goto _L11_cleanup_inner_loop        // Chain to inner loop cleanup

_L8_if_assign_end:
  // if (j == 3) -> break inner loop
  if (j == 3) goto _L12_cleanup_inner_break else goto _L9_if_break_end

_L12_cleanup_inner_break:
  drop inner                          // Inner loop drop
  goto _L6_inner_exit                 // Exit inner loop

_L9_if_break_end:
  // if (i == 4 && j == 2) -> return from function
  if ((i == 4) && (j == 2)) goto _L13_cleanup_inner_to_return else goto _L11_cleanup_inner_loop

_L11_cleanup_inner_loop:              // Shared by continue & normal inner iteration
  drop inner                          // Statically assigned: unconditional drop
  j = j + 1
  goto _L4_inner_cond

_L6_inner_exit:                       // Inner loop finished
  goto _L14_cleanup_outer_loop

_L14_cleanup_outer_loop:              // End of outer loop iteration
  drop-if outer, __drop_flags.outer   // Drop outer only if assigned in inner loop
  i = i + 1
  goto _L1_outer_cond

_L13_cleanup_inner_to_return:         // Return unwind step 1: Inner Scope
  drop inner
  goto _L15_cleanup_outer_to_return

_L15_cleanup_outer_to_return:         // Return unwind step 2: Outer Scope
  drop-if outer, __drop_flags.outer   // Drop outer if assigned
  goto _L16_exit                      // Chain to final function exit

_L3_outer_exit:                       // Outer loop finished normally
  goto _L16_exit

_L16_exit:                            // Single function exit block
  drop-if s, __drop_flags.s           // Drop s only if assigned
  return
}
```

---

## 6. The `Finalize` Pass on IR

`Finalize` runs immediately after `MemoryMgmt`:
1. **Strips `ScopeEnter` and `ScopeExit` markers**: Removes all scope markers from every basic block now that their drop sequences have been fully expanded.
2. **Declaration Resolution**: Inspects all referenced helper functions, copy operators, destructors, and runtime calls (`__smart_ptr_*`, `__cxy_assert*`), ensuring each has an explicit declaration in the `IrModule`.
3. **Control-Flow Verification**: Verifies that all `BasicBlock`s terminate in exactly one terminator (`Branch`, `BranchIf`, `Switch`, `Return`, `Unreachable`).
