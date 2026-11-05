# LLVM Backend: finalized IR to LLVM IR, object file and executable

Status: design. Uses the LLVM **C API** (`llvm-c/*`) from C. The C backend
(`backend/c`) stays and remains the default until the LLVM backend passes the
same test suites.

## 1. Goals and non-goals

Goals
- Consume the **finalized IR** (`driver->finalizedModules`, after
  `runMemoryManagement` and `runFinalize`) and produce an executable.
- Real debug info (`DIBuilder`) instead of `#line` directives.
- Control of target triple, deployment target and pass pipeline. This removes
  the dependency on whichever `clang` is first on `PATH`.
- No change to anything upstream of the backend (sema, lowering, memory pass).

Non-goals
- Lowering the AST directly. Ownership analysis stays on the IR, which still has
  the Cxy types (see the discussion in `memory2.md`).
- Replacing `clang` as the *linker driver* or as the compiler of `@__cc` native
  C sources (`ae.c`, `tinythread.c`, `ctx.c`, ...).
- Matching the C backend's textual output.

## 2. Where it plugs in

```
 ... -> Lower -> MemoryMgmt -> Finalize -> [Codegen] -> [Compile]
                                              |            |
                          C:   emit app.c ----+            +-- clang app.c natives.c -o app
                          LLVM: build LLVMModuleRef        +-- emit app.o, link with clang
```

`driver.h` declares the backend as link-time functions
(`initCompilerBackend`, `generateCode`, `compilerBackendMakeExecutable`,
`compilerBackendExecuteTestCase`, `deinitCompilerBackend`), implemented by
exactly one backend chosen in CMake (`ENABLE_LLVM_BACKEND`).

Change: both backends are compiled in and selected at run time with
`--backend=c|llvm` (default `c`).

```c
typedef struct CompilerBackendOps {
    void *(*init)(CompilerDriver *driver, int argc, char **argv);
    AstNode *(*generate)(CompilerDriver *driver, AstNode *metadata);
    bool (*makeExecutable)(CompilerDriver *driver);
    bool (*executeTest)(CompilerDriver *driver);
    void (*deinit)(CompilerDriver *driver);
} CompilerBackendOps;
extern const CompilerBackendOps cBackendOps, llvmBackendOps;
```

`driver->backend` stays `void *`. The stages in `driver/stages.c` call through
`driver->backendOps`.

The existing `src/cxy/lang/backend/llvm/*.cpp` (3.8k lines, C++ API, written
against the AST and the old `Type->codegen` fields) is **stale**. Move it to
`backend/llvm-legacy/` (not built) for reference, in particular `debug.cpp` for
`DIBuilder` usage, and delete it once the new backend reaches parity.

## 3. Files

```
backend/llvm/
  llvm.h          public: llvmBackendOps
  context.h/.c    LlvmContext, target machine setup, type cache
  types.c         IrType -> LLVMTypeRef
  module.c        globals, function declarations, constructors, main wrapper
  function.c      function bodies: blocks, statements
  value.c         rvalue/lvalue generation, constants
  ops.c           unary, binary, cast
  call.c          calls, varargs, extern ABI classification
  raii.c          alloc, copy, drop, zeromem, move
  asm.c           inline asm
  debug.c         DIBuilder
  runtime.c       prologue bitcode loading and linking
  emit.c          verify, optimize, emit object / .ll / .bc, link
```

LLVM C headers used: `Core.h`, `Target.h`, `TargetMachine.h`, `Analysis.h`
(verify), `Linker.h`, `IRReader.h`/`BitReader.h` (runtime bitcode),
`BitWriter.h`, `DebugInfo.h`, `Transforms/PassBuilder.h` (`LLVMRunPasses`).

## 4. Context

```c
typedef struct LlvmContext {
    CompilerDriver *driver;
    LLVMContextRef ctx;
    LLVMModuleRef mod;            // one module for the whole program
    LLVMBuilderRef builder;
    LLVMTargetMachineRef tm;
    LLVMTargetDataRef dl;
    LLVMDIBuilderRef di;          // NULL unless -g
    HashTable types;              // const IrType* -> LLVMTypeRef
    HashTable strings;            // cstring -> LLVMValueRef (private constants)
    struct {                      // per function
        const IrNode *func;
        LLVMValueRef fn;
        LLVMBasicBlockRef entry;  // allocas are created here
        HashTable blocks;         // const IrNode* (BasicBlock) -> LLVMBasicBlockRef
    } cur;
} LlvmContext;
```

