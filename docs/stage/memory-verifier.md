# Memory verifier (plan phase 5)

Checks the IR after `runMemoryManagement` against the language's memory rules
(`memory-manager.md` 7, 8). It is a **language-rule checker**, not a check of
`manage.c`'s own bookkeeping. It shares no helper with `manage.c`.

## 1. Rules

Enforced by: **V** = IR verifier (this phase), **S** = sema (phase 6),
**R** = runtime / tracer, **P** = programmer (documented only).
Severity: **E** = definite on every path, **W** = possible on some path.

### Ownership

| ID | Rule | By | Sev |
|---|---|---|---|
| O1 | Every owner (local, owning param, temporary) is consumed exactly once on every path: moved, stored, returned or dropped. Reaching exit still owned is a leak. | V | E/W |
| O2 | A moved place is dead until reassigned. Any read, borrow, move, copy or drop of it is a use-after-move. | V | E/W |
| O3 | A drop needs an owner. Dropping a dead, uninitialized or borrowed place is a double free. | V | E/W |
| O4 | A move operand kills its place at any nesting depth: field, tuple, optional, union, closure env, call argument, variadic, struct or array literal, cast. | V | E |
| O5 | Moving out of a field or element leaves that field zeroed. Dropping the parent afterwards drops only the remaining fields. | V | E |
| O6 | Cannot move out of a reference: `&&r` with `r: &T` is an error. | S, V | E |
| O7 | Assigning to an owning place drops the old value first, unless the place is dead or uninitialized. | V | E |
| O8 | Drop flags are exact: on every path `flag == owned`. `drop-if` runs only where the flag is set. | V | E |
| O9 | Returns are owned. A returned local is moved, not dropped. `return ref` (a `&T` returned as `T`) needs `@copy`. `return field` is an implicit copy by default (design 16). | V, S | E |
| O10 | An owned temporary is consumed by the end of its statement. A discarded call result is dropped at once. | V | E |
| O11 | A non-reference parameter is owned by the callee and dropped at exit unless moved or stored. A `&T` parameter is never dropped. | V | E |
| O12 | A captured owner is moved into the closure env. After capture it is dead in the creating scope. A detached `launch` owns its env. | V | E |
| O13 | In a loop, an owner declared in the body is dropped each iteration. An outer owner moved inside the body must be reassigned before the next iteration. | V | E/W |
| O14 | Every early exit (`return`, `break`, `continue`, error return from `!`) drops all owners of the scopes it leaves, once. | V | E |
| O15 | Optionals, unions and results drop by active variant. A moved optional is cleared, so it drops nothing. | V | E |
| O16 | A global is a place. It cannot be moved out of without a replacement. | S, V | E |

### Borrows

| ID | Rule | By | Sev |
|---|---|---|---|
| B1 | A borrow (`&T`) is never retained or dropped by the borrowing site. | V | E |
| B2 | `&T` to `T` needs `@copy`. Without it, error. Already reported by sema today. | S | E |
| B3 | A borrow of a local or owning parameter never escapes: not returned, not stored in a global, not stored in an object that outlives the local. | V | E/W |
| B4 | A borrow is not used after its owner is moved, dropped or reassigned in the same function. | V | E/W |
| B5 | A borrow of a container element is not used after a mutating call on the container. | P, R | none |
| B6 | A borrow field or return is the author's promise that the owner outlives it. | P, R | none |

B5 and B6 are the programmer's responsibility (8.2). R means the debug header
check and `cxy mem dangling` catch them at runtime.

### Copies (checks go live with phase 6)

| ID | Rule | By | Sev |
|---|---|---|---|
| C1 | Copying one owner into another is implicit and valid: variable init, assignment, field or container store, owned argument, literal elements. `@copy` is required only for `&T` to `T` (B2). | S | none |
| C2 | `@copy` or an implicit copy of a `@nocopy` type is an error. A `@nocopy` type is moved only. | S | E |
| C3 | `@copy` of an owner or a trivial value is accepted and redundant. No lint is planned. | S | none |
| C4 | (dropped) The memory pass inserting implicit owner-to-owner copies is correct, so there is no rule about where a copy comes from. | | |
| C5 | `@copy x` leaves `x` owned. | V | E |

