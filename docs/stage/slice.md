# Slice Expressions (`.[start..end]`) & Slicing Operator Overload (`[..]`)

## 1. Executive Summary & Motivation

In Cxy, a **Slice** (`[T]`) is a non-owning, borrowed view representing a contiguous sequence of elements stored elsewhere in memory (`{ ptr: ^T, len: u64 }`).

Currently, creating a slice from an array or extracting sub-slices requires awkward manual pointer offsets or explicit type coercion. This document specifies:
1. **Slice Expressions**: First-class indexing syntax `arr.[start..end]`, `arr.[start..]`, `arr.[..end]`, and `arr.[..]`.
2. **Operator Overload `func [..]`**: A custom overload allowing user-defined collections (e.g. `String`, `Vector[T]`, `Buffer`) to define slicing semantics.
3. **Built-in `slice!(ptr, len)` Macro**: Direct low-level synthesis of slice instances.

---

## 2. Syntax Specification & Variations

### 2.1 Grammar
```ebnf
SliceExpr   ::= PrimaryExpr '.[' RangeExpr ']'
RangeExpr   ::= [ Expression ] '..' [ Expression ] [ ':' Expression ]
```

### 2.2 Syntax Forms
| Syntax | Semantics | Length |
|---|---|---|
| `arr.[start..end]` | Subsequence from index `start` up to index `end - 1` | `end - start` |
| `arr.[start..]` | Subsequence from index `start` to sequence end | `len - start` |
| `arr.[..end]` | Subsequence from index `0` up to index `end - 1` | `end` |
| `arr.[..]` | Full slice over the entire sequence | `len` |

---

## 3. Custom Operator Overload: `func [..]`

User-defined types can overload the slicing operator by declaring `func [..]`:

```cxy
pub class Vector[T] {
    _data: ^T
    _size: u64

    // Slicing operator overload
    pub func `[..]`(start: u64 = 0, end: u64? = null): [T] {
        var e = end ?: this._size
        if (start > e || e > this._size)
            panic("slice bounds out of range")
        return slice!(ptroff!(this._data + start), e - start)
    }
}

// Usage
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
