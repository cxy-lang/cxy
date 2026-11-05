# Cxy memory migration guide (running document)

Audience: AI agents and developers migrating a Cxy codebase (outside the compiler
repository) to the new ownership and memory forms. Design background:
`docs/stage/memory-manager.md`, schedule: `docs/stage/memory-manager-plan.md`.

This file is updated whenever a change lands. Each entry has a **Status**.

## How to use this document

1. Only apply entries whose status is **Landed** and whose **Since** commit your
   compiler contains (the project version is a constant `0.1.0`, so commits are
   the only reliable marker). `Planned` entries describe the future form, do not
   apply them yet.
2. Work one entry at a time. Build and run the tests after each one.
3. Never change behavior to silence a warning. If a fix is not obvious, leave a
   `// TODO(mem-migration): <entry id>` comment and report it.
4. Prefer the borrow form (`&T` parameters) over adding copies. A copy is a
   count change, a borrow is free.

Status legend: `Landed` (in the compiler), `Planned` (agreed design, not built),
`Proposed` (not agreed).

## Ownership vocabulary

| Spelling | Meaning |
|---|---|
| `&x` | borrow, no ownership change, no count change |
| `&&x` | move: the callee or target takes ownership, `x` is dead afterwards |
| `@copy x` | new owner, `x` stays valid (a retain for a class) |
| `x.clone()` | new object with the same contents (deep copy), an explicit method |

Classes are heap allocated and reference counted. Structs are values. A class
variable `T` is an owning handle, `&T` is a borrowed handle.

---

## Landed

### L1. Nested aggregates move correctly

Status: Landed. Since: `0fd2ca3`. Action: none, awareness only.

A move inside a struct, tuple or array literal now kills the source variable:

```cxy
var s = "John".S
var o = Outer{ inner: Inner{ name: &&s } }   // s is moved, not dropped twice
```

Variadic forwarding `f(...&&args)` and `launch { }` environments follow the same
rules. If you worked around a double drop with an extra copy, you can remove the
workaround.

### L2. `&T?` and references to optionals

Status: Landed. Since: `0fd2ca3`. Action: check spelling.

- `&T?` parses as an **optional reference**: `(&T)?`.
- A **reference to an optional** is spelled `&(T?)`, and with const `&(const T?)`.
- A field of type `T?` can be borrowed with `&obj.field`, and is truthy-tested
  through the reference without a copy.

```cxy
func isSet(it: &(const Resource?)): i32 {
    if (it) { return 1 }
    return 0
}
```

### L3. Empty slice literal

Status: Landed. Since: `0fd2ca3`. Action: none.

`[]` is assignable to any slice type, for example `say([])` where the parameter
is `[T]`. A generic `say[T](x: [T])` still cannot infer `T` from `[]`.

### L4. `@copy` expression, `__copy!` removed

Status: Landed. Since: `d87d0a9` (token and parser `c9bb056`). Action: replace
every `__copy!(x)` with `@copy x`.

`@copy` is one token (`@copyright` is still an attribute, and a variable named
`copy` is unaffected). It takes the whole prefix operand: `@copy a.b.c`,
`@copy *p`. Anything more complex needs parentheses.

| Before | After |
|---|---|
| `__copy!(x)` | `@copy x` |
| `__copy!(*value)` | `@copy *value` |
| `m.[__copy!(k)] = v` | `m.[@copy k] = v` |
| `(__copy!(this), false)` | `(@copy this, false)` |

The operand must be a place (variable, field, element) or a reference. `@copy
f()` or `@copy 42` is an error: a call result is already an owner.

Detection: `grep -rn "__copy!" --include=*.cxy`. The old macro now fails with
`unsupported/unknown macro __copy`. Implicit copies are not yet reported (M1).

### L5. `String.clone()`

Status: Landed. Since: `e788602`. Action: replace `s.copy()` with `s.clone()`.

`String.copy()` still works but is marked `deprecated`. `clone()` builds a new
object with the same contents. `@copy s` shares the object instead, see the
vocabulary table. `Vector.clone()` already existed. The struct `copy` operator
overload (``func `copy`()``) is unchanged.

### L6. Ownership warnings (`--warnings=Ownership`)

