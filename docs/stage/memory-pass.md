# Memory pass redesign

Scope: `middle/memory/manage.c` and every place that creates a variable for it.
Goal: make memory management **intuitive and accurate** by making ownership
visible in the IR before the pass runs, and by splitting the pass into small
steps that each do one thing. Copies are one part of this, not the point.

Status: design for review. Nothing here is implemented.

## 1. What is wrong today

Read from the code and checked by running programs.

**1.1 The pass is one big tangle.** `manage.c` is about 1150 lines.
`elaborateDropsAndReassignments` alone is 282 lines and does seven jobs at once:
tracks state, inserts drops, inserts drop-flag writes, strips `irMove` nodes, hoists
moves of fields into temporaries, nulls moved members, injects copies. The rules
for what an assignment, a move, a copy, a drop and a scope exit do to a variable
are written three times (`computeIntraBlockGenKill`, `identifyDropFlagVariables`,
`elaborateDropsAndReassignments`). Nothing keeps the three in step.

**1.2 Moves are found by a list of shapes.** `forEachOwningSlot` descends only the
shapes it names. A move anywhere else is invisible to two of the three copies of
the rules. This is the class of bug behind the nested-move fixes.

**1.3 `Temporary` has no single meaning.** Four layers create temporaries (sema,
simplify, lowering, the memory pass), about 25 creation sites, and most of them
set the same flag, `Temporary`. The memory pass reads that flag as "an owned value
in flight, whoever consumes it takes ownership, never drop it". That is only true
for some of the sites. For the others the consumer does not take ownership, and
the value leaks.

Proof, with `class Node` and `func make(): Node`:

```cxy
func peek(n: &Node): i32 => n.id
peek(make())            // the temporary is never dropped
var r = &make()         // same, and r now borrows a temporary
make().id               // dropped correctly (the temporary is a plain local)
```

A trace build reports `[LEAK]` for the first two, one `Node` each. The generated C
shows `_r5 = make();` with no drop, while `_m6 = make();` is dropped at exit. The
difference is only that `_r` is created by simplify with `Temporary` and `_m` with
no flag. Any `f(g())` where `f` takes a `&T` leaks `g()`'s result today.