`IrNode.codegen` is reserved for the backend and caches the `LLVMValueRef` of
a function, global, parameter slot or variable slot.

One `LLVMModuleRef` for all finalized modules, like the single C file today.
Per-module objects (parallel build, caching) are future work; nothing in the
design prevents it because the type cache is keyed by `IrType*`.

## 5. Type lowering

C has declaration-order rules that `backend/c/typegraph.c` solves. LLVM does
not: struct types are created as *identified* (named) structs first and their
bodies are set afterwards, so recursion needs no special handling.

| `IrType` | LLVM |
|---|---|
| `Void` | `void` (return only), `i8` when a value is needed |
| `Bool` | `i1` as an SSA value, `i8` in memory (fields, allocas, globals). Params and returns are `zeroext i1`. Helpers `toI1` / `toMem` convert. |
| `Char` | `i8` |
| `I8..I128`, `U8..U128` | `iN` (signedness lives in the operations) |
| `F32`, `F64` | `float`, `double` |
| `Pointer` (any category) | `ptr` (opaque pointers) |
| `Array`, sized, unnamed | `[N x elem]` |
| `Array`, unsized or named | named struct, or `ptr` (decayed) |
| `Struct` (class struct, tuple, optional, result, slice, closure) | identified struct `%name`, fields in order |
| `Struct`, `isPacked` | packed struct |
| `UntaggedUnion` | `{ [n x iA] }`, sized and aligned to the largest member. Member access reinterprets the address. |
| `Func` | function type |
| `Opaque` | `i8`, behind a pointer it is `ptr` |
| category `Enum` | the underlying integer |

Layout rules
- Fields with `bits > 0` (only the drop-flag structs today) become one `i64`
  storage word per 64 bits, with load/mask/shift helpers. The layout is private to
  the compiler. Imported C structs with bit-fields are not supported until
  `IrField.offset` is verified to be filled from clang's layout.
- `IrType.align` (from `@align`) is applied to allocas, globals and loads and
  stores of that type, since LLVM struct types carry no alignment.
- When `IrType.size` is non-zero, a debug check asserts it equals
  `LLVMABISizeOfType(dl, ty)`. A mismatch means the layouts of the two backends
  disagree and FFI structs would break.
- `sizeof` is `LLVMSizeOf`/`LLVMABISizeOfType`, never a constant computed
  elsewhere.

## 6. Module level

- **Reachability.** Call `runReachabilityAnalysis(driver)` first and skip nodes
  without `flgReachable`, as the C backend does. `isHint` externs are skipped.
- **Globals.** One `LLVMAddGlobal` per `irGlobalVar`. External linkage by
  default (same as C). `isThreadLocal` becomes `LLVMSetThreadLocal`. The
  initializer must be a constant: `NullConst` gives `zeroinitializer`,
  `StructConst`/`ArrayConst` use `LLVMConstStruct`/`LLVMConstArray`, `GlobalRef`
  is the address constant, strings are private constants. A non-constant
  initializer is a compiler error.
- **Functions.** Declare all functions first (so calls can refer to later
  ones), then emit bodies. Cxy functions get `internal` linkage, which allows
  inlining and dead-code removal. `main`, constructors and anything with an
  explicit external `@linkage` stay external. `extern` functions are
  declarations with the attributes from `function.attrs`: `naked`, `noreturn`
  and `returns_twice`, which map to real LLVM attributes. The 'volatile' and
  'register' pieces are covered in section 12.
- **Constructors and finalizers.** `flgModuleInit` functions and the
  `__fini`/`__tfini` registration functions become entries of
  `@llvm.global_ctors` (append linkage, priority from module order). This
  replaces the `__attribute__((constructor))` text.
- **Strings.** `LLVMBuildGlobalStringPtr` equivalents, cached by content, private
  `unnamed_addr` NUL-terminated constants. UTF-8 and `\x`/octal escapes are
  decoded once into bytes, so the `printUtf8` path of the C backend is not
  needed.
- **Entry point.** Build `main(i32, ptr)` the same way `emitCModule` does:
  start the runtime, build the `{argv, argc}` slice, call the Cxy `main`, handle a
  `Result` (tag 1: take the exception, `builtins_panicOnUnhandledException`,
  drop, return 1; tag 0: return the value if it is an `i32`-like type, else 0).
  Test mode calls `builtins_runTests(args, file, tests)`.