Status: Landed. Since: `ff0e01c`. Action: build with the warnings on and fix
every finding.

The compiler checks the drops it generated against what your code does. The
checks are off by default. Turn them on with `--warnings=Ownership` (or
`--warnings-all`), and make them fail the build with
`--warning-as-error=Ownership`. The warning name is printed in the header,
`warning[UseAfterMove]:`, and is what you pass to `--warnings` to disable one
(`--warnings='Ownership|~DanglingBorrow'`).

| Warning | Reported when | Usual fix |
|---|---|---|
| `UseAfterMove` | a variable is read or moved after `&&x` | `@copy x` instead of `&&x`, or stop using it |
| `DoubleDrop` | a value is dropped after it was moved or dropped | remove the extra move or drop |
| `OwnerLeak` | an owner is still held at function exit | move it, return it, or drop it |
| `OverwrittenOwner` | an assignment replaces a value that is still owned | |
| `DropFlagMismatch` | a conditional drop flag disagrees with ownership | report as a compiler bug |
| `RedeclaredOwner` | a loop redeclares a variable that is still owned | |
| `UnclearedMove` | a field was moved out and not cleared before the parent drops | report as a compiler bug |
| `ReturnedBorrow` | `return &local`: the local dies at function exit | return the value, or `@copy` it |
| `DanglingBorrow` | `var r = &x`, then `r` is read after `x` was moved or dropped | read `r` before the move, or `@copy x` |
| `MoveOfReference` | `&&r` where `r` is a `&T`: a reference is not an owner | write `@copy r` to take an owner, or pass `r` as it is |

`DropFlagMismatch`, `UnclearedMove` and `DoubleDrop` on code that has no
mistake mean the compiler dropped something wrongly. Report them, do not change
the code to hide them.

Only local variables and parameters are tracked. A borrow returned by a call
(for example `c.first()`) is not followed, and a borrow stored in a field or
global is not checked. Compilers before `ff0e01c` crash on a combined list such
as `--warnings='A|B'`.

### L7. `@memtrace(callsite)` for factories

Status: Landed. Since: `f12cdfc`. Action: optional.

Mark functions whose allocations should be reported at the line that called
them, such as factories, builders and pools. It does nothing in normal builds.

```cxy
@memtrace(callsite)
func makeUser(): User => User()
```

Not allowed on virtual or overriding methods, `extern` or plugin-exported
functions, functions used as values or closures, `init`, `deinit`, operator
overloads and `@task` functions. The restriction applies in all builds, so
builds without `--memory-trace` also reject it.

### L8. `@nocopy` types

Status: Landed. Since: see the running log. Action: optional, mark your own
move-only types.

A type declared `@nocopy` has exactly one owner at a time. Copying it is a
compile error, whether you write `@copy x` or the compiler would copy it
implicitly (`var b = a`, passing `a` to an owned parameter, storing it in a
literal). A struct or tuple that holds a `@nocopy` value is move-only too.

```cxy
@nocopy
class Handle { fd: i32 ... }

var b = &&a            // move, a is dead
peek(a)                // `peek(h: &Handle)` borrows, no copy
var c = a              // error: `Handle` cannot be copied
```

`@nocopy` is inherited: every class derived from a `@nocopy` class is move-only,
and a derived class cannot opt back out. Copying a `Child` as a `Base` is an error
too. (Compilers before the commit in the running log did not inherit it.)

Fix an error by moving (`&&x`, when `x` is not used afterwards) or by changing the
parameter to `&T`. Do not add `@copy`: it is rejected too, including `@copy r` of a
reference `r: &T`.

The stdlib marks: `Mutex`, `ConditionVariable`, `Semaphore`, `WaitGroup`,
`CoWaitGroup`, `Channel[T]` and `SharedChannel[T]` (coroutines), `net.Socket`
(so every socket class derived from it) and `os.Process`. `Socket` was converted
in the same change: `Request.submit` and `BufferedSocketOutputStream` take
`&Socket` and only borrow it, and the redis `CachedClient` passes `&&sock` to its
base constructor. If you hold a `Socket` in your own class and pass it to an owned
parameter, expect the same error. Change the parameter to `&Socket` if the callee
only uses it, or move it with `&&sock` if the callee keeps it.

