# Memory manager redesign: reference counting with exact tracing

Status: design. This describes the memory manager as if designed from scratch
for Cxy, and then how to get there from the current one (section 15). Nothing
here is implemented.

Decisions already taken
- Container buffers go through the managed allocator.
- `@memtrace(callsite)` is available to **everyone** (stdlib and user code). It
  only changes trace builds, but its restrictions apply in every build.
- Counts are atomic in version 1. Owner and shared bits are reserved in the
  header for a later biased mode.
- Trace builds use a **fat header** (extra per-object fields); release builds
  do not pay for it.

## 1. Goals and non-goals

Goals
1. Deterministic destruction: `deinit` runs when the last owner goes away.
2. Fewer reference-count operations, and then cheaper ones.
3. Ownership that is explicit in the IR and checked, so a lowering shape that
   hides a move cannot cause a double drop or a leak silently.
4. A tracer with **exact** numbers and **exact** allocation points: the
   allocation point of `Class()` is the source location of that expression.
5. The same behaviour on macOS and Linux, and in both backends (C, LLVM).

Non-goals
- A tracing garbage collector (finalization would not be deterministic).
- Region or lifetime inference, and ownership types in the surface language.
- A cycle collector in release builds. Cycles are found by the tracer and fixed
  with `weak`.

## 2. Semantics that drive the design

| Semantic | Consequence |
|---|---|
| Classes have identity and many holders | Heap objects with a count |
| `deinit` closes files, unlocks mutexes | Reference counting, not tracing |
| Structs are values | No header. Trivial or owning depending on fields. |
| Moves (`&&`) | A move is a bitwise transfer, the source is dead |
| Cycles are possible | `weak` is a type. The tracer finds leaked cycles. |
| Threads and coroutines | Counts are atomic |
| Optionals, unions, results, closures own payloads | Cleanup is derived from the type |
| FFI raw pointers | They never own |

## 3. Object model

### 3.1 Layout

```c
typedef struct ObjHeader {
    _Atomic u32 strong;       // number of owners, starts at 1
    _Atomic u32 weakFlags;    // weak:24 | flags:8
    const TypeInfo *type;
} ObjHeader;                  // 16 bytes, payload follows, 16-byte aligned
```

Flags: `dying` (destructor running), `constructed` (init completed), `shared`
and `immortal` (reserved).

Trace builds put a `TraceHeader` in front of `ObjHeader`:

```c
typedef struct TraceHeader {
    const Site *site;         // where `Class()` was written
    u64 birthTick;            // monotonic allocation counter
    ObjHeader *prev, *next;   // per-thread live list
    u32 thread, stackId;      // allocating thread, interned shadow stack (level 2)
    u64 history[8];           // level 2: last events, Site* | op in the low 3 bits
} TraceHeader;
```

Everything that follows an object pointer (`ObjHeader`, payload) is identical in
both builds. Only the distance to the start of the allocation differs.

### 3.2 TypeInfo

One record per class (and one per buffer element type), emitted by the compiler
into read-only data. Generic instantiations get their own.

```c
typedef struct TypeInfo {
    const char *name;                // "Vector[String]"
    u64 size;                        // payload bytes
    u32 align, kind;                 // kind: object | buffer
    void (*deinit)(void *obj);       // user deinit, or NULL
    void (*dropFields)(void *obj);   // generated glue
    void (*visit)(void *obj, Visitor *v); // exact owned references (trace builds)
    u32 index;                       // for per-type counters
} TypeInfo;
```

`TypeInfo` replaces the bare destructor pointer. The tracer gets names and sizes
from it for free.

### 3.3 Site

```c
typedef struct Site {
    const char *file;
    u32 line, column;
    const char *function;
    u32 index;                       // for per-site counters
} Site;
```

Emitted by the compiler as constants, one per construction or call expression
that can allocate. Used as an address. Release builds neither emit nor pass
them.

## 4. Allocator

API (runtime library, names are placeholders):

```
mm_alloc(type, site)                    -> obj          strong = 1, payload zeroed
mm_alloc_buffer(bytes, elemType, site)  -> buf          owned by exactly one holder
mm_realloc_buffer(buf, bytes, site)     -> buf
mm_free_buffer(buf)
mm_retain(obj) / mm_release(obj)        (compiler operations, see section 8)
mm_weak_retain / mm_weak_release / mm_upgrade
```