## 7. Function bodies

IR is not SSA. Variables are mutable and expressions are trees. The standard
solution applies: every variable is an `alloca`, and `mem2reg`/SROA in the
pass pipeline does the rest.

Per function
1. Create the `entry` block and one `alloca` for each `irVarDecl` and each
   parameter. Allocas always go in the entry block (insert before its
   terminator position), never at the point of declaration, so loops don't grow
   the stack.
2. Store each incoming argument into its parameter slot. Parameters are
   addressable and assignable in Cxy.
3. Create an `LLVMBasicBlockRef` for every `irBasicBlock` up front (forward
   branches), keyed by node in `cur.blocks`.
4. Visit statements in order. A block that ends without a terminator is an IR
   bug and is reported (the C backend gets away with it).

### Expressions: two entry points

```c
LLVMValueRef genValue(LlvmContext *c, const IrNode *n);  // rvalue
LLVMValueRef genAddr(LlvmContext *c, const IrNode *n);   // address of an lvalue
```

| IR node | `genAddr` | `genValue` |
|---|---|---|
| `VarRef` | the variable's alloca | load from it |
| `GlobalRef` (var) | the global | load; function: the function pointer |
| `Member` | GEP into `genAddr(target)`. If the target type is a pointer or reference, load it first, then GEP (C `->`). | load the field |
| `Index` | GEP into array or pointer | load |
| `Deref` | `genValue(target)` | load from it |
| `AddrOf` | not an lvalue | `genAddr(target)` |
| `Alias`, `Move` | pass through | pass through |
| `Cast`, `Unary`, `Binary`, `Call` | not lvalues | see section 8 |
| constants | not lvalues | `LLVMConst*` |
| `OptionalValue`, `UnionValue`, `ClosureValue`, `StructConst`, `ArrayConst` | not lvalues | built into a temporary (see below) |

### Assignment

`irAssign` is `storeInto(addr(lhs), rhs)`. The C backend special-cases several
right-hand sides in `emitAssign`. In one recursive function they become:

| rhs | action |
|---|---|
| `OptionalValue` | store `ok`, then store `val` if present |
| `UnionValue` | store `tag`, then the payload through the member's type |
| `StructConst` | `storeInto` each field (array fields element by element) |
| `ArrayConst` | `memset` zero when shorter than the target, then store each element |
| `NullConst`, aggregate target | `store zeroinitializer` (or `memset` for large types) |
| array to array | `memcpy` |
| anything else | `store`; aggregates above a size threshold use `memcpy` from the source address |

This avoids building aggregates with `insertvalue` for the common case and
produces the straight-line stores the optimizer wants.

### Control flow

| IR | LLVM |
|---|---|
| `Branch` | `br` |
| `BranchIf` | `br i1`, the condition goes through `toI1` |
| `Switch` | `switch`. A case with `endNode` (a GNU range, `case a ... b`) is expanded to `icmp`/`br` pairs in front of the switch when the range is large, to individual cases when it is small. |
| `Return` | `ret`, or `ret void` |
| `Unreachable` | `unreachable` |
| `ScopeEnter` / `ScopeExit` | nothing. Finalized IR already contains the drops. |

## 8. Operators, casts and calls

- **Binary.** Chosen by the *operand* `IrType`: `add/sub/mul`, `sdiv/udiv`,
  `srem/urem`, `shl`, `ashr`/`lshr`, `and/or/xor`, and `icmp` with `s`/`u`
  predicates. Floats use `fadd...` and `fcmp`: `!=` is `une`, all other
  predicates are ordered (C semantics). Pointer `+`/`-` int is a `gep` over the
  pointee type (`i8` for `void *`). Pointer comparison is `icmp` on the
  pointers. If `&&`/`||` ever reach the IR they are lowered with branches and a
  `phi`. Today the lowering already expands them into blocks, so the backend
  asserts on them.
- **Unary.** negate, bitwise not, logical not (`icmp eq 0`), and the four
  increment/decrement forms as load, add, store, with the old or new value as
  the result.