---

## Planned

Order follows the plan phases. Do not apply before the entry says Landed.

### M1. `@copy` turns a reference into an owner

Status: Landed. `@copy` is L4, and `&T` to `T` without it is an error that says
to write `@copy` (commit `5f48603`). Action: fix the errors you get.

**Copying one owner into another stays implicit.** `@copy` exists so a reference
can become an owner, because the compiler cannot tell that is what you want.

```cxy
var a = Node(1)
var b = a            // implicit copy, b is Node. Valid, no change needed.
var c = @copy b      // allowed but not needed, c is Node
var k = &c           // a borrow, k is &Node
var l = @copy k      // a reference becomes an owner, l is Node
```

| Source type | Target | Spelling |
|---|---|---|
| owned `T` | `T` | implicit, or `@copy x` (not needed), or `&&x` to move |
| `&T` | `T` | `@copy r`. Passing or assigning a `&T` to a `T` without it is an error. |
| field or element `T` | `T` | implicit copy, `@copy obj.f` optional, `&&obj.f` moves it out |
| `&T` | `&T` | `&x` or just the reference, no copy |
| temporary `T` | `T` | moves |
| `return local` | | moves the local |
| trivial values | | never need `@copy` |

Decision rule:

- Is the source a reference (`&T`) and the target an owner? Write `@copy r`.
- Is `x` an owner you will not use again? `&&x` avoids a count change.
- Does the callee only read the value? Change its parameter to `&T` and the
  call stays as it is.
- `@copy` on a class shares the **same object**. It is never a clone. For an
  independent object call `x.clone()` (M2).
- `@copy` binds like a prefix operator. `@copy a.b.c` copies the whole chain,
  anything more complex needs parentheses: `@copy (a ?? b)`.
- Trivial values (numbers, enums, raw pointers, structs with no owning field)
  never need `@copy`.
- A type marked `@nocopy` (mutex, socket) cannot be copied: restructure so one
  owner moves it, or pass `&T`.

Detection: the compiler reports `&T` to `T` as an error in a variable initializer, an
assignment, a call or constructor argument, a field of a literal or a field store,
and a return. Each error is followed by the note ``a reference is not an owner, write
`@copy x` to take an owner of the referenced value``. There is no
warning for implicit copies between owners, because they are valid. Copying a
field or element out of an object (`return this.field`) is an implicit copy by
default too. Write `@copy` or `&&` there only if you want to say which one you
mean.

### M2. `copy()` method renamed `clone()`

Status: `String` is Landed (L5). Other types and your own methods: Planned.

Methods that build a new object with the same contents are renamed.

| Before | After |
|---|---|
| `s.copy()` (`String`) | `s.clone()` |
| your own `func copy(): This` | `func clone(): This` |

Not renamed: the `copy` **operator overload** on structs (``func `copy`(...)``).
It keeps its name and its meaning, it customizes `@copy` for that struct and must
return an independent owner.

Detection: `grep -rn "\.copy()" --include=*.cxy` and `func copy(`.

### M3. Parameters: owned unless a reference

Status: Planned (plan phase 6, semantics already true today).

- A parameter that is not a reference is **owned by the callee, always**. The
  callee drops it at exit unless it moves or stores it. There is no `@own`.
- A `&T` parameter is borrowed. No count traffic.
- Returns are owned.

Migration: for every function that only reads a class parameter, change the
parameter type from `T` to `&T`. That removes a copy at every call site and is
the main performance gain of the migration. Keep `T` when the function stores the
value, returns it, or sends it to another thread or task.

```cxy
func render(user: &User): String { ... }      // reads only
func register(user: User) { users.push(&&user) } // stores, owns it
```

### M4. Borrowed references can dangle

Status: Landed (design 8.2). Release builds are unchanged, debug and trace builds check.

`&T` carries no count. The compiler does not prove the owner outlives the
borrow. When migrating:

- Do not hold a `&T` across an operation that clears, reassigns, moves or
  destroys its owner (`cache.clear()`, `vec.push()` for struct elements).
- To keep a value across such an operation take an owner: `var f = @copy c.first()`.
- A `&T` field means "the target outlives me". Use `weak T` (M7) for back
  pointers when that cannot be guaranteed.