- Size-class allocator with thread-local free lists, large blocks to the system
  allocator. Trace builds use the **same** allocator, so the numbers describe the
  real program.
- Buffers (container storage) are first-class allocations with a `TypeInfo` of
  kind `buffer`. Raw `malloc` remains for FFI only.
- Debug builds quarantine freed blocks (a FIFO of N blocks, not reused
  immediately) and poison the payload, so a use after free fails loudly.

## 5. Counting

- `retain`: `strong.fetch_add(1, relaxed)`.
- `release`: `old = strong.fetch_sub(1, release)`. If `old == 1`: acquire fence,
  then destroy.
- `destroy(obj)`: set `dying`, run `deinit` if `constructed`, run `dropFields`,
  unlink from the trace registry, then free the block if `weak == 0`. Otherwise
  the block stays as a zombie until the last weak reference goes.
- A `retain` on a `dying` object is a bug and traps in debug builds.
- `weak T` holds the weak count only. `upgrade` is a compare-and-swap loop that
  increments `strong` only if it is non-zero, and yields `T?`.
- Debug builds trap on count underflow and on overflow of the 32-bit count.

## 6. Construction and the allocation point

### 6.1 No shared constructor function

Today `Class(args)` calls a generated cover function that allocates inside it,
so every allocation of a class shares one site. In the new design the
construction is lowered **at the call expression**:

```
t = alloc(TypeInfo(Class), SITE)     // strong = 1, fields zeroed
defaults(t)                          // field initializers
r = init(t, args)                    // user init, may raise
if error: release(t) ; propagate     // `deinit` is skipped, `constructed` unset
set constructed
yield t
```

`SITE` is the constant for that expression. Fields start zeroed, so the drop glue
is safe on a partly built object.

The sequence does not have to be expanded inline. It can stay in the generated
constructor function, as long as that function takes the call expression's site
as a hidden parameter in trace builds. That is the mechanism of 6.3 applied
implicitly to every generated constructor. The allocation point is exact either
way, and generated code does not grow. The plan uses this form.

### 6.2 Which location counts

- The location of the construction expression as the user wrote it.
- Inside a macro or generated code, the location of the outermost expansion in
  user code, like `file!`/`line!`.
- Compiler-synthesized constructions (copy glue, defaults) take the location of
  the expression that required them.

### 6.3 `@memtrace(callsite)`

For functions whose allocations should be charged to the line that called them:
the stdlib containers (`Vector.push` growing its buffer), and user factories,
builders and pools.

```cxy
@memtrace(callsite)
func makeUser(): User => User()     // charged to the caller of makeUser
```

- The attribute is written in source. The hidden parameter and the site values
  are injected by the compiler. Users never write or see them.
- `@memtrace(callsite)` parses today: a bare name in an attribute's argument list
  is read as `callsite: true`. `@memtrace` is the home for later trace options
  (a `label`, an `ignore` for the leak report).
- In trace builds the function receives a hidden parameter `__site: ^Site`,
  placed first, before `this` (never last, because of variadic parameters).
  Generated `Class(args)` constructor functions get the same parameter
  implicitly. A marked function used as a value is an error, found by the
  reachability pass in all builds.
  Allocations in its body use it. A call from one marked function to another
  forwards the incoming site. An unmarked caller passes the site of its own call
  expression.
- A closure inside a marked function is a separate function and does not see the
  hidden site. Its allocations use their own sites.
- In non-trace builds the attribute generates nothing.
- The restrictions are enforced in **all** builds, so the two builds never
  disagree about what compiles. Not allowed on: virtual or overriding methods,
  `extern` or plugin-exported functions, functions whose address is taken
  (function values, closures), `init`, `deinit`, operator overloads and `@task`
  functions.

### 6.4 Shadow stack (level 2)

Native backtraces depend on inlining and need frame skipping and symbolization.
Level 2 replaces them:

- A per-coroutine (and per-thread) array of `Site *`. Each call pushes its call
  site before the call and pops after. There is no unwinding, because errors are
  results, so every path pops.
- The scheduler swaps the current shadow-stack pointer at a coroutine switch.
- At allocation the top frames are interned into a `stackId` stored in the trace
  header.

## 7. Ownership in the IR

### 7.1 Value categories

Every expression yields either a **place** (borrowed) or an **owned temporary**.
Lowering stores every owned temporary in a named slot before it is used. The IR
has these ownership operations:

```
alloc  retain  release  move  copy-glue  drop-glue
```

