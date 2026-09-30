# Definite Initialization & Drop Elaboration via Dataflow Analysis

This document specifies the design and implementation of **Dataflow-Based Definite Initialization & Drop Elaboration** for the Cxy compiler middle-end (`cxy/src/cxy/lang/middle/memory/manage.c`).

---

## 1. Background & Motivation

In Cxy, resources such as classes (`irTypeCatClass`), destructible structs, tuples, and tagged unions (`flgReferenceMembers`) are subject to automatic RAII teardown (`drop`) upon exiting their lexical scopes.

When control flow contains branches (`if`/`else`, `match`, loops, `break`/`continue`, early `return`), the initialization or moved state of a variable can be:
1. **Definite Initialization**: Initialized along all paths leading to a scope exit $\implies$ static `drop x`.
2. **Definite Moved / Uninitialized**: Moved or uninitialized along all paths leading to a scope exit $\implies$ no drop emitted (dead drop elimination).
3. **Conditional (Ambiguous) State**: Initialized along some paths, but moved or uninitialized along others $\implies$ conditional drop flag (`drop-if x, __drop_flags.x`).

Ad-hoc block-scanning heuristics fail to accurately classify state across complex CFGs (nested loops, unwinding, match cases). Standard **Forward May/Must Initialization Dataflow Analysis** provides optimal, mathematically sound drop elaboration.

---

## 2. Dataflow Analysis Framework

The analysis operates on the **Control Flow Graph (CFG)** of each `irFunction`.

### 2.1 Domain & Bit-Vectors
For a function with $N$ tracked destructible local variables ($0 \le v < N$), state at any program point is represented using bitsets:

- **`MAY_INIT`**: Bit $v$ is 1 if variable $v$ is initialized along **at least one** incoming path.
- **`MUST_INIT`**: Bit $v$ is 1 if variable $v$ is initialized along **all** incoming paths.

Bitsets use standard 64-bit integer words:
```c
typedef struct BitSet {
    u64 *words;
    u64 numBits;
} BitSet;
```

---

### 2.2 Intra-Block Gen and Kill Sets

For each basic block $B$:
- **`Gen[B]`**: Variables that are assigned in $B$ and not subsequently moved in $B$.
- **`Kill[B]`**: Variables that are moved (`irMove`) or dropped in $B$ and not subsequently reassigned in $B$.

#### Statement Transfer Inside Block:
Traversing each statement `s` in basic block `B` forward:
1. **Assignment (`v = expr` or `copy expr -> v`)**:
   - `Gen = Gen | {v}`
   - `Kill = Kill & ~{v}`
2. **Move (`... = &&v` or `move v`)**:
   - `Kill = Kill | {v}`
   - `Gen = Gen & ~{v}`

---

### 2.3 Inter-Block Dataflow Equations

Let `Pred(B)` be the list of predecessor basic blocks of `B`.

#### Block Entry:
- `MayIn[B] = Union of MayOut[P] for all P in Pred(B)` (Bitwise OR)
- `MustIn[B] = Intersection of MustOut[P] for all P in Pred(B)` (Bitwise AND)

*Boundary Condition* for function entry block `B_entry`:
- Function parameters: `MayIn[B_entry] = MustIn[B_entry] = {params}`
- Local variables: `MayIn[B_entry] = MustIn[B_entry] = {}` (all bits 0)

#### Block Exit:
- `MayOut[B] = (MayIn[B] & ~Kill[B]) | Gen[B]`
- `MustOut[B] = (MustIn[B] & ~Kill[B]) | Gen[B]`

#### Convergence:
Iterate over all basic blocks in reverse postorder until neither `MayOut[B]` nor `MustOut[B]` changes for any block (fixed-point reached).

---

## 3. Drop Elaboration Rules

At each scope exit site (`irScopeExit` or function exit `_L_exit`) for variable $v$:

| `MAY_INIT(v)` | `MUST_INIT(v)` | State Classification | Emitted IR Instruction |
|:---:|:---:|:---|:---|
| `1` | `1` | **Definitely Initialized** | `drop v` (Static drop, no flag) |
| `0` | `0` | **Definitely Moved / Uninit** | *None* (Dead drop eliminated) |
| `1` | `0` | **Conditionally Initialized** | `drop-if v, __drop_flags.v` |
| `0` | `1` | *Invalid / Unreachable* | *None* |

### Drop Flag Synthesis:
A variable $v$ is allocated a bit in the synthetic `__drop_flags` struct if and only if there exists **at least one** scope exit or assignment point where:
$$\text{MAY\_INIT}(v) == 1 \quad \land \quad \text{MUST\_INIT}(v) == 0$$

---

## 4. Concrete Examples

