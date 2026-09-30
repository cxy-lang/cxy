# Builtins Redesign

Status: DRAFT for review
Audience: compiler devs, indexed for use during implementation
Precedes: `docs/ir-migration-design.md` (this work should land *before* the IR
migration starts — several IR-doc decisions, e.g. §6/§8.5/§8.6 of that doc, already
assume `Optional`/`Void`/refcounting/exceptions are compiler-native rather than
structurally-trusted Cxy source; this doc is what makes that assumption true).

## 1. Motivation

`src/cxy/runtime/builtins.cxy` (compiled once, at driver init, via
`compileBuiltin()`/`CXY_BUILTINS_SOURCE`, see `driver/driver.c:initCompilerDriver`) is
the one Cxy source file the compiler trusts implicitly. Today that trust is **total
and unstructured**:

- `findBuiltinDecl(name)` (`lang/middle/builtins.c`) falls back to
  `findModuleMember(cxy_builtins.module, name)` — an **unrestricted lookup over every
  top-level name in the file**. Nothing in the source itself indicates which
  declarations the compiler actually depends on structurally versus which are
  incidental helpers (`strlen` extern, `__bswap32`, hash functions). Any declaration
  in the file is fair game for any compiler pass to fish for by bare name.
- Several core language features are implemented by the compiler **rewriting AST into
  calls against whatever happens to be registered under a fixed name**, with an
  unchecked assumption about that declaration's shape:
  - `Optional[T]`/`Some`/`None`: `lang/middle/sema/node.c`'s
    `transformOptionalSome`/`transformOptionalNone`/`transformOptionalType` rewrite
    `T?`/`Some(x)`/`None[T]()` into calls resolved via `findBuiltinDecl(S_Some/S_None/
    S_Optional)`. The struct's field order/shape (`ok`, `val`) is depended on with only
    a source comment (`/* @note: Do not change the order of these members */`) as
    protection.
  - `Slice[T]`: `lang/middle/shake/shake.c`'s `shakeArrayType` rewrites unsized `[T]`
    into a path resolved via `findBuiltinDecl(S_Slice)`, same unchecked-shape pattern.
  - `String`/`__string` equality: `lang/middle/sema/binary.c`'s `checkBinaryExpr`
    special-cases `typString` comparisons by inserting a call to
    `findBuiltinDecl(S___string)`.
  - `exception Foo(...)`: `lang/middle/shake/shake.c`'s `shakeExceptionDecl` rewrites
    every exception declaration into `class Foo : Exception`, where `Exception` is
    resolved by bare-name path lookup with no verification that whatever is named
    `Exception` actually has the shape (`name`, `stackTrace`, trace-push method) the
    frontend's exception machinery (`lang/middle/sema/exception.c`) assumes.
  - f-string interpolation (`f"... {x} ..."`): `shake.c`'s `shakeStringExpr`/
    `makeStrExprBuilder` hardcodes the accumulator type as
    `findBuiltinDecl(S_String)`.
- Memory management (`__smart_ptr_alloc/get/drop[_trace]`) is hand-written Cxy calling
  `malloc`/`free` directly, with a hand-rolled header struct (`__mem { refs, magic,
  dctor }`) whose field order the C backend's generated code depends on exactly —
  again, only a convention, not a checked contract, and there is no comptime/generic
  logic here that benefits from being *written in Cxy* at all.
- Assert/panic (`__cxy_assert`, `__cxy_assert_abort`, `__cxy_panic`) are the same
  shape: fixed-signature runtime plumbing with zero comptime logic, expanded into from
  the `assert!`/`panic!` macros, living in a `.cxy` file for no structural reason.
