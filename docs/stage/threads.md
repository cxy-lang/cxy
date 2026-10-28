# Design Document: First-Class `launch` Construct for Threads

## 1. Overview
The `launch` construct provides native language support for launching OS threads in Cxy.
Instead of exposing internal closure layout hacks (`fn.0`, `fn.1` or raw `^void` casts) to user code, thread creation is represented as a first-class AST construct (`astLaunch`) handled through sema, IR lowering, and backend code generation.

---

## 2. Syntax & Grammar

```cxy
// Basic thread launch
var t = launch {
    println("Hello from worker thread")
}

// Named thread launch
var worker = launch "worker-1" {
    doWork()
}
```

---

## 3. AST Representation

Add `astLaunch` to `AstTag` (`src/cxy/lang/frontend/ast.h`):

```c
struct {
    AstNode *name;    // Optional thread name (e.g. StringLit or expression)
    AstNode *closure; // Closure expression or synthesized block closure
} launch;
```

---

## 4. Frontend & Semantic Analysis (`sema`)

1. **`__Thread` Builtin Override**:
   - The language provides a builtin type hook `__Thread`.
   - `stdlib/thread.cxy` decorates the `Thread` struct with:
     ```cxy
     @[__override_builtin("__Thread")]
     pub struct Thread {
         - _handle: Handle
         // ...
     }
     ```
   - Semantic analysis validates that `__Thread` has been registered/overridden before typing any `launch` expression. If `__Thread` is missing, sema emits:
     `error: 'launch' requires the '__Thread' builtin type to be defined (import 'stdlib/thread.cxy')`.
2. **Closure Synthesis**:
   - The block passed to `launch { ... }` is parsed/synthesized into an escaping closure: `func() -> i32` or `func() -> void`.
   - Captures are validated with escaping semantics (`@escaping`).
3. **Type Resolution**:
   - `astLaunch` resolves to the overridden `__Thread` type (or `!__Thread` if thread creation can fail).
4. **Validation**:
   - Thread closures must take zero arguments. Captured values form the environment.

---

## 5. IR Lowering & Backend Codegen

### The C ABI Impedance Mismatch
- POSIX / C11 thread creation APIs (`thrd_create`, `pthread_create`) expect a function pointer matching:
  ```c
  int (*start_routine)(void *arg)
  ```
- In Cxy, closures are lowered to a 3-field struct:
  ```c
  struct Closure_XXXX {
      void *env;
      int32_t (*fn)(void *);
      void (*dtor)(void *);
  };
  ```
- Because a closure is a 24-byte struct rather than a single function pointer, passing it directly to `thrd_create` violates the C ABI.

### Trampoline Generation
For each `astLaunch` site or closure signature, the backend generates a static trampoline:

```c
static int __cxy_thread_trampoline_XXXX(void *arg) {
    Closure_XXXX *cl = (Closure_XXXX *)arg;
    int32_t res = 0;
    if (cl->fn) {
        res = cl->fn(cl->env);
    }
    if (cl->dtor) {
        cl->dtor(cl->env);
    }
    free(cl);
    return res;
}
```

### Launch Site Lowering
At the `astLaunch` expression site:
1. Allocate heap space for the closure:
   ```c
   Closure_XXXX *cl_mem = (Closure_XXXX *)malloc(sizeof(Closure_XXXX));
   *cl_mem = <evaluated_closure>;
   ```
2. Call `tinyThread.thrd_create`:
   ```c
   tinyThread.thrd_t handle;
   int status = tinyThread.thrd_create(&handle, __cxy_thread_trampoline_XXXX, (void *)cl_mem);
   ```
3. Wrap and return `Thread(handle)`.

---

## 6. Advantages
- **Safety**: Completely avoids fragile manual struct indexing (`fn.0`, `fn.1`) and unsafe pointer-to-closure casts.
- **Ergonomics**: Clean, expressive syntax (`launch { ... }`).
- **Portability**: Platform-specific thread runtime calls (`thrd_create`, `pthread_create`, Windows `CreateThread`) are isolated to backend lowering.