### Example 1: Straight-Line Move (Zero Overhead)
```cxy
func testLinearMove() {
    var x = Awesome()
    var z = &&x
}
```
**Dataflow Analysis:**
- Entry: `MayIn={}`, `MustIn={}`
- After `x = Awesome()`: `May={x}`, `Must={x}`
- After `z = &&x`: `May={z}`, `Must={z}` (`x` was killed)
- Exit:
  - For `x`: `May=0`, `Must=0` $\implies$ **No drop emitted**.
  - For `z`: `May=1`, `Must=1` $\implies$ **Static `drop z` emitted**.
- Drop flag struct: **Not synthesized (0 bytes overhead)**.

---

### Example 2: Conditional Move in Branch
```cxy
func testConditionalMove(cond: bool) {
    var x = Awesome()
    if cond {
        var z = &&x
    }
}
```
**CFG & Dataflow:**
- `_L_entry`:
  - `x = Awesome()` $\implies$ `MayOut={x}`, `MustOut={x}`
- `_L_IF0_then` (Branch taken):
  - `z = &&x` $\implies$ `Kill={x}`, `Gen={z}`
  - Exit `_L_IF0_then`: `MayOut={z}`, `MustOut={z}`
- `_L_IF0_end` (Merge block):
  - $\text{MayIn} = \text{MayOut}(\text{entry}) \cup \text{MayOut}(\text{then}) = \{x, z\}$
  - $\text{MustIn} = \text{MustOut}(\text{entry}) \cap \text{MustOut}(\text{then}) = \emptyset$
- At `_L_exit`:
  - For `x`: `May=1`, `Must=0` $\implies$ **`drop-if x, __drop_flags.x`**
  - Synthesizes `__drop_flags.x`:
    - `_L_entry`: `__drop_flags.x = 1`
    - `_L_IF0_then`: `__drop_flags.x = 0`

---

### Example 3: Both Arms Initializing (Eliminates Flags)
```cxy
func testBothArms(cond: bool) {
    var x: Awesome
    if cond {
        x = Awesome()
    } else {
        x = Container()
    }
}
```
**CFG & Dataflow:**
- `_L_IF0_then`: `Gen={x}` $\implies$ `MayOut={x}`, `MustOut={x}`
- `_L_IF0_else`: `Gen={x}` $\implies$ `MayOut={x}`, `MustOut={x}`
- Merge block `_L_IF0_end`:
  - $\text{MayIn} = \{x\} \cup \{x\} = \{x\}$
  - $\text{MustIn} = \{x\} \cap \{x\} = \{x\}$
- At `_L_exit`:
  - For `x`: `May=1`, `Must=1` $\implies$ **Static `drop x` emitted**.
  - **No drop flag needed**.

---

### Example 4: Nested Loops with Break/Return Unwinding
```cxy
func testLoopUnwind(condBreak: bool, condRet: bool) {
    for i in 0..3 {
        var outer = Awesome()
        for j in 0..3 {
            var inner = Awesome()
            if j == 1 && condBreak {
                break
            }
            if i == 2 && condRet {
                return
            }
        }
    }
}
```
**Unwinding Analysis:**
1. **Normal Loop Exit / Continue**:
   - `inner` goes out of scope at the end of the inner loop body $\implies$ `Must=1`, `May=1` $\implies$ `drop inner`.
   - `outer` goes out of scope at the end of the outer loop body $\implies$ `Must=1`, `May=1` $\implies$ `drop outer`.
2. **Break Unwinding**:
   - Target is `_L_FOR1_exit`: `inner` is in scope $\implies$ `drop inner` emitted on break path before branch.
3. **Early Return Unwinding**:
   - Return jumps through unwind blocks `_L_UNWIND2_return` $\to$ `_L_UNWIND1_return` $\to$ `_L_exit`.
   - Scope exit drops are placed at each unwind step matching dataflow initialization state.

---

## 5. Pipeline Architecture & Implementation Plan

```
+----------------------------------------------------------------+
| Step 1: BitSet Utilities & Data Structures (memory.h)          |
|    - BitSet struct: alloc, set, reset, test, union, intersect  |
|    - CfgBlockInfo: gen, kill, mayIn, mayOut, mustIn, mustOut   |
+----------------------------------------------------------------+
                               |
                               v
+----------------------------------------------------------------+
| Step 2: CFG Construction & Predecessor Linking (manage.c)      |
|    - Walk terminators (irBranch, irBranchIf, irSwitch, return) |
|    - Connect bidirectional predecessor <-> successor edges     |
+----------------------------------------------------------------+
                               |
                               v
+----------------------------------------------------------------+
| Step 3: Compute Gen / Kill & Fixed-Point Solver (manage.c)     |
|    - Intra-block Gen/Kill transfer functions                   |
|    - Iterate MayIn/MustIn fixed-point convergence              |
+----------------------------------------------------------------+
                               |
                               v
+----------------------------------------------------------------+
| Step 4: Drop Elaboration & Drop Flag Synthesis (manage.c)      |
|    - Allocate drop flags where May=1 && Must=0                 |
|    - Transform scope exits into static drop or drop-if         |
+----------------------------------------------------------------+
```

