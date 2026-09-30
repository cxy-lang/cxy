# Custom Iterators & `iterator` Keyword Design

## 1. Overview
This document specifies the design and implementation of custom iterators in Cxy, including:
1. **Iterator Protocol**: The `..` (range) operator on collections producing an iterator object that implements `()` (step/yield) and `!!` (truthiness/continuation).
2. **`iterator` Keyword**: Syntax for defining inline anonymous iterator structs with captured constructor fields and member operators.
3. **`for` Loop Desugaring**: Compiler transformation of `for v in collection` and `for a, b, ... in collection` into stack-allocated while loops with optional redirects (`&.`) and member aliases.

---

## 2. Iterator Protocol

An iterator in Cxy is any struct or class type tagged with `flgIterator` (or structurally satisfying the iterator contract) that implements:
- `func `()`(): T` or `func `()`(): T?` (step function advancing state and returning current value).
- `func `!!`(): bool` (truthiness operator indicating if iteration can continue).

### Collection Integration
Collections implement the range operator overload `func `..`()` to return an iterator instance:

```cxy
iterator MyCollectionIterator[T] {
    data: ^T
    size: u64
    idx: u64 = 0

    func `()`(): T? {
        if idx < size
            return data.[idx++]
        return null
    }

    func `!!`(): bool {
        return idx < size
    }
}

struct MyCollection[T] {
    data: ^T
    size: u64

    func `..`() {
        return MyCollectionIterator[T](data, size)
    }

    const func `..`() {
        return MyCollectionIterator[const T](data, size)
    }
}
```

---

## 3. Inline `iterator` Syntax

The `iterator` keyword allows constructing anonymous iterator instances inline without declaring dedicated named structs:

```cxy
struct MyCustomType {
    s: string

    func `..`() {
        return iterator[(char, i32)](s, 0.i32) {
            `()`: {
                if i < s.len {
                    return (s.[i], i++)
                }
                return null
            }
            `!!`: i < s.len
        }
    }
}
```

### Grammar
```ebnf
IteratorExpr ::= 'iterator' '[' Type ']' '(' ArgumentList? ')' '{' IteratorBody '}'
IteratorBody ::= IteratorMember*
IteratorMember ::= ('`()`' | 'next') ':' (BlockStmt | Expr)
                 | '`!!`' ':' Expr
```

### Compiler Synthesis
The compiler transforms `iterator[T](arg0, arg1) { ... }` into:
1. An anonymous struct type marked with `flgIterator` and `flgAnonymous`.
2. Struct fields generated from the constructor arguments.
3. Method `opCall` (`func `()`(): T` or `T?`) from the `()` or `next` body.
4. Method `opTruthy` (`func `!!`(): bool`) from the `!!` body.
5. Direct instantiation returning the synthesized struct value.

---

## 4. `for` Loop Desugaring

During the semantic analysis phase (`src/cxy/lang/middle/sema/for.c`), when `node->forStmt.range` is not a primitive range/array/slice, the compiler searches for custom range operator `..` on `range->type`.

### Single Variable Iteration (`for val in collection`)

```cxy
for val in collection {
    // body
}
```
**Desugared to:**
```cxy
var __it = collection.`..`()
while !!__it {
    var __item = __it()
    var val = *__item // or __item if returning bare T
    // body
}
```

### Tuple / Destructuring Iteration (`for x, i in collection`)

```cxy
for x, i in collection {
    // body
}
```
**Desugared to:**
```cxy
var __it = collection.`..`()
while !!__it {
    var __item = __it()
    alias x = __item&.0
    alias i = __item&.1
    // body
}
```
*(Uses redirect operator `&.` on optional `__item` to read members safely with zero copying).*

---

## 5. Task Breakdown

### Task 1: Lexer & AST Definitions
- [ ] Add `tokIterator` keyword in `src/cxy/lang/frontend/lexer.h` & `lexer.c`.
- [ ] Add `astIteratorExpr` tag and `iteratorExpr` AST struct in `src/cxy/lang/frontend/ast.h`.
- [ ] Add AST constructor `makeIteratorExpr` and AST node allocator in `ast.c`.
- [ ] Add `astIteratorExpr` to AST visitors and dumpers (`visitor.c`, `dump.c`, `sexp.c`, `to_cxy.c`).

### Task 2: Parser Support
- [ ] Parse `iterator[Type](args) { members }` in `src/cxy/lang/frontend/parser.c`.
- [ ] Ensure `func `..`()` parses cleanly as the collection range overload.

### Task 3: Sema Struct Synthesis & Validation
- [ ] In `src/cxy/lang/middle/sema/check.c` (or dedicated `iterator.c`), evaluate `astIteratorExpr`.
- [ ] Synthesize an anonymous struct type carrying `flgIterator`.
- [ ] Generate fields from initialization arguments and compile methods for `()` and `!!`.
- [ ] Type-check the synthesized struct and constructor call.

### Task 4: For Loop Desugaring Refactor
- [ ] Update `findCustomRange` in `src/cxy/lang/middle/sema/for.c` to look up operator `..` and validate return type has `()` and `!!`.
- [ ] Rewrite `transformForCustomRange` in `for.c` to emit `astWhileStmt` with `!!__it` condition, stepping via `__it()`, and injecting member aliases (`&.0`, `&.1`, ...).

### Task 5: Middle-End & Backend Integration
- [ ] Verify `astAliasExpr` handling for optional redirect member access in `lower.c`.
- [ ] Verify RAII and cleanup for iterator structs that have managed members in `manage.c`.