### Types and glue

| ID | Rule | By | Sev |
|---|---|---|---|
| G1 | `needsDrop(T)` is derived from the fields. A struct holding an owner is itself an owner and must be tracked. | V | E |
| G2 | A tracked variable is never accessed after its scope exit. | V | E |

### Warning names

Each rule the verifier reports is a compiler warning (`CXY_COMPILER_WARNINGS` in
`core/log.h`). The name shows in the diagnostic header, `warning[DanglingBorrow]:`,
and is what `--warnings` and `--warning-as-error` accept.

| Rule | Warning | Meaning |
|---|---|---|
| O1 | `OwnerLeak` | owner still held at function exit |
| O2 | `UseAfterMove` | read or move after a move or drop |
| O3 | `DoubleDrop` | drop of a value already moved or dropped |
| O5 | `UnclearedMove` | field moved out and not cleared before the parent is dropped |
| O7 | `OverwrittenOwner` | assignment over an owned value |
| O8 | `DropFlagMismatch` | drop flag out of sync with ownership |
| O10 | `TemporaryOwnership` | a temporary the compiler made is not consumed or dropped exactly once: an in flight one is handed to one consumer, a statement one is only read and dropped once after its last read |
| O13 | `RedeclaredOwner` | redeclared in a loop while still owned |
| O16 | `MovedGlobal` | a global moved out by a function and not assigned again on every path to its return |
| B3 | `ReturnedBorrow` | borrow of a local is returned |
| B4 | `DanglingBorrow` | borrow read after its owner was moved or dropped |
| O6 | `MoveOfReference` | a `&&` the user wrote on a reference |

`Ownership` is a group name for all of them.

## 2. What this phase delivers

- **V1 (done):** O1, O2, O3, O7, O8, O13 on named locals and parameters.
  Temporaries and ephemerals are not tracked, so O10 is not checked. A move of a
  value that holds nothing (an empty optional) is legal, so only a move or drop
  of a value that was already moved or dropped is reported.
- **V2 (done):** O5 (any write to a field of the variable clears every pending
  moved field), B3 (a borrow returned directly), B4 (a reference variable bound to
  one `&x`). B1 holds by construction, because reference variables are not
  tracked as owners. O12 needs no extra check: a captured owner is an ordinary
  move, so O2 and O3 cover it.
- **V3 (sema checks, mostly done):** C2 (`@nocopy`) and B2 (`&T` to `T`) are
  enforced, and O6 by the verifier (`UserWritten`). O16 is not. C5 (`@copy x` leaves `x` owned) is checked by the
  general move and drop rules.

## 3. Design

### 3.1 Independence

- Moves, copies, drops and uses come from a **generic walk** of every statement
  (`ir/visitor.c`), in evaluation order. There is no shape list.
- Any `irMove` at any depth is an event. A new IR shape that hides a move from
  `manage.c` is still seen here, and shows up as O3 or O2.

### 3.2 Events

| Event | Source |
|---|---|
| `def(place)` | assignment, `irVarDecl` with init, parameter at entry |
| `move(place)` | `irMove` anywhere in the statement |
| `copy(place)` | `irCopy`, a class retain |
| `borrow(place)` | `irAddrOf`, a `&T` argument |
| `use(place)` | any other read of a variable or member |
| `drop(var, flag?)` | `irDrop`, flag from `__drop_flags.x` |
| `flag(var, 0/1)` | assignment to `__drop_flags.x` |
| `scopeExit(vars)` | `irScopeExit` (still present before finalize) |

### 3.3 Places and state

- A place is a root variable plus a field path. Paths are tracked to depth 3.
  Anything deeper is folded into its ancestor.
- Each place has a 4-bit mask over **{uninit, owned, moved, dropped}**. Each
  tracked variable also has a flag bit, so the state is a set of
  (state, flag) pairs. The join is a union, so correlation survives branches.