- `Void` (`pub struct Void{}`) is an empty marker struct matched by bare name
  (`isVoidResultType`'s `type->name == S_Void && type->ns == NULL`) — there is no
  behavior here at all, just a name lookup standing in for what should be a `Type` the
  compiler constructs directly.
- `__string` is a confusing name for what is genuinely just a borrowed
  pointer+length string view (`struct __string { s: string; ... }`) — no different in
  kind from a `Slice[char]`.

**Net effect:** the line between "declarations the compiler is structurally coupled
to" and "ordinary code that happens to live in the bootstrap file" does not exist in
the source. This doc draws that line explicitly, removes what shouldn't require
trust at all, and gives what's left an explicit, checked opt-in marker.

## 2. Three categories (recap, now finalized)

| # | Category | Examples | Verdict |
|---|---|---|---|
| 1 | Native type concepts — the compiler *constructs* the type/behavior directly, no Cxy source | `Optional`, `Slice`, `Void` | **Promote to fully native `Type`s**, delete from `builtins.cxy` |
| 2 | External runtime contracts — fixed C-callable signature, zero comptime/generic logic | `__smart_ptr_alloc/get/drop[_trace]`, `__cxy_assert*`, `__cxy_panic` | **Move implementation to hand-written C runtime**; keep a thin `pub extern func` prototype in `builtins.cxy` (see §6's correction — several compiler passes resolve these names via `findBuiltinDecl`/binding and need a declaration to exist) |
| 3 | Generic runtime behavior — genuinely complex, comptime-heavy, worth keeping as inspectable/overridable Cxy source | `String`, `OutputStream`, `Exception` (base only) | **Stay Cxy source**, gated behind new `@builtin` attribute + a compiler-side shape check |

Everything in category 3's *file* that isn't itself `@builtin`-marked (helper externs,
`OutOfRangeException`/`InvalidArgumentError`/etc., hash functions, bit-swap helpers)
becomes **ordinary, unprivileged code** — still compiled as part of the bootstrap
module (so it's always available, matching today's behavior), but no compiler pass is
allowed to `findBuiltinDecl` it. If some future pass needs a new builtin contract, the
declaration must be explicitly marked; there is no silent fallback path anymore.

## 3. The `@builtin` attribute

- New attribute, checked at bind time, meaningful **only** while compiling the actual
  bootstrap module (`hasFlag(node, BuiltinsModule)`, mirroring how `isBuiltinsInitialized()`/
  `flgBuiltinsModule` already gate other builtin-only behavior in
  `lang/frontend/parser.c`'s `operatorOverload`, `lang/middle/bind/resolve.c`'s
  `bindStructDecl`). Using `@builtin` in any other file is a hard compile error — this
  keeps the mechanism a closed, single-file contract, not something stdlib/user code
  can hijack.
- Bare form only: `@builtin`, always registers under the declaration's own name. (No
  aliasing support needed here — that's what §5's `@alias` fix is for, and keeping
  these two concerns separate avoids overloading one attribute with two jobs.)
- **Registration:** only `@builtin`-marked declarations are inserted into the table
  `findBuiltinDecl`/`findBuiltinType` read from. This directly replaces today's
  `findModuleMember(cxy_builtins.module, name)` unrestricted fallback in
  `lang/middle/builtins.c` — the new `findBuiltinDecl` only ever finds declarations
  that opted in.
- **Validation:** the compiler carries a small, static, C-side table
  (`lang/middle/builtins.c`, extending the existing `cxy_builtins` state) mapping
  known contract names to a minimal required shape:

  ```c
  typedef struct BuiltinContract {
      cstring name;
      BuiltinContractKind kind;      // class-with-base, or bare-function-signature
      cstring requiredBase;          // e.g. "OutputStream" for String; NULL if none
      // ... minimal required member list, checked by name/signature only,
      //     not full structural equality — enough to catch "String has no
      //     append(...)" or "someone deleted OutputStream::append", not to
      //     duplicate the type-checker.
  } BuiltinContract;
  ```

  At bind time, for every `@builtin`-marked declaration, look it up in this table and
  verify: correct declaration kind (class/struct/function), required base class
  present (e.g. `String : OutputStream`), and the minimal member surface the compiler
  itself calls directly (see §3.1 below for exactly what that surface is per type) is
  present with a compatible signature. Mismatch is a hard compile error attributed to
  the offending declaration in `builtins.cxy`, not a downstream codegen/runtime
  failure. A declaration with `@builtin` but no matching entry in the contract table
  is also an error ("`@builtin` on `Foo`, but `Foo` is not a known builtin contract") —
  this prevents the attribute from silently becoming a second unrestricted lookup path
  by typo or scope creep.
- This mechanism does **not** replace `OVERRIDABLE_BUILTINS`/`overrideBuiltin()`/
  `@[__override_builtin("name")]` (`lang/frontend/strings.h`,
  `lang/middle/builtins.c`). That mechanism answers a different question — "who is
  allowed to *supply* the implementation of this cross-file slot" (used by
  `stdlib/thread.cxy`, `stdlib/coro.cxy` to override `__thread_launch`, `timestamp`,
  etc.) — and is left untouched.

