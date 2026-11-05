# Instantiation Trace for Diagnostics

## Problem

Errors raised while checking a generic substitution (or a comptime macro inside one) are
reported at the location of the *implementation*, not the *use*:

```
error: type class builtins.Exception does not implement `str` operator
  in __builtins.cxy:634:13 (to 638:14)
```

The user cannot tell which `<<` / `println` / `f"..."` triggered it.

## Facts (current code)

- `Log` (`core/log.h`) is the single sink. `TypingContext.L` (sema) and `EvalContext.L` (eval) are the **same** `Log`.
  So the trace can live in `Log`; no pass needs its own copy.
- Substitution is **synchronous**: `resolveGenericDecl` (`sema/generics.c`) clones the decl, then calls
  `checkFunctionSignature`/`checkFunctionBody`/`checkType(substitute)` inline. Member functions of a generic
  struct are checked inside that `checkType` (`checkMemberFunctions`). A plain push/pop around the
  substitution is therefore a correct stack.
- Cache hit (`goi.f`) returns before any substitution: no frame, and no errors can occur there.
- `error!`/`require!` reach `staticLog` (`eval/macro.c`) → `logError`. Same path as any other error.
- Diagnostics are delivered through `DiagnosticHandler(const Diagnostic*, ctx)`; notes are already separate
  `Diagnostic`s (`dkNote`). Emitting the trace as notes needs no handler/plugin/LSP change.
- Early exit from `resolveGenericDecl` before substitution (`resolveGenericDeclError`, arg errors) happens
  before the cache check, so it needs no frame.

## Design

### Frame stack in `Log`

```c
typedef struct LogFrame {
    FileLoc loc;        // call site ("requested here")
    cstring fmt;        // e.g. "in instantiation of {t}"
    FormatArg arg;      // lazy: formatted only if an error is printed
} LogFrame;

// Log gets:
struct { LogFrame *items; u32 count, cap; } frames;
u32 maxFrameNotes;      // default 10, 0 = unlimited, option --trace-depth=N
bool traceFrames;       // default true, --no-trace-frames disables
```

```c
void logPushFrame(Log *, const FileLoc *loc, cstring fmt, FormatArg arg);
void logPopFrame(Log *);
```

- Push is a struct copy, no allocation on the hot path (amortised array growth).
  **Never format at push time**: there are thousands of instantiations and almost none fail.
- `arg` holds a `const Type *` (`.t`) or `cstring` (`.s`). Types live in the type table for the whole
  compile, so lazy formatting is safe.
- Frames with `loc.fileName == NULL` (synthesised nodes, `builtinLoc()`) are skipped at **print** time.

### Emission

In `printMessage` (`log.c`): after delivering a `dkError` or `dkWarning`, walk frames innermost → outermost and
deliver one `dkNote` per frame through the same handler, with `frame.loc`.
Notes passed to `logNote` directly do **not** trigger the trace (avoids duplicates on multi-part errors).

```
error: type class builtins.Exception does not implement `str` operator
  in __builtins.cxy:634:13 (to 638:14)
note: in instantiation of 'OutputStream.appendValue[Exception]' requested here
  in __builtins.cxy:1012:9
note: in instantiation of 'OutputStream.op__lshift[Exception]' requested here
  in jwt.cxy:88:22
```

Last note is in user code, which answers "what tried to print `ex`".

### Depth control

- Print the innermost `K = maxFrameNotes/2` and outermost `K` frames; between them one note:
  `note: ... N more instantiations skipped (use --trace-depth=0 to show all)`.
- Collapse consecutive frames with the same `(loc, arg)` (recursive generics) into
  `(repeated N times)`.

### Push sites