- Never store a borrow of a local in an object that outlives the local.

Debug builds panic when a `&T` loaded from a field, or returned by a call, points
at a destroyed object (`borrowed reference used after its owner was destroyed`).
Trace builds list the borrows that dangle with `cxy utils mem dangling`, with the
allocation and free sites of each target. Treat each report as a real bug, not as
noise. A temporary the `!T` protocol or a statement drops now dies with its
statement, so keep what you read from it: `var s = f(); var v = s.view()`, not
`var v = f().view()`.


### M7. `Weak[T]` and `@weak T`

Status: Landed (plan phase 8).

Use for parent pointers and observer lists where a strong handle would create a
cycle. `T` must be a class. `@weak T` is another spelling of `Weak[T]`.

```cxy
class Child {
    parent: @weak Parent
    func `init`(p: &Parent) { this.parent = Weak[Parent](p) }
}

var up = child.parent.upgrade()     // Parent?, empty once the last owner is gone
var other = @copy child.parent      // a second weak reference
```

A weak reference keeps the block of the object, not the object: when the last
owner goes the object is destroyed and its fields are dropped, the memory is freed
with the last weak reference. `upgrade()` gives a new owner or nothing. It works
with `--without-builtins`. Trace builds list the blocks only weak references keep
with `cxy utils mem zombies`, and the exit report shows the ones never released.

---

## Verification checklist after each entry

- The project builds with `--warnings=Ownership` and no findings (L6).
- The project builds with no new warnings of the migration category.
- All tests pass, including any that run with memory tracing.
- For trace builds: no new leaks in the exit report, and no dangling reports.
- No `@copy` was added to silence an error without checking use-after.
- No `TODO(mem-migration)` remains unreported.

## Common mistakes

- Using `@copy x` for a deep copy. It shares the object. Use `clone()`.
- Using `&&x` and then reading `x`. After a move the variable is dead.
- Adding `@copy` to every call argument. If the parameter is `&T` no copy is
  needed, and if the callee only reads, change the parameter instead.
- Replacing `T` parameters with `&T` on functions that store or return the value.
- Returning `&T` from a function that built the value locally. Return `T`.

## Running log

| Date | Entry | Change |
|---|---|---|
| 2026-11-03 | L1, L2, L3 | Landed in commit `0fd2ca3`. |
| 2026-11-03 | M1 to M7 | Written from the design. Nothing implemented. |
| 2026-11-04 | L4 | `@copy` token and expression, `__copy!` removed. Commits `c9bb056`, `d87d0a9`. |
| 2026-11-04 | L5 | `String.clone()`, `String.copy()` deprecated. Commit `e788602`. |
| 2026-11-04 | L6 | Ownership warnings and `--warning-as-error`. Commit `ff0e01c`. |
| 2026-11-04 | M1 | Corrected: owner-to-owner copies stay implicit, `@copy` is for references. |
| 2026-11-04 | L8 | `@nocopy` on types, copies rejected after the memory pass. M5 removed. Commits `1f9c11b`, `548da47` (references), then inheritance. |
| 2026-11-04 | L7 | `@memtrace(callsite)` moved from Planned (M6) to Landed. Commit `f12cdfc`. |

Add a row for each change and move an entry from Planned to Landed with the
commit that contains it. M6 was removed when it became L7, so the numbering of
Planned entries has a gap on purpose: ids are never reused.

## Task list

Mirrors the entries above, in the order they landed or will land.

- [x] L1: nested aggregates move correctly
- [x] L2: `&T?` and references to optionals
- [x] L3: empty slice literal
- [x] L4: `@copy` expression, `__copy!` removed
- [x] L5: `String.clone()`
- [x] L6: ownership warnings
- [x] L7: `@memtrace(callsite)`
- [x] L8: `@nocopy` types
- [ ] M2: rename `copy()` to `clone()` on the other types
- [ ] M3: parameters are owned unless a reference, review stdlib for `&T`
- [x] M1: `&T` to `T` needs `@copy`, owner to owner stays implicit
- [x] M4: dangling reference checks and the `dangling` command
- [x] M7: `Weak[T]` and `@weak T`