---

## 6. Detailed Implementation Steps

### Step 1: BitSet Utilities & Data Structures (`memory.h`)
Define bit-vector operations optimized for 64-bit words:
```c
typedef struct BitSet {
    u64 *words;
    u64 count; // Number of bits
} BitSet;

BitSet makeBitSet(MemPool *pool, u64 count);
void bitSetSet(BitSet *bs, u64 index);
void bitSetReset(BitSet *bs, u64 index);
bool bitSetTest(const BitSet *bs, u64 index);
void bitSetCopy(BitSet *dst, const BitSet *src);
void bitSetSetAll(BitSet *bs);
void bitSetClearAll(BitSet *bs);
bool bitSetUnion(BitSet *dst, const BitSet *src);       // dst = dst | src
bool bitSetIntersect(BitSet *dst, const BitSet *src);   // dst = dst & src
bool bitSetEquals(const BitSet *a, const BitSet *b);
```
Define CFG block dataflow metadata:
```c
typedef struct CfgBlockInfo {
    IrNode *bb;
    BitSet gen;
    BitSet kill;
    BitSet mayIn;
    BitSet mayOut;
    BitSet mustIn;
    BitSet mustOut;
    DynArray preds; // List of CfgBlockInfo*
    DynArray succs; // List of CfgBlockInfo*
} CfgBlockInfo;
```

---

### Step 2: CFG Construction (`manage.c`)
Function `buildFunctionCfg(MemMgmtContext *ctx, IrNode *func)`:
1. Allocate a `CfgBlockInfo` for each `irBasicBlock` in `func->function.firstBlock`.
2. Inspect the terminator of each block:
   - `irBranch`: add edge to `branch.target`.
   - `irBranchIf`: add edges to `branchIf.trueTarget` and `branchIf.falseTarget`.
   - `irSwitch`: add edge to each case `target` and `defaultTarget`.
   - `irReturn`: 0 successors.
3. Populate bidirectional predecessor and successor lists.

---

### Step 3: Gen / Kill & Fixed-Point Solver (`manage.c`)
1. **Compute Gen and Kill per block (`computeBlockGenKill`)**:
   - For each statement $s \in B$:
     - `irAssign` / `irCopy` with destination variable $v$:
       `bitSetSet(&gen, v); bitSetReset(&kill, v);`
     - `irMove` with source variable $v$:
       `bitSetSet(&kill, v); bitSetReset(&gen, v);`
2. **Fixed-Point Dataflow Iteration (`solveDataflow`)**:
   - Initialize entry block:
     - Parameters set in `mayIn[entry]` and `mustIn[entry]`.
     - Local variables cleared (0).
   - Initialize all other blocks: `mustIn = 1s`, `mayIn = 0s`.
   - Loop over blocks until no `mayOut` or `mustOut` changes:
     ```c
     // MayIn = Union of all pred.MayOut
     // MustIn = Intersection of all pred.MustOut
     // MayOut = (MayIn & ~Kill) | Gen
     // MustOut = (MustIn & ~Kill) | Gen
     ```

---

### Step 4: Drop Elaboration & Drop Flag Synthesis (`manage.c`)
1. **Identify Variables Needing Drop Flags**:
   - Check all basic blocks at scope exit / assignment points.
   - If for variable $v$: `bitSetTest(&mayIn, v) && !bitSetTest(&mustIn, v)` $\implies$ mark `v->needsDropFlag = true`.
2. **Synthesize `__drop_flags` Struct**:
   - Allocate bit-fields in `__drop_flags` only for variables with `needsDropFlag == true`.
3. **Emit Drops at Scope Exits**:
   - If `v->needsDropFlag`: emit `drop-if v, __drop_flags.v`.
   - Else if `bitSetTest(&mustIn, v)`: emit static `drop v`.
   - Else: emit no drop (dead drop eliminated).

---

## 7. Files & Components

- **`cxy/src/cxy/lang/middle/memory/memory.h`**:
  - `BitSet` bit-vector definition and operations.
  - `CfgBlockInfo` CFG node and dataflow state structures.
- **`cxy/src/cxy/lang/middle/memory/manage.c`**:
  - CFG construction, Gen/Kill computation, dataflow solver, and drop elaboration.
- **`cxy/tests/lang/middle/mem/*.cxy`**:
  - Regression snapshots verifying drop placement and flag elimination.