- **Cast.** By source and target tags: `trunc`/`zext`/`sext` (by *source*
  signedness), `sitofp`/`uitofp`, `fptosi`/`fptoui`, `fpext`/`fptrunc`,
  `ptrtoint`/`inttoptr`, and no-op for pointer to pointer (class up/down casts
  are pointer casts). Array to pointer decays to a GEP `0,0`. To `Bool` is
  `icmp ne 0`.
- **Calls.** A callee `GlobalRef` to a function is a direct call. Anything else
  is an indirect call using the function type from `callee->type`.
  Closure calls are already lowered into `.fn(env, ...)` by the IR lowering.
- **C varargs.** For variadic externs the backend applies the default argument
  promotions: `float` to `double`, integers narrower than `int` to `i32`.
- **Intrinsics by name.** The IR calls `__atomic_*` builtins by name. In C,
  clang knows them. Here the backend recognises them and emits `load atomic`,
  `store atomic`, `atomicrmw`, `cmpxchg` and `fence`, reading the ordering from
  the constant argument (a non-constant ordering becomes `seq_cst`). `memcpy`,
  `memset` and `memmove` become `llvm.mem*` intrinsics. `irAlloca` becomes a
  dynamic `alloca`.

### ABI

LLVM does not implement the C calling convention for aggregates. Two cases:

1. **Cxy to Cxy calls** use our own convention. First cut: pass and return
   aggregates as first-class LLVM values. They are split into scalars by the
   backend. If that proves slow for large `Result`/optional types, switch to
   passing aggregates above 16 bytes by pointer to a caller-owned temporary
   (the memory pass already makes by-value copies) and returning them through a
   hidden `sret` pointer.
2. **Calls to `extern` C functions** with struct arguments or results need
   the real C ABI. First cut: a small classifier for x86-64 SysV and AArch64
   (Apple and AAPCS64). A struct of at most 16 bytes is coerced to one or two
   integer registers, a larger one is passed indirectly (`byval`/`sret`), and
   AArch64 homogeneous float aggregates are passed as float registers. A
   combination the classifier doesn't know is a compile error that names the
   function, never silently wrong code.

## 9. RAII operations

The finalized IR holds `Alloc`, `Copy`, `Drop`, `Zeromem` and `Move`. The C
backend expands them inline per type category (`emitDrop`, `emitCopy`,
`emitAlloc`). The LLVM backend implements the same table in `raii.c`. Runtime
calls go to the functions from the prologue (section 10).

| Op | Category | Emitted |
|---|---|---|
| `Alloc` | class | `call __smart_ptr_alloc(sizeof(T), dctorFwd or dctor or null)` (`_trace` variant with file and line when memory trace is on) |
| `Drop` | class pointer | `call __smart_ptr_drop(ptr)` |
| | struct, tuple, union | call `strct.dctor` with the address |
| | array | call `array.dctor` with `{ptr, len}` |
| | optional | `if (x.ok)` then drop of `x.val` by the element's category |
| | result | tag 1: drop `val._1`. Tag 0: drop `val._0` if destructible |
| | closure | `if (x.dtor) x.dtor(x.env)` |
| | with `flag` | wrapped in `if (flag)` |
| `Copy` | class | `dst = __smart_ptr_get(src)` |
| | struct, tuple, union | `dst = strct.copy(&src)`, else a plain load/store |
| | optional | copy `ok`, then the element copy under `if (ok)` |
| | result | copy `tag`, then the payload per tag |
| | closure | copy, then `if (dtor) __smart_ptr_get(env)` |
| `Zeromem` | any | `memset` (a pointer target zeroes the pointee) |
| `Move` | any | the operand, as in C (ownership was settled by the memory pass) |

Reference-category sources (`irTypeCatReference`) are loaded through before the
copy, as `emitCopy` does by stripping pointers.

## 10. The runtime (`runtime/prologue.h`)

`prologue.h` is plain C that is pasted into the generated file today. It holds
the smart-pointer functions, panic and assert, call-trace helpers, finalizer
registries, hashing and byte swaps, and the memory-trace hooks. It stays C and is
**compiled once, at build time, to LLVM bitcode** and embedded in the compiler.

Build (CMake)
- A `cxy_runtime.c` wrapper includes `prologue.h`. A custom command compiles it with
  the clang from `find_package(Clang)`: `clang -O2 -emit-llvm -c`.
- Four variants: `NDEBUG` on/off times `ENABLE_MEMORY_TRACE` on/off. They are
  embedded with the existing `amalgamate` tool, like `CXY_PROLOGUE_SOURCE`.