| Site | Frame text | Loc |
| --- | --- | --- |
| `resolveGenericDecl`, substitution branch | `in instantiation of '{t}'` with `goi.s` (applied type) | `node->loc`; for generic *functions* prefer `ctx->currentCall->loc` when set (call span, not just the callee ident) |
| macro expansion in `eval/macro.c` (user macros, `f"..."` string expr) | `in expansion of macro '{s}'` | call node loc |
| `inferGenericFunctionTypes` closure-arg checks | none (errors are already at user code) | — |

Exact scope in `resolveGenericDecl`:

```c
// after cache-hit return, before cloneGenericDeclaration
logPushFrame(ctx->L, loc, "in instantiation of '{t}'", (FormatArg){.t = goi.s});
... substitute / checkFunctionSignature / checkFunctionBody / checkType ...
logPopFrame(ctx->L);   // right before `return substitute->type`
```

There is no early `return` between those two points (error paths set `node->type` and fall through),
so one push/pop pair is enough. If one appears later, use a scope macro:

```c
#define LOG_FRAME(L, loc, fmt, arg) \
    for (bool _once = (logPushFrame(L, loc, fmt, arg), true); _once; _once = false, logPopFrame(L))
```
(`break` inside would still pop; `return` would not, so keep it for non-returning bodies.)

### Applied-type name

`goi.s` is created before `substitute` is named. `{t}` on an applied type must print `Name[Arg,...]`
(verify; if it prints the mangled/raw form, store `generic->type` and the declaration name instead).

## Pitfalls / checks

1. **Call-site loc quality.** Operator overloads (`a << b`), `f"..."` and implicit `str` calls are
   resolved through synthesised path nodes. Confirm `node->loc` is copied from the user expression;
   otherwise the innermost user-visible frame is missing. Fix at synthesis, not in the log.
2. **Deferred checking.** Anything that queues a body for later checking (`astModifierAdd`, closures,
   lazy member checks) loses the stack. Today substitutes are checked inline; if that changes, capture
   the stack snapshot at enqueue time.
3. **Error type caching.** A substitution that fails is cached as `ERROR_TYPE`; later uses fail silently
   with no trace, which is fine (only the first, real error is shown) but should be noted in tests.
4. **`maxErrors` / `errorCount`.** Notes are `dkNote`, they do not bump counters
   (`printDiagnosticToState` only counts errors and warnings).
5. **Warnings inside instantiations** (e.g. unused variable in a generic body) will also get the trace.
   For warnings only emit the trace if `--trace-warnings` is set, otherwise output gets noisy.
6. **Snapshots.** `tests/lang` error snapshots whose error is inside a generic body will gain notes.
   Review with `git diff` before `runner.sh -u`.
7. **Plugins / LSP handler.** They receive extra `dkNote` diagnostics; no ABI change.
8. **Threading.** Compilation is single-threaded per `Log`. If modules are ever checked in parallel,
   frames must move to a per-thread/context stack that `printMessage` can read.

## Status

Implemented: `Log` frames (`core/log.{h,c}`), trace notes after `logError` only (not warnings),
middle-elision at `Log.maxFrameNotes` (default 10), push/pop in `resolveGenericDecl`
(location is `node->loc`).

Not yet: CLI options (`--trace-depth`, `--no-trace-frames`), repeated-frame collapsing,
macro-expansion frames, warning traces, regression test.

## Implementation plan

1. `core/log.{h,c}`: `LogFrame`, push/pop, free in `freeLog`, emission in `printMessage`
   (error/warning only), depth collapsing, options `--trace-depth`, `--no-trace-frames`.
2. `sema/generics.c`: push/pop in `resolveGenericDecl` (the only required site).
3. Verify with a test: `tests/lang/errors/generic_instantiation_trace.cxy` (error inside a generic body,
   two levels deep) and snapshot.
4. `eval/macro.c`: macro expansion frame.
5. Optional: `--trace-warnings`.

## Non-goals

- Runtime stack traces (see `StackTrace` in `runtime/builtins.cxy`).
- Tracing non-generic inlining or simplify/lowering errors (they run on already-checked code).