**1.4 The verifier cannot see it.** The ownership verifier tracks only named
locals and parameters. It skips temporaries, so a leaked temporary is not a
finding. The rule that would catch it (O10, "an owned temporary is consumed by the
end of its statement") is not implemented, because there is no way to tell from
the IR which temporaries are owned.

## 2. Principles

1. **A temporary exists only if it has a job in the next step.** Its job is one of
   two: the value must be evaluated once and in order, or an ownership event needs
   a name the analysis can hold on to. If neither is true, use the expression.
2. **Every temporary of an owning type belongs to exactly one class** (section 4),
   and the class decides its flag, its name, who consumes it and when it dies.
3. **A temporary that owns nothing carries no ownership flag.** A bool, an integer
   or a plain struct slot is not part of memory management.
4. **Every temporary is assigned once, before its use.** A consumed temporary has
   exactly one consumer.
5. **After normalization, ownership events are statements.** Operands of calls,
   literals and casts contain no moves and no copies, only variables, constants and
   borrows. A pass that handles statements cannot miss a nested case.
6. **No function over about 50 lines, and none that both decides and mutates.**
   Analysis functions read the IR. Elaboration functions write it.
7. **One layer decides ownership.** Sema, simplify and lowering create a temporary
   when they need a slot or an evaluation order, and say nothing about who owns it.
   `normalize` classifies it from its use, in one place.

## 3. Every generated variable today

Flags: `T` Temporary, `E` Ephemeral, `R` Returned, `RM` ReferenceMembers (the type
needs a drop). "Pass" is what `manage.c` does with the variable.

| Name | Created by | Flags | Assigned | Pass |
|---|---|---|---|---|
| user locals, parameters | lowering | as declared, `RM` if owning | initializer | tracked, dropped at scope exit and on reassignment |
| `__ret` | lowering (`funcRetVar`) | `R` | the return value | not tracked, ownership passes to the caller |
| `__objN` (init cover) | sema | `T` (some), `Reference` | the allocated object | `T`: not tracked (consumed by the cover's return) |
| `_rv` | sema (`transformRValueToLValue`) | `T` | a call result used as a place | not tracked |
| `_r` | simplify (`&expr` of an rvalue) | `T` | the rvalue | not tracked. **Leaks** (1.3) |
| `_m` | simplify (member access on an rvalue) | none | the rvalue | tracked, dropped at scope exit |
| `_c` | simplify (call on an rvalue callee) | none | the callee value | tracked |
| `_idx` | simplify (index expression) | `T` | the index value | not tracked, owns nothing |
| `_ga`, `_gi`, `_seq`, `_it`, `_item`, `_new_tmp`, `_if` | sema, simplify | mixed | intermediate values | mostly tracked as locals |
| `__tN` (flatten) | lowering (`flattenIrValue`) | `T`, `RM` | any non-place operand | not tracked, the consumer takes it |
| `__tN` (borrowed) | lowering, read-only consumer | `RM` only | an owned rvalue read by a borrower | tracked, dropped at scope exit |
| `__tN` (discarded) | lowering, expression statement | `E`, `RM` | a call result nobody uses | dropped right after the assignment |
| `__call_res_N` | lowering, call to a `!T` | `T`, `RM` | the result struct (tag and payload) | not tracked, the payload is consumed and the tag cleared by unmanaged writes |
| `__catch_resN` | lowering, `catch` | `T`, `RM` | the unwrapped payload | not tracked |
| `__ternary_N` | lowering | `T`, `RM` | the selected branch value | not tracked |
| `__optN`, `__tN` (`??`, `?.`) | lowering | `T` | the optional result | not tracked |
| `__condN` | lowering (`&&`, `||`) | `T` | a bool | not tracked, owns nothing |
| `__arrN` | lowering (array literal to slice) | `T` | the fixed array of elements | not tracked |
| slice `__tN` | lowering (array to slice coercion) | none or `T` | the slice view | varies |
| `__envN` (heap) | lowering (closure) | `T` | the allocated environment | not tracked, owned by the closure value |
| `__envN` (stack) | lowering (closure) | `RM` or none | the environment struct | tracked when it owns |
| `__mvN` | the pass | `T` | a member moved out | marked moved, consumed by the next statement |
| `__copyN` | the pass | `T` | an implicit copy of an owned operand | marked moved, consumed by the callee |
| `__drop_flags` | the pass | none | one bit per conditionally owned variable | read by `drop-if`, written at defs and moves |

Two observations. First, ownership is decided by the creating code and recovered by
the pass from one flag, so the same flag means three things. Second, `Temporary`
is used for variables that own nothing (`_idx`, `__condN`) and for ones that own
something whose consumer may not take it (`_r`). Both are wrong for the pass.

### 3.1 Which layer should create which temporary

The same job, "evaluate this operand once, in order, before the statement", is
done twice: by `simplify` on the AST, and again by `lower` on the IR. The memory
pass then has to make sense of both.

**Simplify** (1624 lines, `middle/simplify/simplify.c`) hoists operands into
variables ahead of the statement:

| Temporary | Hoisted when | Does lowering do it anyway? | Verdict |
|---|---|---|---|
| `_m` | member access on an rvalue (`make().id`) | yes: `visitMemberExpr` calls `lowerBorrowedOperand` on the target | remove from simplify |
| `_idx` | index expression that is not a place | yes: `visitIndexExpr` calls `lowerFlattenedOperand` on target and index | remove from simplify |
| `_r` | `&rvalue` | yes: `visitUnaryExpr` flattens its operand first | remove from simplify. This is the leaking one. |
| `_c` | call through an rvalue closure (`.fn` and `.env` both read it) | to check: lowering must read the callee once | keep until checked, then move |
| `_if` | ternary rewritten to an `if` | lowering has its own `visitTernaryExpr` (`__ternary_N`, 52 lines), probably unreachable now | keep in simplify, delete the dead lowering copy after checking |
| `_ga` | global array initializer | no, this is static initialization | keep |

The `whileCond` machinery in simplify (`reassignInWhile`, about 30 lines spread
over four places) exists only because simplify hoists a temporary *before* the
loop and then has to re-assign it inside the condition. Lowering emits the
condition in its own block, so a temporary created there is evaluated each
iteration with no special case. Removing the hoisting removes the mechanism. See
3.2: the mechanism does not work today.

**Sema** creates `_rv` (an rvalue used as a place) and `__obj` (the object being
constructed). Both express meaning that exists at the AST level, a receiver and a
constructor cover, so they stay. They are only classified, see section 4.

**Lower** (4863 lines, `middle/ir/lower.c`) is the largest file in the compiler.
`lowerType` alone is 574 lines, `visitBinaryExpr` 256, `visitCatchExpr` 176,
`visitReturnStmt` 116. What it does that is about ownership, and what should
move:

| In lower today | Lines | Verdict |
|---|---|---|
| `flattenIrValue` chooses `Temporary`, no flag, or `Ephemeral` from a `borrowed` argument at the point of creation | `lowerFlattenedOperandEx`, 10 call sites | **move to normalize.** Lowering flattens for order only. The class is chosen later from how the temporary is used (4.1). |
| the `!T` result protocol: checks the tag, unwraps the payload, clears the tag with an `Unmanaged` write so the destructor does not drop what was taken | `emitResultCheckAndUnwrap`, `visitCatchExpr`, `visitReturnStmt` | **later.** These are hand-written ownership transfers hidden behind `Unmanaged`. They should become one explicit event ("payload taken from a result") that the analysis understands. Larger change, own step. |
| a copy of a place for an explicit `@copy` used as an expression | `visitBackendCall`, `bfiCopy` | keep. An explicit `@copy` is a user operation. Its temporary is the in-flight class. |
| closure environment allocation | `visitClosureValue` | keep. The environment is owned by the closure value. |
| `lowerType`, the other 300-line visitors | | not ownership. They should be split by type category and by operator family, as a separate cleanup. |

The direction: **lowering translates structure and fixes evaluation order, and
stops deciding ownership.** The memory pass, preceded by `normalize`, is the one
place that decides who owns a value.

### 3.2 Why simplify rewrites `while`, and why it should not

Simplify does two things to a `while` statement (`visitWhileStmt`, about 25 lines):

1. `simplifyCondition` turns a non-bool condition (integer, float, char, pointer)
   into `x != 0` or `x != null`. It is applied only to `while`. The same rewrite for
   `if` is commented out, and `if (x)` works without it. Lowering already has
   `lowerConditionOperand`, which handles the optional and closure conditions, so
   the integer, float, char and pointer cases belong there too.
2. Any temporary hoisted from the condition (`_m`, `_c`, `_idx`) is declared
   *before* the loop, and simplify tries to re-evaluate it by putting the
   re-assignments into the `update` clause of the `while`. **Lowering never lowers
   that clause.** `visitWhileStmt` in `lower.c` handles the condition and the body
   only. `update` is read by sema's `check.c`, `for.c` clears it, and nothing else
   reads it.

The effect, checked by running it:

```cxy
var counter = 0
func next(): Node { counter = counter + 1; return Node(counter) }
while next().id < 4 { total = total + 1 }
```

Simplify hoists `_m4 = next()` before the loop, the condition becomes
`if (_m4->id < 4)`, and nothing ever calls `next()` again. The program loops
forever. The only reason the `while` rewrite exists is a workaround for hoisting
that is both unneeded (lowering evaluates a condition in its own block each time)
and broken. The fix is to delete it: lowering emits the temporary inside the
condition block, and `lowerConditionOperand` takes over `simplifyCondition`. The
rest of this document follows from the same observation.

A related cleanup: `visitMatchStmt` in simplify carries about 35 lines of
commented-out code.

## 4. Proposed classes

A temporary of owning type is in exactly one class. Names are prefixes, followed by
a counter.

| Class | Name | Flag | Meaning | Created when | Consumer | Dies |
|---|---|---|---|---|---|---|
| **In flight** | `__own` | `Temporary` | an owner with no name yet, handed to exactly one taker | a nested operand must give up or take a count: see rules N2, N3, N5 | exactly one: an assignment, a call argument to an owned parameter, a literal element, a return | consumed, never dropped by the pass |
| **Statement** | `__st` | `Ephemeral` | an owned value only read, or discarded | the value has no taker and is not bound: N6, N7 | none | dropped at the end of the statement that created it |
| **Extended** | a normal local | none | an owned value a reference variable is bound to | `var r = &make()` | none | dropped at the end of the scope of `r` (the C++ lifetime extension rule) |
| **Result** | `__res` | `Temporary` | the `!T` result of a call: a tag and a payload | a call to a `!T` function | the check reads the tag, the payload is taken, the tag is cleared | the tag-aware destructor, if the payload was not taken |
| **Return slot** | `__ret` | `Returned` | the value being returned | functions that return a value | the caller | not dropped, the caller owns it |
| **No ownership** | `__v` | none | a bool, number, pointer or plain struct | lowering control flow (`&&`, `??`, index values) | n/a | n/a |

### 4.1 How a temporary gets its class

`normalize` decides the class of every compiler-created temporary of an owning
type by looking at its uses, not at the flag it arrived with. The flag from the
creator is only a marker that the variable is compiler-generated.

| Use of the temporary | Class |
|---|---|
| exactly one use in an owned position (assignment, owned parameter, literal element, return) | In flight |
| only reads (a member access, a condition, a reference parameter), or no use | Statement |
| bound to a reference variable (`var r = &tmp`) | Extended |
| a `!T` call result | Result |
| the type owns nothing | no class, no flag |

This is what fixes `_r` without touching simplify: `peek(make())` is a read, so its
temporary is Statement, regardless of the `Temporary` flag simplify gave it. A
creator can no longer get the class wrong, because it does not choose one.

Flag meanings after the redesign, each with one meaning:

| Flag | On | Meaning for the pass |
|---|---|---|
| `Temporary` | a variable | in flight, or a `!T` result: tracked by its statement, never dropped by scope exit |
| `Ephemeral` | a variable | dropped at the end of its statement |
| `Returned` | a variable | its ownership goes to the caller, not dropped |
| `ReferenceMembers` | a type or variable | the type needs drop glue, so ownership tracking applies |
| `Unmanaged` | a statement | emitted by the compiler with the ownership already worked out, skip it |

`Temporary` is not set on a variable whose type owns nothing, so `_idx` and
`__condN` stop looking like ownership temporaries.

The fix for 1.3 falls out of the table: `peek(make())` and `var r = &make()` are
the **Statement** and **Extended** classes, not **In flight**. `_r` stops carrying
`Temporary`. The pass never has to guess.

## 5. Normalization rules

A new step, `normalize`, runs after lowering and before the analysis. Each rule is
a pattern, whether it needs a temporary, and why.

| Rule | Pattern (owning type) | Temporary | Result | Why |
|---|---|---|---|---|
| N1 | `&&x`, `x` a local or parameter, in any operand | none | the statement `move x` is emitted before the statement, the operand is `x` | killing `x` needs a statement. The value is `x` itself. |
| N2 | `&&p.f` or `&&a[i]` | **In flight** `__own` | `__own = p.f; p.f = null` (unmanaged), operand is `__own` | the source cannot be killed as a variable, it has to be emptied, and the value has to be read first |
| N3 | a place passed to an owned position (owned call parameter, literal element, union or optional payload, closure environment) | **In flight** `__own` | `copy place -> __own`, operand is `__own` | the operand cannot carry a retain. The assignment form `x = place` needs no temporary, see N3b |
| N3b | `x = place`, `var x = place`, `return place` | none | `copy place -> x` | the destination exists already |
| N4 | `&&x` or a place passed to a reference parameter | none | the move is dropped, the operand is the borrow | a reference does not own |
| N5 | a call or literal that owns something, nested inside another owning operand | none: lowering already does it | `__t = call(...)`, operand is `__t` | evaluate once, in order, and give the call result a name with one consumer. Checked: `flattenIrValue` names every nested call and literal, so normalize has nothing to add. |
| N6 | a call result in an expression statement | **Statement** `__st` | `__st = call(...)`, dropped after the statement | nobody takes it |
| N7 | an owned rvalue read by a borrower (`f().x`, `peek(f())`, a condition) | **Statement** `__st` | same | nobody takes it, and it must still be freed |
| N8 | `var r = &rvalue` | **Extended** local | the temporary is a normal local in `r`'s scope | `r` must not dangle |
| N9 | a call to a `!T` function | **Result** `__res` | unchanged from today | the tag protocol is already correct |

After normalization an operand of a call, a literal, a cast or an assignment right
hand side is a variable, a constant, a borrow, or a pure expression of those. The
analysis never looks inside it.

## 6. Structure

The pass becomes four steps in separate files. Each function is short and either
reads or writes.

| File | Step | Reads or writes | Contains |
|---|---|---|---|
| `normalize.c` | N1 to N9 | writes | one function per rule, a driver that visits each statement once |
| `cfg.c` | build the CFG | reads | blocks and edges, from the existing `buildFunctionCfg` |
| `flow.c` | analysis | reads | **one** `applyEvent(state, event)` for assign, move, copy, drop and scope exit. The dataflow solver, the drop-flag selection and the elaboration all call it. |
| `elaborate.c` | insert drops and flags | writes | `dropAtScopeExit`, `dropOnReassign`, `setFlagAtDef`, `clearFlagAtMove`: each calls `applyEvent`, none re-implements a rule |

`manage.c` keeps only `runMemoryManagement` and the per-function driver.

The event for a statement is read from the statement itself, since normalization
guarantees one shape: an `irAssign` or `irCopy` defines its target, a `move x`
statement kills `x`, an `irDrop` kills its target, an `irScopeExit` ends a scope.
`forEachOwningSlot`, `moveSlot` and `processPayloadItem` go away.

## 7. How to get there

Each step ships with every test passing, so nothing is left half switched on.

| Step | Change | Output changes? | Checked by |
|---|---|---|---|
| S0 | Tests for the two bugs found so far, the `peek(make())` leak (1.3) and the `while next().id < 4` hang (3.2), added disabled until their fix lands in S5 and S7. A test per row of section 3. | no | the new tests |
| S1 | Move the CFG and bitset code out of `manage.c` into `cfg.c` | no | IR snapshots unchanged |
| S2 | Add `applyEvent` in `flow.c` and make the three copies of the rules call it | no | IR snapshots unchanged, unit tests of `applyEvent` for each event |
| S3 | Split `elaborateDropsAndReassignments` into the small functions of section 6 | no | IR snapshots unchanged |
| S4 | Add `normalize.c`, one rule at a time, deleting the matching code in `manage.c` | yes, reviewed | IR snapshots, ASan suite, the verifier |
| S5 | Classify temporaries by use (4.1). The `_r` leak goes away here. | yes, only for leaks and lifetimes | the S0 tests now pass, the trace suite shows no new leaks |
| S6 | Extend the verifier: O10 for temporaries, class checks | no | unit tests |
| S7 | Remove the duplicate hoisting from simplify (`_m`, `_idx`, `_r`), the `whileCond` machinery and `simplifyCondition` for `while` (3.2). Lowering handles the condition. | yes: the infinite loop in 3.2 is fixed | IR snapshots, the new `while` tests |
| S8 | Check and move `_c`, delete the unreachable `visitTernaryExpr` in lower | no | IR snapshots, a closure-call test |
| S9 | Make the `!T` result protocol an ownership event instead of `Unmanaged` writes | yes | the exception tests, the verifier |

S1 to S3 are moves of existing code with no change in output. They reduce risk for
S4. S5 is where behavior changes on purpose: the leak in 1.3 goes away. S7 fixes
the `while` hang.

Two more tracks run independently of the memory pass. They are specified in
sections 9 and 10.

| Track | Change | Output changes? |
|---|---|---|
| L1 to L5 | Split `lower.c` into files (section 9) | no |
| I1 to I3 | Carry ownership information on IR nodes (section 10) | no, until a consumer switches over |

## 8. Decisions

Agreed in review:

1. **Lifetime of read-only temporaries:** dropped at the end of the statement,
   so the object is freed earlier (C++ semantics).
2. **Evaluation order:** operands hoisted into statements are evaluated left to
   right.
3. **Moves in async code:** N1 emits the `move x` statement before the call that
   can suspend, as today. The coroutine ASan tests cover it.
4. **Verifier independence:** the verifier keeps its own walker. After S4 the pass
   has no shape walker, but the verifier is still checked against an independent
   reading of the IR.
5. **Simplify and lower:** simplify stops hoisting `_m`, `_idx` and `_r`, and the
   `while` handling moves to lowering (3.2). Lowering already flattens the operands
   of member access, index and unary expressions, so the same temporaries are
   created, in one place.
6. **Cost.** The IR gains temporaries. The C compiler removes them, and they are
   named by class, so the generated C is readable. The number added should be
   measured on the stdlib at S4.

## 9. Splitting `lower.c`

`lower.c` is 4863 lines and 110 functions. `LowerContext` is already public in
`lower.h`, so the state is shared in the open. Most helpers are `static` only
because everything shares one file. The split is mechanical: no logic changes, no
output changes, so every IR snapshot is the regression test.

Layout: each IR stage has its own folder under `middle/ir/`, and the sources every
stage shares stay in `ir/` itself.

```
ir/node, type, visitor, context     shared by every IR stage
ir/lower/                           AST to IR (this section)
ir/dump/                            text dump today, the S-expression dump later
ir/reachability/
```

Proposed files in `ir/lower/` (line ranges and sizes are from the original
file). Done: `lower.c`, `internal.h`, `names.c`.

| File | Contents | Lines then |
|---|---|---|
| `lower.c` | `lowerToIr`, the visitor table | ~120 |
| `internal.h` | the helpers shared between files, the `visit*` functions with a `lower` prefix | new |
| `names.c` | unique variable names, symbol and mangled names, `getNormalizedName` | ~170 |
| `type.c` | the type cache, type names, `lowerType` | 223 to 1067, ~850 |
| `operand.c` | `isIrLValueOrConst`, `flattenIrValue` and its callers, `lowerCompositeElement`, `lowerConditionOperand`, slice and optional into a target | 1067 to 1370, ~300 |
| `access.c` | member, index, slice, path, pointer-of | ~400 |
| `operator.c` | binary, unary, assignment, casts and coercions | ~520 |
| `call.c` | callee types, arguments, calls | ~300 |
| `result.c` | the `!T` protocol: result check and unwrap, `catch`, `raise`, the error parts of `return` | ~450 |
| `aggregate.c` | array, tuple, struct, union values | ~200 |
| `closure.c` | closure values and environments | ~80 |
| `backend.c` | `visitBackendCall` | ~230 |
| `stmt.c` | variable declarations, expression statements, blocks, `if` | ~330 |
| `loop.c` | `while`, `for`, `break`, `continue`, `yield` | ~300 |
| `match.c` | `switch` and `match` | ~190 |
| `decl.c` | function declarations, classes and structs, the program, lifecycle types, `asm` | ~340 |

After the split, the three functions over 250 lines (`lowerType` at 574,
`visitBinaryExpr` at 256) are split by category. `lowerType` becomes one function
per type category (struct, class, tuple, union, optional, closure, function,
array). `visitBinaryExpr` becomes one function per operator family (logical,
coalesce, arithmetic, comparison).

Order, each step building and passing everything before the next:

| Step | Moves |
|---|---|
| L1 (done) | `internal.h`, `names.c`, the folder layout |
| L2 | `type.c` |
| L3 | `operand.c`, `access.c`, `operator.c` |
| L4 | `call.c`, `result.c`, `aggregate.c`, `closure.c`, `backend.c` |
| L5 | `stmt.c`, `loop.c`, `match.c`, `decl.c`, and the function splits |

Because the memory pass track changes `lower.c` as well (S5, S7, S8, S9), do L1 to
L5 first or at least before S7, so the later changes are small edits in small files.

## 10. Information on IR nodes

Lowering reads the AST and its types, then drops most of what it knows. Later
stages recompute it from the shape of the IR: `injectCallArgumentCopies` looks at
the callee type to find owned parameters, `forEachOwningSlot` lists the shapes
where a value is handed over, the `@nocopy` check tests for an empty source range
to find generated glue. Each of these is a place where information that lowering
had was thrown away.

**Where to put it.** `IrNode` is 160 bytes. `tag` is 4 bytes at offset 0 and `loc`
starts at offset 8, so there are 4 unused bytes at offset 4. A `u32 info` field
there costs nothing. The existing `flags` field holds AST flags, and all 64 bits
of that set are taken by AST meanings, so IR information gets its own field and
its own names, with a static assertion that `sizeof(IrNode)` does not change.

Draft bits. Each has a producer and a consumer, otherwise it is not added:

| Bit | Set by | Read by | Replaces |
|---|---|---|---|
| value class (3 bits): in flight, statement, extended, result, return slot | `normalize` (from 4.1) | the analysis, the verifier (O10), the tracer names | the overloaded `Temporary` and `Ephemeral` flags |
| `OwnedPosition` | lowering, on an operand that is consumed by a taker: an owned call argument, a literal element, an optional or union payload, a closure environment, an assignment source, a return value | `normalize` N3 and N5, the verifier | the callee-type walk in `injectCallArgumentCopies` and the shape list in `forEachOwningSlot` |
| `Place` | lowering, on a variable, field, element or dereference | `normalize` N3 | `isIrLValueOrConst` recomputed in several files |
| `UserWritten` | lowering, on `&&`, `@copy` and `&` that came from source | the verifier (rules against user syntax), diagnostics | nothing today |
| `Implicit` | `normalize`, on a copy or move it inserts | the tracer (provenance of a retain), diagnostics | nothing today |
| `FromReference` | lowering, on a copy whose source had a `&T` type | the `@nocopy` check | the special case that looks through a reference type |
| `Glue` | lowering, on everything inside generated glue (copy and destructor overloads, visit functions) | the `@nocopy` check, the verifier, diagnostics | the empty source range heuristic |
| `Synthetic` | any pass that creates a statement or variable | diagnostics: do not blame the user for generated code | nothing today |

`OwnedPosition` is the one that changes how the pass is written. Lowering knows the
callee, the literal and the assignment when it builds the node, so it marks the
operand there. The pass then finds every owned operand by walking the IR and
reading one bit. There is no shape list to keep in step and no callee analysis.

Tracks:

| Step | Change |
|---|---|
| I1 | Add the field and helpers, a static assertion on the size, and print the bits in the IR dump so IR snapshots show them. No behavior change. |
| I2 | Lowering sets `OwnedPosition`, `Place`, `UserWritten`, `FromReference` and `Glue`. IR snapshots change once, reviewed. |
| I3 | Consumers switch one at a time and delete their heuristic: the `@nocopy` check first (`Glue` and `FromReference`), then `normalize` (`OwnedPosition`, `Place`), then the verifier (`UserWritten`). |

Open: should the IR dump always print the bits? That makes the information
visible in snapshots and tested, at the cost of one reviewed update of the
snapshots.

## 11. Testing with `TestCompiler`

Every step above is written test first. `src/cxy/testing/compiler.hpp` runs the
real compiler in process and keeps the program, so a test looks nodes up and
compares their S-expression with an expected pattern. No output file, no binary.

```cpp
TestCompiler cc;
REQUIRE(cc.compile(source));                      // parse + all passes
IrNodeWrapper demo = cc.function("demo");         // IR function
REQUIRE_IR_MATCHES(demo.findVariable("_m", 0), "(VarDecl _m0 ^Node)");
AstNodeWrapper f = cc.ast().findFunction("demo"); // AST side
```

- `IrNodeWrapper`: `sexpr()`, `findFunction`, `findGlobal`, `findBlock`,
  `findParam`, `findVariable(prefix, index)`. An empty wrapper answers empty.
- `AstNodeWrapper`: `sexpr()`, `findFunction`, `findClass`, `findVariable`.
- Dumping a fragment numbers generated names (`_m0`) off its whole function, so
  fragment patterns are stable.
- Only one `TestCompiler` may be alive at a time (process-global builtins).

### 10.1 What I2 built, and where it differs from the draft

| Bit | Status | Notes |
|---|---|---|
| `OwnedPosition` | built | Set by the IR constructors (`node.c`): assign source, call argument for a non-reference parameter (variadic by its own type), literal element, optional and union payload, closure env. Only on values that own something (`isIrTypeDestructible`). An exhaustive cross-check against `processPayloadItem` over `tests/lang` found no owned slot without the bit. |
| `UserWritten` | built | The parser records it on the AST node (`AstNode.userWritten`, in the head padding) for `&&`, `&` and `@copy`; lowering copies it to the Move, Cast/AddrOf and Copy it produces. Moves that sema inserts are not marked. |
| `Glue` | built, on the **function** | `AstNode.glue` is set where sema generates copy and destructor functions (`markGlue`), lowering sets the bit on the IR function. Per function is enough for the consumers (the `@nocopy` check skips glue functions), and a node inside glue needs no bit of its own. `flgGenerated` could not be used: generic instances carry it too. |
| `Place` | dropped | Derivable from the tag (`VarRef`, `Member`, `Index`, `Deref`, through `Alias`). A bit on every one of them would be noise in every snapshot. |
| `FromReference` | dropped | Derivable from `copy.src->type` being a reference, which is what the `@nocopy` check already reads. |
| `Synthetic` | lowering (`__t` temporaries) and normalize (`__mv`, `__copy`) | The verifier skips it, `classifyTemporaries` classifies the owning ones. |
| value class | `classifyTemporaries` (normalize) | Written on a synthetic temporary from its uses, see 4.1. |
| `Implicit` | reserved | Not set yet. |

Marks live on the node that is in the slot. When the pass replaces a `Move` by
its source (a move of a variable), the bit is not carried over. Switching the
pass to read the bit (I3) means keeping it there, or reading it before the
rewrite.

The dump prints the bits as markers inside the node (`(VarRef a :owned-position)`)
and after `(Ret ..)` for a function (`:glue`). Constants are atoms and print
without them. No existing snapshot changed.

## 12. Task list

Source of truth for order. Work top to bottom. Check a task when it is committed.
A task that changes what lowering produces adds a test in `tests/lang/middle/lower/` first (11.1).
Lowering is split first (section 9), so the later changes are small edits in
small files. Each split task changes no output: the IR snapshots are the check.

Done

- [x] L1: split `lower.c`, shared header and names into their own files
- [x] Move IR sources into `ir/lower`, `ir/dump`, `ir/reachability`
- [x] IR S-expression dump (`--dump-ast IRSEXP`)
- [x] `TestCompiler` with IR and AST node wrappers, in process
- [x] I1: `IrNode.info` field and bits, printed in the IR dump
- [x] I2: lowering sets `OwnedPosition`, `UserWritten` and `Glue`

Split `lower.c` (4675 lines today)

- [x] L2a: move the type cache and type names into `ir/lower/type.c`
- [x] L2b: move `lowerType` into `type.c`
- [x] L3a: move `operand.c` (`flattenIrValue`, `lowerCompositeElement`, `lowerConditionOperand`, slice and optional into a target)
- [x] L3b: move `access.c` (member, index, slice, path, pointer-of)
- [x] L3c: move `operator.c` (binary, unary, assignment, casts, coercions)
- [x] L4a: move `call.c` (callee types, arguments, calls)
- [x] L4b: move `result.c` (the `!T` protocol: unwrap, `catch`, `raise`)
- [x] L4c: move `aggregate.c`, `closure.c` and `backend.c`
- [x] L5a: move `stmt.c` (declarations, expression statements, blocks, `if`)
- [x] L5b: move `loop.c` (`while`, `for`, `break`, `continue`, `yield`)
- [x] L5c: move `match.c` and `decl.c`
- [x] L5d: split `lowerType` into one function per type category
- [x] L5e: split `visitBinaryExpr` into one function per operator family
- [x] L5f: split `visitCatchExpr` and `visitReturnStmt`

Clean up `simplify` and `lower` (section 3)

- [x] S0: disabled tests for the `peek(make())` leak and the `while next().id < 4` hang
- [x] `lowerConditionOperand` handles integer, float, char and pointer conditions (takes over `simplifyCondition`)
- [x] Remove `simplifyCondition` from simplify
- [x] Remove the `whileCond` / `reassignInWhile` machinery from simplify, enable the S0 hang test
- [x] Remove the `_m` hoisting from simplify (lowering already flattens the member target)
- [x] Remove the `_idx` hoisting from simplify (lowering already flattens target and index)
- [x] Remove the `_r` hoisting from simplify, which is the leaking one
- [x] Check that lowering reads a closure callee once, then move `_c` out of simplify
- [x] Delete the unreachable `visitTernaryExpr` in lower, keep `_if` in simplify
- [x] Delete the commented-out code in simplify's `visitMatchStmt`

Use the IR bits (section 10)

- [x] I3a: the `@nocopy` check skips `:glue` functions, delete the empty-source-range test
- [x] I3b: `injectCallArgumentCopies` reads `:owned-position`, no callee type walk
- [x] I3c: replace `forEachOwningSlot` with a walk over `:owned-position` operands
- [x] I3d: verifier applies the user-syntax rules by `:user-written`
- [x] Fix the `DropFlagMismatch` and `RedeclaredOwner` findings on a `while` condition temporary (`--warnings=Ownership` on lang/mem/36 and 38)

Restructure the memory pass (sections 5 and 6)

- [x] S1: move the CFG and bitset code from `manage.c` into `cfg.c`
- [x] S2: write `applyEvent` in `flow.c`, call it from the three copies of the rules
- [x] Make the three copies agree on which statements produce events: a user `@copy` over an owned variable does not drop the old value (skipped test in test_memory_pass_bugs.cpp), gen/kill ignores `ScopeExit`, the drop flag analysis ignores moves in call statements
- [x] S3: split `elaborateDropsAndReassignments` into small functions
- [x] S4a: add an empty `normalize.c` pass between lowering and the analysis
- [x] S4b: N1 and N4, moves become statements, moves into reference parameters are dropped
- [x] S4c: N2, move out of a field or element
- [x] S4d: N3 and N3b, copy a place passed to an owned position
- [x] S4e: N5, nested calls and literals that own something (lowering already names them, test added)
- [x] An ephemeral temporary is not an owner in the analysis: a discarded call result was dropped twice (test, lang/mem/39)
- [x] S5a: lowering marks `__t` temporaries `:synthetic`, normalize writes their class (in flight, statement, extended) from their uses, the verifier skips synthetic variables
- [x] S5b: a statement temporary is dropped after the last statement that reads it (a terminator reads a hoisted value), not at scope exit
- [x] S5c: normalize sets the temporary and ephemeral flags from the class, `flattenIrValue` stops choosing them (the `_r` leak and the S0 tests are already done)
- [x] V1: normalization runs as its own step before the verifier snapshot (`runNormalization`), the memory pass no longer calls it
- [x] V2: the snapshot keeps the statements of each block in order, so a statement the pass removes (`move x`) keeps its events
- [x] V3: normalize keeps what the verifier reads: `:user-written` on `move x` and on a borrowed move it strips, `:implicit` on the capture of N2
- [x] V4: the verifier reads the normalized form (`move x`, capture and clear, copy statements); fixes the false `UnclearedMove` (test on real programs, `--warnings=Ownership`)
- [x] V5: the verifier tells user variables from temporaries by `:synthetic`, `isUserVariable` drops the flag checks
- [x] C1: lowering states the class of every variable it makes (`markCompilerVar`): pending for flatten and discarded, transient for sema and simplify temporaries and the creators that know, result, return slot
- [x] C2: the memory pass, normalize, the verifier and lowering read the class (`irIsConsumedTemp`, `irIsStatementTemp`, `irIsReturnSlot`), not `Temporary`, `Ephemeral`, `Returned`
- [x] C3: lowering and normalize no longer set `Temporary`, `Ephemeral`, `Returned` on IR variables, the dump prints the class
- [x] C4: the verifier follows an owner by its class (none or extended), not by who made it; the stack closure environment and the slice temporary of lowering are marked `:synthetic`. Variables sema, simplify and shake make without `Temporary` (`__it`, `__item`, `__i`, `_if`, `_hd`, `_gi`, `__sched`) are ordinary locals the pass manages, nothing reads their provenance.
- [x] S6: verifier checks O10 on temporaries by their class (in flight: one consumer, statement: one drop after the last read)
- [x] Make the `!T` result protocol an ownership event instead of `Unmanaged` writes: the payload or exception a result gives up is a `Move` out of its place (normalize captures it and clears the field), a dropped exception is a `Drop` that clears its field, nothing writes the tag
- [x] The unwrapped payload of a successful call (normalize N1b, in an owned position) and `raise e` of a caught exception (lowering) are a `Move` out of the result, `needsOwnedCopy` no longer special-cases result members
- [x] A payload of a `!T` call that is only borrowed, `peek(make())`, is moved into a statement temporary (normalize N2b) and dropped when its statement ends; it leaked before. A view taken from it does not outlive it: `json.cxy` kept `expectString().s()` past its statement and now holds the `String`
- [x] The verifier (O10) reports a `!T` payload that is still read in place after normalization (a borrow, a `Return` operand, a `Copy` source): only a drop, a clear or the capture of a move may touch it
- [x] Find the cause of the intermittent `lang/mem/32_launch_env_ownership [asan]` failure: the detached thread was never waited for, the test now waits on an `Atomic` flag

### 11.1 Where the lowering tests live

`tests/lang/middle/lower/`, one file per area: `test_info.cpp` (IR info bits),
`test_conditions.cpp`, `test_binary.cpp`, `test_result.cpp` (the `!T` protocol),
`test_member_rvalue.cpp` (temporaries of rvalues, `while` conditions) and
`test_type.cpp`. A change to what lowering produces comes with a pattern test in
that folder, written before the change. A pure move of code needs none, the IR and
C snapshots are the check.