Aggregates (struct literals, tuples, optionals, unions, closures, call
arguments) take only explicit operands: `copy(place)`, `move(place)` or a
constant. One rule handles all of them: **a move operand kills its place.**
There is no nested case for a pass to miss.

### 7.2 Glue is derived from the type

`needsDrop(T)` is a computed property. Drop glue, copy glue and visit glue come
from the same per-type field list, for structs, tuples, arrays, optionals,
unions, results, closures and slices.

### 7.3 Drop flags and conditional moves

A variable that is moved on some paths and not others still needs a drop flag.
The flow analysis stays, but it only has to understand one operand shape.

### 7.4 Ownership verifier

A debug-build pass over the IR after drop elaboration: every owned temporary and
every owning variable must be consumed exactly once on every path (moved, stored,
returned or dropped). It reports at compile time what would otherwise be a double
free or a leak. It runs in CI over all tests.

### 7.5 Explicit ownership syntax

Three operations, three spellings:

| Source | Meaning |
|---|---|
| `&x` | borrow, no ownership change |
| `&&x` | move: take ownership, `x` is dead |
| `@copy x` | **new owner**, `x` is untouched |

The rule that decides when each is needed: **copying one owner into another is
implicit, and `@copy` is how a reference becomes an owner.** A reference carries
no ownership, so the compiler cannot tell that the user wants one, and asks.
Between owners the intent is clear.

```cxy
var a = Node(1)
var b = a            // implicit copy, b is Node
var c = @copy b      // allowed but not needed, c is Node
var k = &c           // a borrow, k is &Node
var l = @copy k      // a reference becomes an owner, l is Node
```

`@copy` is a single token, `@copy`, parsed as a prefix operator on the operand
that follows it (`@copy a.b.c` copies the whole member chain, anything else needs
parentheses). A function or variable named `copy` is unaffected, and `@copyright`
is still an attribute. It replaced the `__copy!` macro, which was removed.

**What `@copy x` does, derived from the type**

| `T` | Result |
|---|---|
| class (`T` or `&T`) | a new owner of the **same object**, a retain. Never a clone. |
| struct | copy glue: every owning field copied by this same rule, the rest by bits. A struct's `copy` overload customizes it and must return an independent owner. |
| optional, union, result, tuple, array, closure | per element, closures retain a heap environment |
| `weak T` | a new weak reference |
| trivial (primitives, enums, raw pointers, structs with no owning field) | bit copy, `@copy` is not needed |
| `@nocopy` type (mutex, socket) | compile error |

For `x: &T` with a class `T`, `y = @copy x` gives `y: T`, an owned handle, and
the object's strong count is incremented. For a struct `T` it builds a new value
in `y`, with each class field retained. A reference to an optional gives an
optional, and the retain happens only when it holds a value.

**Conversion rules**

| From | To | Rule |
|---|---|---|
| temporary `T` | `T` | moves implicitly |
| owned place `T` | `T` | implicit copy (a retain for a class). `@copy x` is allowed and redundant. `&&x` moves instead. |
| `return local` | | moves the local |
| `&T` | `T` | `@copy x`. Without it: error. |
| `&T` | `T` by `&&x` | error, cannot move out of a reference |
| `T` | `&T` | `&x`. Call arguments and `this` borrow implicitly. |
| field or element | `T` | implicit copy like any owned place, `@copy obj.f` if the user wants to spell it, or `&&obj.f` (leaves the field zeroed) |
| trivially copyable | same | implicit, no ownership involved |

Storing into a field or a container follows the same rows: an owned source copies
implicitly, a reference source needs `@copy`.

`@copy` of a value that is already an owner (`var c = @copy b`) is accepted. A
lint for the redundant form is possible later and is not planned.

**Diagnostics must teach.** For example:
```
error: cannot take `&String` as an owned `String`
  var s: String = r
                  ^ use `@copy r` to share it with another owner
```

**`@nocopy`** marks a move-only type. `@copy` on it is an error, and so is an
implicit copy of it, and both errors name the type. This is the one case where an
owner-to-owner copy is rejected: the type has exactly one owner at a time, so
the program must `&&` it or pass `&T`.

**Deep copies are `clone()`.** The standard library method that makes a new
object with the same contents (today `String.copy()` and similar) is renamed
`clone()`. `@copy` has one meaning, and a `copy()` method never competes with it.
The `copy` operator overload on structs keeps its name.