- Functions that are `static inline` today must be emitted with external
  definitions in the bitcode, otherwise nothing refers to them. A
  `CXY_RT_FN` macro in `prologue.h` expands to `static inline` for the C
  backend and to an external, `used` definition for the bitcode build.
- Macros that the IR relies on (`__cxy_start_runtime`, `__cxy_report_runtime`)
  become real functions, since macros are not visible from IR. `likely`,
  `unlikely`, `unreachable` and `INT128` are only used inside the runtime.

Link (`runtime.c`)
1. After the program module is complete, parse the variant for the current
   options with `LLVMParseBitcodeInContext2`.
2. `LLVMLinkModules2(program, runtime)`.
3. Mark every function and global that came from the runtime `internal`
   (except `__cxy_run_thread_finis`, which `thread.c` references). Now the
   whole program is one module, so `__smart_ptr_get`/`drop` inline into
   generated code exactly as they do in the C backend, and unused ones are
   removed.

If a Cxy program is built as a plugin or library, the runtime is linked as a
normal object instead.

## 11. Debug info (`debug.c`)

Enabled by `-g`.
- One `DICompileUnit` and `DIFile` per source file name seen in `IrNode.loc`.
- One `DISubprogram` per emitted function, attached with `LLVMSetSubprogram`.
- `DILocalVariable` plus a debug declare for each `irVarDecl` (uses
  `varDecl.display` as the name, not the uniquified C name) and each parameter.
- Every instruction gets `LLVMSetCurrentDebugLocation2(builder, line, col,
  scope, NULL)` from `node->loc`. Compiler temporaries (`flgTemporary`) get the
  location of the statement that created them.
- Types: `DIBasicType`, pointer, array, struct (members from `IrField`, name
  from `IrType.display`), with class pointers shown as pointer-to-struct.
- `LLVMDIBuilderFinalize` before verification.

Reference implementation: `backend/llvm-legacy/debug.cpp` (C++ API, same
concepts).

## 12. Assembly, coroutines and register variables

- **Inline asm.** The Cxy asm text already uses LLVM's `$0` / `$$` syntax, so it is
  passed to `LLVMGetInlineAsm` unchanged (the C backend rewrites it to `%0`). The
  constraint string is built as outputs first (`=` prefixed), then inputs, then
  clobbers (`"memory"` becomes `~{memory}`, `"cc"` becomes `~{flags}`,
  register names become `~{x0}`). Output operands are results of the call and
  are stored to their lvalues afterwards.
- **`@register("x0") var`** (GNU register variables) has no LLVM equivalent.
  The coroutine macros that use it are replaced for this backend by the naked
  `setjmp`/`longjmp` in `native/coro/ctx.c`, which exists already and is marked
  `returns_twice`. `returns_twice` is a first-class attribute in LLVM, which makes
  this *more* robust than the C path.
- **Stack switching.** The spawn macro switches `sp` and keeps running in the
  same function. LLVM, like clang, may address locals relative to the old stack
  after that. Mitigation: start the child through a separate trampoline
  function (also in `ctx.c`) and not inline in the spawning function.

## 13. Optimization, verification and emission (`emit.c`)

1. Set the triple and data layout (section 14).
2. Verify (`LLVMVerifyModule`, abort-on-failure with the message) before
   optimization in debug builds and with `--verify-llvm`. A verification failure
   is a compiler bug and prints the offending function.
3. Optimize with the new pass manager: `LLVMRunPasses(mod, "default<O2>", tm,
   opts)`. The level comes from `options.optimizationLevel` (`O0` runs no
   passes except the always-inliner).
4. Emit:
   - object file: `LLVMTargetMachineEmitToFile(tm, mod, path, LLVMObjectFile)`
   - `--emit-llvm` writes `.ll` (`LLVMPrintModuleToFile`) or `.bc`
     (`LLVMWriteBitcodeToFile`). These are the artifacts the tests compare.

## 14. Target, triple and deployment target

- Triple: `LLVMGetDefaultTargetTriple()` unless `--target` is given. On macOS,
  the version suffix comes from `MACOSX_DEPLOYMENT_TARGET` or an option, so
  the object is stamped with a version you chose and not whichever SDK the
  invoked `clang` assumes. This removes the "built for newer macOS than being
  linked" mismatch.
