# Lowering tests still to write

Lowering is `src/cxy/lang/middle/ir/lower/`. A test is a pattern on the IR
S-expression (`TestCompiler`, `REQUIRE_IR_MATCHES`). Check a line when the test is
committed. A name is `file: test name`, the description says what the pattern
asserts.

Covered already: info bits (`test_info`), conditions (`test_conditions`), binary
operators `&&` `||` `??` closure `== null` (`test_binary`), the `!T` protocol basics
(`test_result`), rvalue members and `while` conditions (`test_member_rvalue`),
borrowing an rvalue (`test_reference`), index access (`test_access`),
parameter types (`test_type`).

## Names (`names.c`) , `test_names.cpp`

- [ ] `names: a local that shadows a parameter gets a suffix`: `x` then x_1, params keep their name
- [ ] `names: two locals with one name in different scopes are unique`: `x`, `x_1` in one function
- [ ] `names: a method is module.Type.method`: display name of a member function
- [ ] `names: a generic instance has the type arguments in its name`: two instances, two different symbols
- [ ] `names: main is called main whatever the module`: display name is `main`
- [ ] `names: a long type name is bounded and stable`: same shape twice gives the same name

## Types (`type.c`) , `test_type.cpp`

- [ ] `type: a class that holds itself terminates`: `class N { next: N? }` lowers, the field type is `^N?`
- [ ] `type: a derived class starts with the fields of its base`: field order in the struct
- [ ] `type: a @nocopy class and a struct that holds one are marked`: `noCopy` seen through the dump or the type table
- [ ] `type: an enum has its options and base type`: `Color_Red` as a global ref with the enum type
- [ ] `type: a slice is a pointer and a length`: `[i32]` fields `ptr`, `len`
- [ ] `type: a closure is env, fn and dtor`: fields of the closure struct, `fn` takes the env first
- [ ] `type: a reference to an array is the element pointer`: `&[i32, 3]`
- [ ] `type: a destructible struct is marked as holding references`: flag on a struct with a class field

## Literals and constants (`operator.c`) , `test_literal.cpp`

- [ ] `literal: integer, float, char, bool, string`: each becomes its constant with its type
- [ ] `literal: null for a pointer, an optional and a class`: `null` typed, `(None)` form for the optional
- [ ] `literal: a negative integer`: sign is kept

## Unary, cast, coercion, assignment (`operator.c`) , `test_operator.cpp`

- [ ] `unary: not on a bool, on an optional`: `!x`, `!opt` tests `ok`
- [ ] `unary: minus, complement, increment, decrement`: pre and post forms
- [ ] `unary: deref of a pointer and of an optional`: `(Deref p)`, `(Member opt val)`
- [ ] `unary: address of and reference of`: `&x` on a struct, on a class (a cast), on an array
- [ ] `unary: delete drops its operand`: `(Drop x)`
- [ ] `cast: numeric, pointer and class casts`: `(Cast type expr)`
- [ ] `coercion: T to T?`: `(Some x)`, `null` to the empty optional
- [ ] `coercion: array to slice`: struct with `ptr` and `len`
- [ ] `coercion: a member to a union`: tagged union value with the right index
- [ ] `assign: compound operators`: `+=` becomes assign of a binary
- [ ] `assign: through a reference`: `r = v` writes to the target of `r`
- [ ] `assign: @copy on the right is a Copy statement`: `(Copy src dst :user-written)`
- [ ] `assign: optional, slice and union targets`: the right side is converted into the target

## Operands (`operand.c`) , `test_operand.cpp`

- [ ] `operand: a call in an operand position is flattened once`: `__t0` declared and assigned before use
- [ ] `operand: a place is not copied into a temporary`: `x.y` stays a member
- [ ] `operand: a borrowed rvalue is tracked as a local`: flags of `__t` for a borrowed class call result
- [ ] `operand: a composite element that owns something is hoisted`: element of a literal is a call
- [ ] `operand: an optional condition tests ok, a closure tests fn`: `if opt`, `if closure`
- [ ] `operand: slice into a target`: `[]` is all zero, an array fills ptr and len
- [ ] `operand: optional into a target`: `null`, a value, an optional value
- [ ] `operand: tagged union into a target`: value of a member type, a union value

## Access (`access.c`) , `test_access.cpp`

- [ ] `access: a local, a parameter, a global and a function reference`: `VarRef` vs `GlobalRef`
- [ ] `access: a struct, class and tuple member`: `(Member x name)`, tuple by index
- [ ] `access: an enum option is a global ref`: `Color.Red`
- [ ] `access: an optional member`: `a?.b` branches on `ok`
- [ ] `access: redirect through an optional`: `->` form
- [x] `access: index of an array, a slice, a pointer`: `(Index x i)`
- [ ] `access: slice of an array with a range`: ptr offset and length
- [ ] `access: a static member and a method`: callee is a global ref

## Calls (`call.c`) , `test_call.cpp`