**Implicit copies are implemented by the memory pass.** Because copying one owner
into another is valid language, the compiler has to insert those copies somewhere.
That is the memory pass: it promotes an assignment from a place to a copy, adds a
copy for an owned call argument, and copies the payloads of literals. Nothing
requires moving this into sema. See 7.6 for whether the pass should be organised
differently.

**What it gives the tracer.** Every retain comes from a copy node with a source
location, user-written (`@copy`) or implicit (the operand's location), and every
transfer from an `&&`. The per-object history (level 2) can name the lines.

**Parameters.** A parameter that is not a reference is owned by the callee, so
there is nothing to declare (8.1). An owned place passes implicitly as a copy, a
reference needs `@copy r`, and `&&x` moves. A `&T` parameter is borrowed and
never needs any of them.

**Migration.** Nothing needs converting for owner-to-owner copies. What changed
for existing code is the removal of `__copy!` (use `@copy`) and the `clone()`
rename, which has a deprecation period for the old `copy()` names. A `&T` assigned
to a `T` is already a compile error, and its message should name `@copy`.

### 7.6 Organising the memory pass (proposal, not decided)

The detailed design, with the table of every generated variable and the
normalization rules, is in `memory-pass.md`. What follows is the summary.

The pass is not only about copies. It is `middle/memory/manage.c`, about 1150
lines, and the part that decides what gets dropped, copied and flagged. What a
read of it shows today:

- **The per-statement rules exist three times.** What an assignment, a move, a
  copy, a drop and a scope exit do to a variable's state is written in
  `computeIntraBlockGenKill`, again in `identifyDropFlagVariables`, and again in
  `elaborateDropsAndReassignments` (one 280-line function). They must agree, and
  nothing makes them.
- **Moves are found by a list of shapes.** `forEachOwningSlot` descends only the
  shapes it names (union, optional, struct and array literals, closure env, call
  arguments). A move in any other place is invisible to the first two copies of the
  rules. This is the class of bug behind the nested-move fixes (L1), and the
  ownership verifier (7.4) exists because of it.
- **Analysis and rewriting are mixed.** The elaboration pass tracks state while it
  inserts drops, strips `irMove` nodes, hoists moves of fields into temporaries,
  nulls moved members, injects copies for owned operands, and sets or clears drop
  flags. A reader cannot tell which line is a decision and which is a mutation.

The design of 7.1 already says the fix: aggregates take only explicit operands,
so that "a move operand kills its place" is one rule with no nested case. A
normalization pass before the memory pass would get there:

1. **Normalize** (new, before `runMemoryManagement`). Every move, and every
   implicit copy of an owned operand, becomes its own statement, in evaluation
   order, with the operand replaced by a temporary. Literals, call arguments and
   casts then contain no ownership operations.
2. **Analyse.** Because every ownership event is a statement, the three copies of
   the rules collapse into one small `switch (stmt->tag)` that gen/kill, flag
   selection and elaboration all call. `forEachOwningSlot`, `moveSlot` and
   `processPayloadItem` are deleted.
3. **Elaborate.** Only inserts drops and flags. It no longer rewrites operands.

What it costs: the IR gains temporaries (the C output changes, behaviour should
not), evaluation order has to be preserved when an operand is hoisted, and moves
in async code must still take effect before the call that can suspend. The IR
snapshots and the ASan suite are the regression tests, and the verifier checks the
result independently.

A smaller first step, if the pass is not restructured: write the event rules once
(one iterator over a statement's ownership events, built on the generic IR walk,
not a shape list) and call it from all three places. That removes the duplicated
rules but keeps the rewriting inside elaboration.

## 8. Retain and release as compiler operations

Calls to a runtime function for every count change hide them from the
optimizer. Instead:

- `retain` and `release` are IR operations. The C backend emits inline atomic
  operations. The LLVM backend emits `atomicrmw`. Only the slow path
  (`destroy`, trace hooks) calls into the runtime.
- Because they are explicit, a pass can remove a retain/release pair in one block
  and the retains made redundant by a borrow.

### 8.1 Calling convention

The convention follows from the parameter's type, so no attribute is needed:

- **A parameter that is not a reference is owned by the callee, always.** The
  callee drops it at exit unless it moves or stores it. The caller supplies an
  owner: a temporary moves in, an owned place is copied implicitly (or moved with
  `&&x`), and a reference needs `@copy r` (7.5).
- **A `&T` parameter is borrowed.** There is no count traffic for the call.
  Passing an owned value `x` to a `&T` parameter borrows it implicitly.
- **Returns are owned.**
- To avoid count traffic, an API that only reads a class declares the parameter as
  `&T`. The stdlib is reviewed for this in the plan.
- **No implicit guard.** The compiler does not retain on the caller's behalf. A
  borrow stays valid only while its owner does, and keeping that true is the
  programmer's job. See 8.2.

### 8.2 Dangling references

`&T` is a plain pointer to the owner's storage. It may be a parameter, a local,
a class field or a return type, and none of these carries a count. Nothing in
version 1 proves that the owner outlives the borrow. This costs nothing on the
hot path, and it is the programmer's responsibility.

A borrow dangles when its owner is released or its storage moves while the borrow
is still in use:

```cxy
class Cache {
    items: Vector[Item]
    func first(): &Item { return &items.[0] }
    func clear() { items.clear() }
}

func bad(c: &Cache) {
    var f = c.first()
    c.clear()            // owner of the item is gone
    println(f.name)      // dangling
}

func good(c: &Cache) {
    var f = @copy c.first()   // f is an owner, the count keeps the item alive
    c.clear()
    println(f.name)
}
```

```cxy
class View {
    src: &Buffer         // not owned: Buffer must outlive View
}
```

Rules of thumb, to be written into the language guide:

- A `&T` is valid until the owner is cleared, reassigned, moved or destroyed, or
  for a struct slot in a container until the container grows.
- To keep a value across such an operation, take an owner with `@copy` (7.5).
- A `&T` field or return value is a promise by the author. Use `weak T` (plan
  step 8) where the lifetime relation cannot be guaranteed.
- Never store a borrow of a local in an object that outlives the local.

What the toolchain does about it:

- **Release builds:** nothing. There is no retain, check or cost.
- **Debug builds:**
  - freed blocks are quarantined and poisoned (section 12);
  - loading a `&T` class field or a `&T` returned from a member checks that the
    header is live (not freed, not quarantined) and panics with the allocation
    site and the free site when it is not. A struct slot has no header, so only
    the poisoned container buffer catches it.
- **Trace builds:** `visit` glue reports `&T` fields as borrow edges, separate
    from ownership edges (10.3). A borrow edge whose target is freed or in
    quarantine is reported as dangling by `cxy mem dangling`, with the allocation
    site of the target and the site where it was freed. Borrow edges do not
    count toward `externalRefs`.

Deferred, not rejected: a compile-time loan check and guard retains for borrows
of non-local sources. The IR keeps `retain` and `release` explicit (section 8), so
adding them later does not change the model.

## 9. Containers and buffers

- `Vector`, `List`, `HashMap` and `String` allocate their storage with
  `mm_alloc_buffer`, `mm_realloc_buffer` and `mm_free_buffer`. These carry a
  site (hidden `@memtrace(callsite)` parameter in trace builds for the growing
  operations).
- A buffer is a node in the live registry, so a buffer with no owner is a
  detectable leak and its bytes count toward the owner's retained size.
- A container's `visit` reports the edge owner to buffer, and buffer to each
  element it holds. The buffer does not know how many elements are initialized,
  so the container enumerates them.

## 10. Tracing

### 10.1 Levels

Selected at compile time.

| Level | Adds | Cost |
|---|---|---|
| 0 | nothing (release) | none |
| 1 | trace header, `Site`, live registry, per-type and per-site counters, `visit` glue, named roots | small |
| 2 | shadow stack, interned stack ids, per-object history of the last 8 events | higher, for debugging |

### 10.2 Registry

- Each thread owns a doubly linked list of its live objects (through the trace
  header). Insertion and removal are O(1), with no global lock and no hash table.
- An object freed on a different thread unlinks from its owner's list. Each list
  head has a small lock. This is only paid in trace builds and only for
  cross-thread frees.
- Counters (allocations, frees, live count, live bytes) are kept per site, per
  type and per thread in thread-local arrays, and summed on demand. They run from
  the **first** allocation. There is no "started" gate and no fixed-size history.

### 10.3 Exact edges and roots

- `visit(obj, v)` is generated from the same field list as the drop glue. It
  calls `v->edge(from, to)` for each owned reference. Weak references are not
  edges.
- `externalRefs(x) = strong(x) - (number of incoming edges)`. An object with
  `externalRefs > 0` is held by something outside the heap: a stack, a global or
  a coroutine.
- The compiler registers global variables of an owning type in a `RootInfo`
  table (name, address, type). Visiting the global's storage with the type's
  value-visit glue attributes references to it by name, so a path can read
  `global cache -> HashMap -> buffer -> key`. References still unexplained are
  reported as `external` with a count.
- **Leaked cycles:** a group of objects with no incoming edge from outside the
  group and no external references is garbage.
- **Long-lived** is derived: retained by a global root. Optional scope labels can
  be added later as accounting names, but nothing needs tagging by hand.

### 10.4 Query snapshot

Commands run in a forked child (trace builds only), as in the current tracer.
For a consistent snapshot:
- The trace build has a cooperative safepoint at allocation and free. A query
  asks the program to pause and waits with a timeout. Then the registry locks are
  taken and the process forks.
- If a thread does not reach a safepoint in time (a long loop that never
  allocates), the query falls back to a racy snapshot and marks its results
  **approximate** (an `externalRefs < 0` is shown as such).

### 10.5 Commands

| Command | Answers |
|---|---|
| `summary` | live objects and bytes, totals since start |
| `types [n]` | live count and bytes per type |
| `sites [n]` | live and cumulative counts per allocation site |
| `list [n]` | recent live objects with site (and shadow stack at level 2) |
| `holders <ptr>` | who owns it |
| `path <ptr>` | shortest chain from a root |
| `contains <y> <x>` | is x owned, directly or not, by y |
| `retained [n]` | roots by exclusive and reachable bytes, with top sites |
| `roots` | named global roots with what they retain, and unexplained external counts |
| `cycles` | leaked cycles |
| `history <ptr>` | level 2: last retain and release sites of the object |
| `mark` / `diff` | as today, over birth ticks |

### 10.6 Exit report

After finalizers have run:
- Objects retained by named global roots are listed separately as expected.
- Any other object with external references is a leak candidate.
- Leaked cycles are leaks.
- Numbers are exact: sizes include headers and buffers.

### 10.7 What "accurate" means here

- The allocation point is the static site of the construction expression, not a
  frame found by walking the native stack.
- Counters never lose events and are never gated.
- Sizes include headers and container buffers. The summary reports payload and
  overhead separately.
- Graph results are exact when the program was paused, and are marked
  approximate otherwise.

## 11. Threads and coroutines

- Atomic counts in version 1. A reserved `shared` flag and the thread id in the
  trace header leave room for a biased mode (owner thread non-atomic, others
  atomic) without changing the layout.
- Coroutines on one thread share counts like any code on that thread.
- Shadow stacks are per coroutine, so an interleaving of coroutines never mixes
  frames.

## 12. Checks (debug builds)

- Count underflow and overflow, retain on a dying or freed object, double free.
- Freed blocks are quarantined and poisoned.
- Resurrection during `deinit` traps.
- The ownership verifier (section 7.4).
- Optional canaries around buffers.

## 13. Backends

- C and LLVM both lower the ownership operations of section 7. `retain` and
  `release` are inline atomic operations, `alloc` and `destroy` call the runtime.
- The runtime (allocator, destroy path, trace) is one library compiled once. For
  the LLVM backend it is embedded as bitcode, as described in `llvm.md`.

## 14. Test strategy

- The existing `tests/lang/mem` suites keep their expected behaviour. Snapshots
  change where the generated shape changes.
- New trace tests run a program with trace level 1 and 2 and assert on commands:
  exact site lines for `Class()` in user code, in a generic function and through
  an `@memtrace(callsite)` function; exact counts and bytes; `retained`, `path`, `cycles`
  on small known graphs.
- The ownership verifier runs on every lang test.
- A stress test with threads checks the counters, the registry and the snapshot
  fallback.

## 15. Path from the current implementation

| Today | New |
|---|---|
| `__smart_ptr_alloc/get/drop` with a destructor pointer | `mm_alloc` with `TypeInfo`, inline `retain`/`release` |
| Generated constructor cover function allocates | Construction lowered at the call expression |
| Allocation path and line from the IR node of the cover | `Site` of the construction expression |
| C backtrace with skipped frames | Shadow stack |
| Container `malloc`/`realloc`/`free` | Managed buffers |
| `__copy!` macro | `@copy` token (done) |
| Copies inserted by the memory pass | Unchanged: implicit owner-to-owner copies stay implicit |
| `copy()` method for deep copies | `clone()` |
| Live hash table under one mutex, 1024-event ring | Per-thread intrusive lists, full counters |
| Late dataflow pass finds moves in nested operands | Normalized operands plus the ownership verifier (drop flags stay) |

Phases, each testable by itself

1. **Header, `TypeInfo`, `Site`, allocator API.** The allocation point becomes
   exact at this step. The old functions are kept as thin wrappers until the end.
2. **Construction at the call expression**, removing the cover function, plus
   `@memtrace(callsite)`.
3. **Managed buffers** in the stdlib containers, with `@memtrace(callsite)` on
   the growing operations.
4. **`visit` glue, registry, `RootInfo`, query snapshot and the commands** of
   section 10.5.
5. **Ownership verifier** on the IR.
6. **Ownership syntax** (7.5): `@copy`, `@nocopy`, the conversion rules (an owner
   copies implicitly, a reference needs `@copy`), the `clone()` rename and the
   diagnostics that point to `@copy`. Done.
7. **Retain and release as IR operations**, pair elimination, and the stdlib
   review for `&T` parameters. Debug header check for `&T` loads and the
   `dangling` command (8.2).
8. **`weak`.**
9. **Level 2:** shadow stack, stack ids, per-object history.
10. Optional later: the biased reference-count mode.

## 16. Risks and open questions

- **Strictness of `@copy`** (decided: loose between owners). `var b = a` is a
  valid implicit copy. Requiring `@copy` there was considered and dropped,
  because class handles are everywhere and the intent is clear. `@copy` is
  required only for `&T` to `T`.
- **Field and element sources** (decided: implicit by default). `return this.field`
  and `x = obj.f` copy out of an object's storage, like any owned place. Writing
  `@copy` or `&&` is the user's choice, the compiler does not require it.
- **`clone()` rename** (accepted). It touches every caller of the old `copy()`
  method. In the stdlib `String.copy()` has one caller. Code outside the
  repository needs the deprecation period.
- **Borrowed references** (`&T`) can dangle. Version 1 leaves this to the
  programmer (8.2). Debug quarantine, the `&T` load check and the `dangling`
  command are the safety net. A loan check or guard retains can be added later.
- **Header layout change** touches everything that assumes the 16-byte `__mem`
  header. Phase 1 should go through a single accessor for it.
- **`@memtrace(callsite)` and the type system:** the hidden parameter must not
  leak into closures, interface tables or function values. The compile-error rule
  in 6.3 keeps it out of those places. User code can now trip that rule, so the
  error message must say why.
- **Safepoints** in trace builds cost a branch at every allocation and free. A
  thread that never allocates delays pausing, hence the timeout and the
  approximate marker.
- **Global variables of owning type declared in generics or plugins** need to be
  registered in the root table. Missing ones show up as `external`, never as
  wrong results.
- **Shadow stack cost** at level 2 is a push and pop per call. It is for
  debugging only.

## 17. Task list

What the design asks for, in order. Schedule and detail are in `memory-manager-plan.md`.

- [x] Object header, `TypeInfo`, `Site` (3)
- [x] Allocator API and saturating counts (4, 5)
- [x] Construction at the call expression, `@memtrace(callsite)` (6)
- [x] Value categories, glue from the type, drop flags (7.1 to 7.3)
- [x] Ownership verifier (7.4)
- [x] `@copy`, `@nocopy`, `clone()`, `&T` to `T` rule (7.5)
- [x] Containers on managed buffers (9)
- [x] Registry, exact edges and roots, query snapshot, commands, exit report (10)
- [x] Reorganise the memory pass (7.6, tracked in `memory-pass.md`, all of its tasks are done)
- [x] Retain and release as compiler operations, pair elimination (8): a class-handle `copy` and `drop` are the operations, `pairs.c` removes a pair when the copy is only an alias
- [x] Dangling reference checks in debug builds and `dangling` command (8.2, 12): `__mm_ref` on a `&T` loaded from a field or returned by a call, `&T` fields are borrow edges, `cxy utils mem dangling`
- [x] `weak T` (5, 8): `Weak[T]` and `@weak T`, weak count and zombie blocks in the runtime, zombies in the trace (`zombies` command, summary, exit report)
- [ ] Shadow stack, level 2, coroutine switch (6.4, 10.1, 11)
- [ ] LLVM backend emits the same operations (13)