### 3.1 Concrete contract table for this migration

| Name | Kind | Required base | Required surface the compiler calls directly |
|---|---|---|---|
| `OutputStream` | class | none | `virtual func append(str: ^const void, size: u64): Optional[u64]` |
| `String` | class | `OutputStream` | default-constructible (`init()` with no required args); inherits `append` |
| `Exception` | class | none | a `string`/`String`-typed `name`-ish member and a stack-trace-push surface (matching whatever `lang/middle/sema/exception.c`'s trace-push call site expects — exact signature to be pinned down during implementation, see §7 open item) |

Nothing else in the current file gets a table entry, which means nothing else may
carry `@builtin` after this migration lands.

## 4. Native promotion: `Optional`, `Slice`, `Void`

All three are removed from `builtins.cxy` entirely (no Cxy source, no `@builtin`) and
become compiler-constructed `Type`s/behavior, consistent with how `typTuple`/
`typArray` already work (real `TTag` values with no backing source) and with
`docs/ir-migration-design.md` §6's type-lowering table, which already treats these as
compiler-defined lowered shapes.

- **`Void`** (smallest change): `typVoid` already exists for the "no value" type used
  in ordinary `void`-returning functions; what's missing is a *distinct, zero-size,
  named* type for the "ok" arm of a `void`-returning result union (`!void`'s `T | Exception`
  becomes `Void | Exception`). Add `makeVoidResultOkType(TypeTable*)` (parallel to the
  existing `makeVoidType`/`makeNullType` constructors in `lang/frontend/ttable.c`),
  producing a genuine, uniquely-named, zero-field struct-shaped `Type` with no `AstNode`
  backing it at all. Every current `type->name == S_Void && type->ns == NULL` check
  (`isVoidResultType` in `lang/frontend/types.c`, `shakeUnionDecl`/
  `makeTestFunctionSignature` path-construction in `shake.c`, the JSON-RPC plugin
  helper in `stdlib/plugins/jsonrpc.c`) is replaced with a direct type-identity check
  against this new singleton, and the AST-path-construction call sites are replaced
  with direct `Type*` construction — no name lookup left anywhere.
- **`Optional[T]`**: `typOptional` (`lang/frontend/types.h`) already exists as a real
  tag and is already used consistently for the *type* — what's compiler-managed only
  by convention today is the **value-construction/access behavior**
  (`transformOptionalSome`/`None`/`Type` in `sema/node.c`, `!!`/`*`/`&.` operator
  lowering). This migration makes the compiler construct the `ok`/`val`-equivalent
  representation and its `Some`/`None`/truthy/deref/redirect operations directly in C
  (`lang/middle/sema/*.c`), the same way binary/unary operators on primitive types are
  already synthesized without any backing Cxy struct. No behavior changes from a Cxy
  source perspective (`Some(x)`, `*opt`, `!!opt`, `opt&.field` all keep working
  identically) — only the implementation moves from "trust a struct's field layout" to
  "the compiler defines the layout." This is the most invasive item in this doc; see
  §7's phasing note.
- **`Slice[T]`**: same treatment as `Optional`, mirroring `shakeArrayType`'s current
  rewrite-to-`findBuiltinDecl(S_Slice)` path with direct `Type`/`AstNode`-free
  construction of the `(data, len)` shape and its indexing/length operators.

This item directly derisks `docs/ir-migration-design.md` §6, which already documents
`Optional`/`Slice`/`Void` as compiler-defined IR struct shapes with no source-level
backing — after this change, that's true pre-IR as well, not just post-`Lower`.

## 5. `__string` → `strview`, generalizing `@alias`

- Rename `struct __string` to `struct strview` in `builtins.cxy` (or wherever it ends
  up post-native-promotion discussions above — `strview` is a plain borrowed
  pointer+length view, not itself a candidate for native promotion, since it has real
  user-facing methods worth keeping as ordinary Cxy, same status as today).
- `@alias` **already exists** (`lang/middle/bind/bind.c`'s `getAliasName`/
  `defineDeclaration`) and already does exactly what's needed: register the same
  `AstNode*` under a second name in the binding environment. It is currently gated by
  `if (!hasFlag(node, Extern)) return NULL;` — i.e. today it only works on `extern`
  declarations (presumably originally added to let a Cxy name diverge from a C symbol
  name). **Fix:** lift that restriction so `@alias("__string")` works on an ordinary
  (non-`extern`) `struct`/type declaration too. This is a one-line gate removal in
  `getAliasName`, not new machinery.
- `struct strview { ... } @alias("__string")` (or attribute-before-declaration,
  matching whatever the parser's existing attribute-placement convention is) then
  makes both names resolve to the same declaration for all existing call sites
  (`wrapLiteralWithSuffix`'s `'s'` literal suffix in `parser.c`, `checkBinaryExpr`'s
  string-equality rewrite, `isBuiltinString`) — none of which need to change at all,
  since they look up by the string constant `S___string`, which continues to resolve.
  Only new code should use `S_strview`/write `strview` going forward.

## 6. Moving memory management and panic/assert to a C runtime

**Correction from an earlier draft of this doc:** this is *not* a pure, invisible
relocation, and deleting these declarations outright (as an earlier version of this
section said) would break the compiler. Several compiler passes construct calls to
these functions *directly*, not via macro-expanded Cxy text, and resolve the callee
through `findBuiltinDecl` (backed by `findModuleMember`/binding), which requires a
real `AstNode`/`Type*` to exist for the name:

- `lang/middle/eval/macro.c`'s `makeAssertNode` (backs the `assert!` macro) calls
  `findBuiltinDecl(S___cxy_assert)` and wires the result's `AstNode*`/`Type*` directly
  into a synthesized `CallExpr` (`csAssert0(builtinAssert)` — crashes, doesn't
  gracefully fail, if the lookup returns nothing).
- `lang/middle/simplify/simplify.c`'s `visitCastExpr` does the same for the runtime
  check inserted on every tagged-union `as` downcast.
- `panic!` itself is a **Cxy-source macro defined inside `builtins.cxy`**
  (`macro panic(msg) { __cxy_panic(file!, line!, column!, msg!) }`) — its expansion is
  ordinary Cxy text that must bind `__cxy_panic` as an identifier through normal
  scope resolution (`lang/middle/bind/scope.c`'s `findSymbol`, which itself falls back
  to `findBuiltinDecl`).
- The smart-pointer functions have the identical problem via
  `lang/middle/n2e.c`'s `n2eMakeExternReferenceToBuiltin`, called from
  `lang/middle/simplify/simplify.c`'s `visitProgram` for `S_sptr_ref`/`S_sptr_drop`/
  `S_sptr_get_trace`/`S_sptr_drop_trace` — same `findBuiltinDecl` → `csAssert0` crash
  if there's no Cxy-side declaration left to find.

**Fix:** keep a thin `pub extern func ...` prototype in `builtins.cxy` for each of
these names — exactly the pattern the same file already uses for `strlen`/`malloc`/
`write`/etc. **Only the implementation (function body) moves** to a new hand-written C
header/source pair (e.g. `lang/backend/c/src/runtime.h`/`runtime.c`, colocated with
the existing `lang/backend/c/src/prologue.h`/`epilogue.h` that `generate.c` already
`#include`s directly into every generated translation unit). This was considered and
rejected as an alternative: promote these to first-class `BackendFuncId`/
`astBackendCall` entries (`bfiAssert`/`bfiPanic`), letting `makeAssertNode`/
`visitCastExpr` construct the call directly with no `AstNode` lookup at all. That
works too, but a `bfi` is the wrong-weight tool here — `bfi`s exist for
type-dependent codegen dispatch (`bfiCopy`/`bfiDrop` pick a different function
depending on the argument's type) or raw-syntax emission (`bfiAlloca`/`bfiSizeof`),
not fixed-arity/fixed-signature calls, which a plain `extern` already models exactly.
It would also be new machinery slated for deletion anyway once
`docs/ir-migration-design.md` §11 Phase 7 removes `BACKEND_FUNC_IDS`/`astBackendCall`
entirely — a thin `extern` needs no such unwinding, since it's still exactly the same
shape (a `Call` to a fixed C symbol) after the IR migration.

- With the thin `extern` kept, no other change is needed: `findBuiltinDecl`,
  `findSymbol`, and `n2eMakeExternReferenceToBuiltin` all keep working unmodified;
  `makeAssertNode`/`visitCastExpr`/`visitProgram` need zero code changes.
- No `@builtin` needed on these prototypes, and no bind-time contract check (§3) — a
  C function prototype is already a precise, compiler-checked contract (matching how
  every other `pub extern func` in this file already works), which is a strictly
  *better* guarantee than anything `@builtin`'s shape-table offers for a plain
  function signature. `@builtin`'s contract table (§3.1) is specifically for cases
  where the compiler depends on more than a signature (a type's shape/base/behavior).
- `assert!`/`panic!` macros in `builtins.cxy` keep expanding to calls against these
  same fixed names, and keep binding correctly, since the names are still declared
  (just bodiless, `extern`) in the same module.

## 7. Migration plan / open items

1. **Land `@builtin` + contract table first**, scoped to `String`/`OutputStream`/
   `Exception` only (§3), with `Optional`/`Slice`/`Void`/memory-management/panic left
   untouched initially. This is the lowest-risk slice: it only *adds* a validation
   check and a registration filter, it doesn't change any type's runtime shape.
2. **Move memory management + panic/assert implementations to the C runtime file**
   (§6). Low-risk/mechanical, but **not** a pure deletion — keep the thin `extern`
   prototypes in `builtins.cxy` (see §6's correction) so `makeAssertNode`,
   `visitCastExpr`, and `n2eMakeExternReferenceToBuiltin` keep resolving these names
   without any code changes. Directly unblocks nothing else, so it can land
   independently and in either order relative to step 1.
3. **`strview` rename + `@alias` gate fix** (§5). Independent, small, no interaction
   with the other steps.
4. **Native-promote `Void`** (§4) — small, isolated, no other type depends on it.
5. **Native-promote `Slice`, then `Optional`** (§4) — do these last and separately;
   they're the most invasive (every current call site rewriting AST into
   `findBuiltinDecl(S_Some/S_None/S_Optional/S_Slice)` calls needs a matching C-side
   constructor instead), and should each get their own focused change + test pass
   rather than landing together.
6. **Open item — `Exception`'s exact required surface** (§3.1): needs a short spike
   during implementation to pin down precisely what `lang/middle/sema/exception.c`'s
   trace-push call site (`makeExceptionPush`/`checkCatchBinaryOperator`) structurally
   requires, so the contract table entry is neither too strict (breaking legitimate
   subclassing) nor too loose (defeating the point of validation). Note this is
   independent of — and should be resequenced relative to — the exceptions-in-`Lower`
   work already tracked in `docs/ir-migration-design.md` §8.6/§13 item 6, since that
   work changes *where* the raise/catch rewrite happens but not what shape `Exception`
   itself needs to have.
7. **Testing:** each step above should get a dedicated negative test (e.g. "a
   builtins.cxy with `String` missing `append` fails to compile with a clear error
   pointing at the missing member") in addition to the existing `tests/lang` positive
   coverage, since the whole point of this doc is to convert silent runtime/codegen
   failures into loud compile-time ones.