- [ ] `call: arguments in order, an owned argument is marked`: `:owned-position` per parameter
- [x] `call: a call through a closure`: callee `(Member f fn)`, first argument `(Member f env)`
- [ ] `call: variadic arguments`: extra arguments follow the declared ones
- [ ] `call: optional call a?.f()`: branches on the receiver
- [ ] `call: a !T callee outside a catch`: result temporary, tag check, error goes to the caller
- [ ] `call: a !T callee inside a catch`: no check, the catch reads the tag

## Result protocol (`result.c`) , `test_result.cpp`

- [ ] `result: catch as err binds the exception`: alias to the payload member
- [ ] `result: catch discard drops the payload`: `catch discard` on `!T` with a class payload
- [ ] `result: yield in a catch body`: assigns the catch result and jumps to merge
- [ ] `result: raise inside a catch`: the caught exception is moved, not dropped
- [ ] `result: return in a catch body`: caught exception released first
- [ ] `result: return a slice, optional, union into !T`: converted into the payload slot
- [ ] `result: nested catch`: inner catch context is restored

## Aggregates and closures (`aggregate.c`, `closure.c`) , `test_aggregate.cpp`

- [ ] `aggregate: array literal`: `(Array ...)` elements and count
- [ ] `aggregate: tuple literal with an optional member`: `T` stored in `T?` is wrapped
- [ ] `aggregate: struct literal in field order`: fields by index whatever the source order
- [ ] `aggregate: struct literal with an optional field`: `(Some x)` and `null`
- [ ] `aggregate: union value`: index and payload
- [ ] `closure: a non-escaping closure has a stack environment`: env is a local, `AddrOf`
- [ ] `closure: an escaping closure allocates its environment`: `alloc`, dtor is set
- [ ] `closure: captures are fields of the environment`: `Field a (VarRef a)`

## Backend calls (`backend.c`) , `test_backend.cpp`

- [ ] `backend: copy of a place`: `(Copy src dst)`, with and without a destination
- [ ] `backend: move`: `(Move x)`
- [ ] `backend: drop`: `(Drop x)`
- [ ] `backend: alloc, alloca, zeromem, sizeof, len, slice`: one pattern each
- [ ] `backend: atomic load, store, exchange, compare-exchange`: operations and memory order
- [ ] `backend: atomic fetch ops and fences`: add, sub, or, and, xor, thread and signal fence
- [ ] `backend: current exception`: `ex!` reads the catch payload

## Statements (`stmt.c`) , `test_stmt.cpp`

- [ ] `var: initializer of a plain, slice, optional and union variable`: assign, or conversion into the target
- [ ] `var: a variable with no initializer`: declared, zero initialized
- [ ] `var: @copy as an initializer is a Copy`: `(Copy src var :user-written)`
- [ ] `var: a module level variable`: `Global` with its initializer
- [ ] `var: a thread local global`: marked
- [ ] `expr: a discarded call result that owns something`: dropped in place
- [ ] `expr: a discarded !T payload`: dropped through the result temporary
- [ ] `block: a block opens and closes a scope`: `ScopeEnter` and `ScopeExit` pair
- [ ] `if: else and else if chains`: block names `IFn_then`, `IFn_else`, `IFn_end`
- [ ] `return: a value, a slice, an optional, a union, @copy`: how each reaches `__ret`
- [ ] `return: from inside nested scopes unwinds them`: `UNWIND` blocks with scope exits

## Loops and jumps (`loop.c`) , `test_loop.cpp`

- [ ] `for: a range with a step`: init, condition, update blocks
- [ ] `for: over a collection`: iterator calls
- [ ] `break: leaves the loop and exits the scopes in between`: scope exits then goto exit
- [ ] `continue: jumps to the update block`: scope exits then goto
- [ ] `break inside a catch body`: caught exception released first
- [ ] `yield: assigns the block value`: statement expression result

## Switch and match (`match.c`) , `test_match.cpp`

- [ ] `switch: cases with several values and a default`: `Switch` node cases
- [ ] `switch: no default`: falls to the exit block
- [ ] `match: a union by member type`: tag compare per case
- [ ] `match: a case that binds the value`: alias to the payload
- [ ] `match: no default case`: unreachable or exit

## Declarations (`decl.c`) , `test_decl.cpp`

- [ ] `func: parameters of a destructible type are marked`: flag on the parameter
- [ ] `func: an extern function has a prototype only`: `(Extern)`
- [ ] `func: extern attributes`: naked, noreturn, returns-twice
- [ ] `func: a function with @memtrace(callsite) takes a call site`: attribute set
- [ ] `func: an init cover takes a call site`: same attribute
- [ ] `func: a function with no return value has no __ret`: void
- [ ] `class: the lifecycle glue exists`: `op__destructor` and `op__destructor_fwd` for a class, `op__copy` for a struct
- [ ] `class: a struct with no class member has no copy glue`: no `op__copy`
- [ ] `program: the module lists globals first, then functions`: order in `Module`
- [ ] `asm: an inline asm statement`: `(Asm)`

## Dead code

- [ ] Delete `visitTernaryExpr` in `lower.c` (simplify turns a ternary into an `if` first). No test: show first that no program reaches it.