### Task 6: Tests & Validation
- [ ] Add unit tests for named struct iterators in `tests/lang/mem/` and `tests/lang/types/`.
- [ ] Add tests for inline `iterator[T]` expressions.
- [ ] Add tests for destructuring iterations `for a, b in customRange`.
var vec = Vector[i32]()
var sub = vec.[1..4]  // Desugars to vec.`[..]`(1, Some(4))
var tail = vec.[2..]  // Desugars to vec.`[..]`(2, None[u64]())
```

### Requirements for `func [..]`:
* **Operator Identifier**: `S_SliceOverload` / `opSliceOverload` (`"[..]"`).
* **Parameters**:
  * First parameter: `start: Integral` (defaults to `0`).
  * Second parameter: `end: Integral? = null` (defaults to sequence length/null).
* **Return Type**: Slice type `[T]` or user-defined slice-like container.

---

## 4. Built-in Macro: `slice!` (`bfiSlice`)

The `slice!(ptr, len)` compiler macro constructs a slice value directly from a pointer and a length:

```cxy
var rawPtr: ^i32 = alloc[i32](10)
var s: [i32] = slice!(rawPtr, 10)
```

* **Macro Name**: `slice!(ptr, len)`
* **Backend Function**: `bfiSlice`
* **Lowering**: Emits an `irStructConst` of type `Slice_T` with fields `{ ptr, len }`.

---

## 5. Compiler Pipeline Transformations

```
Source Code: arr.[start..end]
     │
     ▼
[Parser: parser.c] ──────► astSliceExpr { target, range }
     │
     ▼
[Shake & Bind] ──────────► Resolve symbols in target, start, and end
     │
     ▼
[Sema: check.c / array.c]
     │
     ├─► Built-in ([T, N] / [T]): Typecheck bounds & return [T]
     └─► Custom Type: Rewrite to method call target.`[..]`(start, end)
     │
     ▼
[Lower: lower.c] ────────► Lower to Slice_T { ptr: target + start, len: end - start }
     │
     ▼
[Manage: manage.c] ──────► Slices are borrowed views (no drops/ref-counts emitted)
     │
     ▼
[Backend: generate.c] ───► (Slice_T){ .ptr = target + start, .len = end - start }
```

### 5.1 AST & Tokens (`ast.h`, `ast.c`, `strings.h`)
* Add `astSliceExpr` to `AST_EXP_TAGS`:
  ```c
  struct {
      AstNode *target; // array, slice, string, or object
      AstNode *range;  // astRangeExpr (start, end, step)
  } sliceExpr;
  ```
* Add `opSliceOverload` to `AST_OVERLOAD_ONLY_OPS` (`"[..]"`).
* Add `bfiSlice` to `BACKEND_FUNC_IDS`.

### 5.2 Parser (`parser.c`)
* In `parsePrimaryExpr`:
  * If `.['` is encountered:
    * Parse `RangeExpr` (optional `start`, `..`, optional `end`, optional `:step`).
    * Consume `']'`.
    * Return `makeSliceExpr(pool, loc, target, range)`.
* In `parseFunctionDecl`:
  * Recognize `func [..]` as `opSliceOverload`.

### 5.3 Semantic Analysis (`sema`)
1. **Target Evaluation**: Check type `targetType = checkType(visitor, node->sliceExpr.target)`.
2. **Path A: Built-in Slices (`[T, N]` or `[T]`)**:
   * Evaluate `start = range.start ? checkType(start) : 0`.
   * Evaluate `end = range.end ? checkType(end) : (isFixedArray ? N : target.len)`.
   * Ensure `start` and `end` are integral.
   * Result type: `makeSliceType(ctx->types, elemType)`.
3. **Path B: Custom Type Overload**:
   * Look up `opSliceOverload` in `targetType`.
   * If found, transform `node` into a method call `target.[..](start, end)`.

### 5.4 Middle IR Lowering (`lower.c`)
* For built-in fixed arrays `[T, N]` and slices `[T]`:
  * Calculate pointer:
    * Fixed array: `makeIrAddrOf(&target[start])`.
    * Existing slice: `makeIrBinary(target.ptr, opAdd, start)`.
  * Calculate length:
    * `makeIrBinary(end, opSub, start)`.
  * Result: `makeIrStructConst` with fields `{ ptr, len }` of type `Slice_T`.

### 5.5 Memory Management & RAII (`manage.c`)
* Slices are non-owning views (`flgSlice`).
* Slicing operations borrow underlying storage and never duplicate or destroy elements.
* Slices do not require drop flags or scope-exit drops.

---

## 6. Implementation Roadmap

| Task | Component | Scope |
|---|---|---|
| **Task 1** | AST, Tokens & Strings | Define `astSliceExpr`, `opSliceOverload`, `S_SliceOverload`, and `bfiSlice`. |
| **Task 2** | Parser | Implement `.[' [start] '..' [end] ']'` and `func [..]` parsing. |
| **Task 3** | Macro & Intrinsic | Implement `slice!(ptr, len)` in `macro.c` and `lower.c`. |
| **Task 4** | Semantic Analysis | Implement `checkSliceExpr` with built-in bounds resolution and `opSliceOverload` dispatch. |
| **Task 5** | IR Lowering & Codegen | Lower built-in slice expressions to `Slice_T { ptr, len }`. |
| **Task 6** | Validation & Tests | Add tests in `tests/lang/mem/` verifying array slicing, slice reslicing, and custom operator overloads. |