- E means the mask holds only the bad state. W means the mask holds the bad state
  and a good one.
- A parent drop with a moved child checks that the child's zeroing is visible (O5).

### 3.4 Transfer

| Event | Check | Effect |
|---|---|---|
| `def` | O7: old state must be dropped, moved or uninit, or a drop was emitted | owned |
| `move` | O2: must not be moved or dropped. Moving an empty value is legal. | moved |
| `copy`, `use`, `borrow` | O2: must not be moved or dropped | none |
| `drop` | O3: must not be moved or dropped already. O5: no moved field pending. | dropped |
| `drop-if` | O8: must be owned wherever flag is set | dropped |
| `flag = v` | none | sets the flag bit of every pair |
| exit | O1: nothing owned | none |

Skipped variables: references, `this`, `flgReturned` (checked as O9 instead).

### 3.5 Diagnostics

- Format: the warning name in the header, the function, the offending location,
  and a note at the origin event (the move, drop or declaration).
- The origin location is kept per place at the join and is the first one found.
- Example:
  ```
  warning[UseAfterMove]: `n` is used after it was moved or dropped (in useAfterMove)
    in hello.cxy:27:13
  note: moved here
    in hello.cxy:26:13
  ```

### 3.6 Driver

- There is no verifier flag. The pass runs only when one of the nine warnings is
  enabled, so `--warnings=Ownership` (or `--warnings-all`) turns it on. They are
  off by default. Debug compiler builds can enable them once the suites pass clean.
- `--warning-as-error=Name|Name` (or `All`) reports those warnings as errors and
  enables them. In CI: `--warning-as-error=Ownership`.
- The pass is `middle/memory/verify.c`. `driver/stages.c` takes the snapshot before
  `runMemoryManagement` and verifies right after it. It never changes the IR.
- Findings go through `logWarningWithId`, so they honor both options.

## 4. Tests

- **Unit tests (C++):** `tests/lang/middle/test_ownership_verify.cpp` builds IR by
  hand with one defect per rule, expecting exactly one report with the right rule.
  A correct sibling for each case expects none.
- **Regression shapes** (must fail if the verifier ignores them):
  - a move nested in a call inside a literal;
  - a move wrapped by an optional (the `&&s` in `Inner{ name: &&s }` case);
  - a moved variadic forwarded with a reference sibling.
- **Mutation check (done once by hand):** with the pass blind to moves, a real
  compile reported `DoubleDrop` across the suites. The hook was removed because
  the unit tests cover the same shapes without test code in the pass.
- **Whole matrix:** `tests/lang`, `tests/trace/programs` and `examples` compile
  with `--warnings=Ownership --last-stage=Codegen`. Each finding is a compiler bug
  or a verifier bug, fixed or listed here with a reason. Today there are none.
- **Warning lists:** `tests/unit/test_warnings.cpp` covers `--warnings` parsing.

## 5. Exit

- V1 and V2 pass on the whole matrix with zero unexplained findings (met).
- The warnings are enabled as errors in debug builds in CI.

## 6. Task list

- [x] V1: O1, O2, O3, O7, O8, O13 on named locals and parameters
- [x] V2: O5, B3, B4
- [x] Unit tests with one defect per rule, and the three regression shapes
- [x] Findings are compiler warnings (`Ownership`), `--warning-as-error`
- [x] C2 (`@nocopy`) and B2 (`&T` to `T`) enforced in sema
- [x] C5 checked by the general move and drop rules
- [x] Rules about user syntax use `:user-written` (`memory-pass.md`, I3d)
- [x] O10: temporaries are checked by their class, no flow needed (`memory-pass.md`, S6)
- [x] O6: error on `&&r` with `r: &T`, by the `UserWritten` bit
- [x] O16 (`MovedGlobal`): a global moved out and not assigned again on every path to the function's return, a forward flow with one bit per moved global
- [x] `--warning-as-error=Ownership` on in debug builds in CI: every stdlib `ctest` case in a Debug build, the `lang/mem` `ir` and `codegen` suites, and the new `.github/workflows/test.yml`