- CPU and features: generic by default, host with `--native`
  (`LLVMGetHostCPUName`, `LLVMGetHostCPUFeatures`).
- Relocation model PIC, code model default, optimization level mapped from the
  option.
- `LLVMCreateTargetDataLayout` gives the data layout, which is set on the module
  and used for every size query.

## 15. Building the executable

The compile step keeps the shape of `compilerBackendMakeExecutable`. After the
object file is emitted, link with the system compiler driver:

```
clang <obj> <native .c sources> <cflags/defines/-I> <-L/-l/-framework> -o <out>
```

- Native sources (`driver->nativeSources`, from `@__cc` / `__cc`) are still
  compiled by clang here. They could be compiled to cached objects later.
- `linkLibraries`, `-lcxy-trace` when memory trace is on, library and framework
  paths: same as today.
- The C compiler is chosen from an option or the environment, not from `PATH`
  order. The driver prints the exact command on failure.
- Test mode (`cmdTest`) and `compilerBackendExecuteTestCase` behave the same as
  with the C backend: build, run the binary, compare.

## 16. CMake

- Remove the `ENABLE_LLVM_BACKEND` switch that replaces the C sources. Both
  backends are always built. LLVM libraries stay linked, since `cxy-core`
  already needs them for the C import code.
- Add LLVM components: `Core`, `Support`, `Target`, `TargetParser`,
  `TargetInfo`, `Analysis`, `Linker`, `BitReader`, `BitWriter`, `IRReader`,
  `Passes`, `ScalarOpts`, `InstCombine`, `TransformUtils`, `CodeGen`, `MC`,
  plus the all-targets list already used. Debug info needs no extra component.
- New custom commands that build and embed the four runtime bitcode variants.

## 17. Testing

- **Differential tests.** Run the existing suites with `--backend=llvm` and
  compare to the same expected outputs: `tests/lang/mem` (asan and execute
  configs), the stdlib specs (`coro.spec.cxy`, json, ...) and `tests/package`.
  The C backend is the oracle.
- **IR snapshots.** `--emit-llvm` text is checked with `FileCheck`, which the
  repository already builds. Check structure (types, drops, flags), not exact
  names.
- **Verifier on in CI.** Every test run with the LLVM backend verifies the
  module.
- **ABI tests.** Small programs that call libc and libpq-style functions taking
  and returning structs, on both supported architectures.

## 18. Milestones

1. Backend ops table, `--backend`, module, context, target machine, empty
   `main` links and runs. Runtime bitcode embedded and linked.
2. Scalars, functions, control flow, calls, strings, globals. "Hello world".
3. Aggregates: structs, tuples, arrays, slices, optional, union, result,
   closure values and the assignment table.
4. RAII: alloc, copy, drop, zeromem per category. `tests/lang/mem` passes.
5. Extern ABI, varargs, atomics, thread locals, constructors and finalizers,
   the full `main` wrapper, test mode.
6. Inline asm, coroutines through `ctx.c`, `coro.spec.cxy` passes.
7. Debug info.
8. Pass pipeline tuning, `--emit-llvm`, `--native`, compile-time comparison.
9. Parity on every suite. Decide on the default backend.

## 19. Risks and open questions

- **C ABI for extern aggregates** (section 8) is the largest risk. The
  classifier covers two architectures, and anything else errors out.
- **Struct layout agreement.** Imported C structs must have the layout clang
  gives them. Check `IrField.offset` and `IrType.size` population; if
  they are not filled from clang's record layout, use explicit padding built from
  them.
- **Bool representation.** `i1`/`i8` conversions are a common source of bugs
  with C functions. Covered by the ABI tests.
- **First-class aggregate arguments** may be slow for large types. Revisit
  after milestone 3 with measurements.
- **Coroutine stack switching** (section 12) depends on keeping the child on a
  separate function. Verify under `-O2` on both architectures.
- **Runtime variants.** Four bitcode blobs are built and embedded. If that
  proves heavy, move `NDEBUG` and memory trace into runtime flags.
- **Warnings parity.** The C backend benefits from clang's diagnostics on
  generated C. The IR verifier is the replacement. Keep it on in development.
- **Two backends.** Every new IR node tag needs a case in both. A shared test
  that walks `CXY_IR_TAGS` and fails when a backend lacks a handler would keep
  them in sync.
