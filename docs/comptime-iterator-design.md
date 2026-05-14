# Comptime Iterator System - Design Document

## Overview

A unified iterator system for compile-time (`#for`) loops that provides consistent iteration over various compile-time collections including struct/class members, tuple/union types, arrays, ranges, and other AST constructs.

## Motivation

Currently, comptime iteration is handled inconsistently across different node types. This design introduces a unified `ComptimeIterator` abstraction that:

- Provides consistent API for all iterable compile-time constructs
- Simplifies implementation of `#for` loops
- Enables generic iteration utilities
- Aligns with existing `comptime.c` architecture patterns

## Supported Iterables

The iterator system must support the following compile-time iterables:

### Type-Based Iterables

1. **Struct/Class Members** (`T.members`)
   - All members (fields + methods)
   - Prefer type as source if available (post type-checking)
   - Fall back to decl members if type not available

2. **Tuple Type Members** (`T` where T is tuple type)
   - Iterate over constituent types
   - Return wrapped type nodes

3. **Union Type Members** (`T` where T is union type)
   - Iterate over alternative types
   - Return wrapped type nodes

### AST Node Iterables

4. **ArrayExpr** - Array literal elements
5. **TupleExpr** - Tuple expression elements
6. **VarArgs** - Variadic function arguments
7. **StringLit** - String characters (as char literals)
8. **Range** - Numeric range (0..10)

## Core API

### Iterator Structure

```c
typedef enum {
    citMembers,      // struct/class members (fields + methods)
    citTupleType,    // tuple type members
    citUnionType,    // union type alternatives
    citArrayExpr,    // array expression elements
    citTupleExpr,    // tuple expression elements
    citVarArgs,      // variadic arguments
    citStringLit,    // string characters
    citRange,        // numeric range
    citInvalid       // error state
} ComptimeIteratorKind;

typedef struct ComptimeIterator {
    ComptimeIteratorKind kind;
    EvalContext *ctx;
    
    // Polymorphic state - different per kind
    union {
        // Members iterator (struct/class)
        struct {
            AstNode *current;
            const Type *type;  // source of truth if available
        } members;
        
        // Tuple type iterator
        struct {
            const Type *tuple;
            u64 index;
        } tupleType;
        
        // Union type iterator
        struct {
            const Type *union_;
            u64 index;
        } unionType;
        
        // Array expression iterator
        struct {
            AstNode *current;
        } arrayExpr;
        
        // Tuple expression iterator
        struct {
            AstNode *current;
        } tupleExpr;
        
        // Varargs iterator
        struct {
            AstNode *current;
        } varArgs;
        
        // String literal iterator
        struct {
            const char *str;
            u64 index;
            u64 length;
        } stringLit;
        
        // Range iterator
        struct {
            i64 current;
            i64 end;
            i64 step;
        } range;
    } state;
} ComptimeIterator;
```

### Primary Interface

```c
// Create iterator from an AST node (result of member access like T.members)
ComptimeIterator newComptimeIterator(EvalContext *ctx, AstNode *source);

// Check if more elements available
bool comptimeIteratorHasNext(ComptimeIterator *it);

// Get next element (returns NULL if none)
AstNode *comptimeIteratorNext(ComptimeIterator *it);
```

### Usage Pattern

```c
// User code:
// #for member in T.members { ... }

// Implementation:
AstNode *iterable = evaluate(ctx, node->forStmt.range); // evaluates T.members
ComptimeIterator it = newComptimeIterator(ctx, iterable);

while (comptimeIteratorHasNext(&it)) {
    AstNode *element = comptimeIteratorNext(&it);
    // Bind element to loop variable and evaluate body
}
```

## Iterator Semantics by Kind

### 1. Members Iterator (Struct/Class)

**Source**: Result of `T.members` member access

**Strategy**:
- If `type` field is available (post type-checking), use it as source of truth
- Otherwise use AST node member list from declaration
- Iterate through linked list of members (fields + methods)

**Returns**: Direct AST node pointers to field/method declarations

### 2. Tuple Type Iterator

**Source**: Comptime-wrapped tuple type node

**Strategy**:
- Access `type->tuple.members` array
- Iterate by index from 0 to `type->tuple.count`

**Returns**: Comptime-wrapped type nodes for each member type

### 3. Union Type Iterator

**Source**: Comptime-wrapped union type node

**Strategy**:
- Access `type->tUnion.members` array  
- Iterate by index from 0 to `type->tUnion.count`

**Returns**: Comptime-wrapped type nodes for each alternative type

### 4. ArrayExpr Iterator

**Source**: ArrayExpr node

**Strategy**:
- Iterate through `node->arrayExpr.elements` linked list

**Returns**: Direct AST node pointers to array element expressions

### 5. TupleExpr Iterator

**Source**: TupleExpr node

**Strategy**:
- Iterate through `node->tupleExpr.elements` linked list

**Returns**: Direct AST node pointers to tuple element expressions

### 6. VarArgs Iterator

**Source**: Result of variadic parameter expansion

**Strategy**:
- Source is `comptimeOnly` node where `node` field points to first vararg
- Iterate through variadic argument linked list starting from `comptimeOnly.node`

**Returns**: Direct AST node pointers to argument expressions

### 7. StringLit Iterator

**Source**: StringLiteral node

**Strategy**:
- Get string value and length
- Iterate by index through bytes (not UTF-8 aware in initial implementation)
- Create character literal node for each byte
- UTF-8/multi-byte support will come naturally when wide char support is added

**Returns**: Newly allocated character literal nodes (one per byte)

### 8. Range Iterator

**Source**: Range node (e.g., 0..10)

**Strategy**:
- Extract start, end, step from range
- Iterate numerically from start to end (exclusive) by step
- Create integer literal node for each value

**Returns**: Newly allocated integer literal nodes

## Integration with Comptime System

### Member Access Returns Iterable

When evaluating `T.members`, the comptime member access should return a node with `comptimeOnly` field populated:

```c
// In getMembers() or similar
AstNode *getMembers(EvalContext *ctx, AstNode *node) {
    const Type *type = actualType(node);
    
    if (typeIs(type, Struct) || typeIs(type, Class)) {
        // Create a node with comptimeOnly metadata
        AstNode *result = makeAstNode(ctx->pool, &node->loc, 
                                      &(AstNode){.tag = astComptime});
        result->comptimeOnly.node = getStructOrClassMembers(type);
        result->comptimeOnly.type = type;
        return result;
    }
    
    // ... handle other types
}
```

**AstNode Extension**: Add to AstNode union:
```c
struct { 
    AstNode *node;      // AST node (member list, first vararg, etc.)
    const Type *type;   // Associated type (struct/class/tuple/union type)
} comptimeOnly;
```

### Iterator Creation from Source

```c
ComptimeIterator newComptimeIterator(EvalContext *ctx, AstNode *source) {
    ComptimeIterator it = {.ctx = ctx, .kind = citInvalid};
    
    if (!source)
        return it;
    
    // Check for comptimeOnly node first (from member access like T.members)
    if (source->tag == astComptime && source->comptimeOnly.node) {
        AstNode *node = source->comptimeOnly.node;
        const Type *type = source->comptimeOnly.type;
        
        // Determine kind from type if available
        if (type) {
            if (typeIs(type, Struct) || typeIs(type, Class)) {
                it.kind = citMembers;
                it.state.members.current = node;  // member list
                it.state.members.type = type;
                return it;
            }
            else if (typeIs(type, Tuple)) {
                it.kind = citTupleType;
                it.state.tupleType.tuple = type;
                it.state.tupleType.index = 0;
                return it;
            }
            else if (typeIs(type, Union)) {
                it.kind = citUnionType;
                it.state.unionType.union_ = type;
                it.state.unionType.index = 0;
                return it;
            }
        }
        
        // Handle varargs (node is first arg)
        if (node) {
            it.kind = citVarArgs;
            it.state.varArgs.current = node;
            return it;
        }
    }
    
    // Direct node iteration (no comptimeOnly wrapper)
    switch (source->tag) {
        case astArrayExpr:
            it.kind = citArrayExpr;
            it.state.arrayExpr.current = source->arrayExpr.elements;
            break;
            
        case astTupleExpr:
            it.kind = citTupleExpr;
            it.state.tupleExpr.current = source->tupleExpr.elements;
            break;
            
        case astRangeExpr:
            it.kind = citRange;
            // Extract range bounds
            it.state.range.current = getRangeStart(source);
            it.state.range.end = getRangeEnd(source);
            it.state.range.step = 1;
            break;
            
        case astStringLit:
            it.kind = citStringLit;
            it.state.stringLit.str = source->stringLiteral.value;
            it.state.stringLit.index = 0;
            it.state.stringLit.length = strlen(source->stringLiteral.value);
            break;
            
        // ... handle other cases
    }
    
    return it;
}
```

## Error Handling

### Invalid Iterator

If `newComptimeIterator` cannot determine how to iterate the source:
- Set `kind = citInvalid`
- `hasNext` returns `false`
- `next` returns `NULL`
- Caller should check and report error

### Empty Collections

- Valid iterator with `kind` set correctly
- `hasNext` returns `false` immediately
- `next` returns `NULL`

### Allocation Failures

For iterators that allocate nodes (StringLit, Range):
- Return `NULL` from `next` on allocation failure
- Caller must handle `NULL` check

## Implementation Notes

### Stack Allocation

Iterator is stack-allocated value type, not heap pointer:
```c
ComptimeIterator it = newComptimeIterator(ctx, source);
```

### No Reset Required

For comptime evaluation, we only iterate forward once. No need for reset capability in initial implementation.

### Type as Source of Truth

When iterating certain constructs:
- **Prefer type** for: struct/class members, tuple types, union types
  - Type is canonical after type-checking phase
  - Handles inherited members correctly for classes
- **Use AST directly** for: ArrayExpr, TupleExpr, StringLit, Range
  - These are expression-level constructs without associated type semantics
  - Type field not applicable or not the source we want

### Node Ownership

- Iterators return pointers to existing nodes where possible (members, array elements)
- Only allocate new nodes when necessary (string chars, range values)
- Caller does not own returned nodes; they live in AST pool

## Example Usage Scenarios

### Iterate Struct Fields

```c
// Cxy code:
// #for member in MyStruct.members { ... }

// C implementation:
AstNode *structType = ...; // result of MyStruct
AstNode *members = evalAstNodeMemberAccess(ctx, structType, "members");
ComptimeIterator it = newComptimeIterator(ctx, members);

while (comptimeIteratorHasNext(&it)) {
    AstNode *member = comptimeIteratorNext(&it);
    // member is FieldDecl or FuncDecl node
}
```

### Iterate Array Elements

```c
// Cxy code:
// #for elem in [1, 2, 3] { ... }

// C implementation:
AstNode *arrayExpr = ...; // [1, 2, 3]
ComptimeIterator it = newComptimeIterator(ctx, arrayExpr);

while (comptimeIteratorHasNext(&it)) {
    AstNode *elem = comptimeIteratorNext(&it);
    // elem is IntegerLiteral node
}
```

### Iterate Range

```c
// Cxy code:
// #for i in 0..10 { ... }

// C implementation:
AstNode *range = ...; // 0..10 range expression
ComptimeIterator it = newComptimeIterator(ctx, range);

while (comptimeIteratorHasNext(&it)) {
    AstNode *value = comptimeIteratorNext(&it);
    // value is newly allocated IntegerLiteral: 0, 1, 2, ..., 9
}
```

### Iterate Tuple Type Members

```c
// Cxy code:
// #const T = #(i32, string, bool)
// #for memberType in T { ... }

// C implementation:
AstNode *tupleTypeNode = ...; // comptime-wrapped tuple type
ComptimeIterator it = newComptimeIterator(ctx, tupleTypeNode);

while (comptimeIteratorHasNext(&it)) {
    AstNode *typeNode = comptimeIteratorNext(&it);
    // typeNode is comptime-wrapped type: i32, string, bool
}
```

## Future Extensions

### Filtered Iteration

Add optional filter callback to skip unwanted elements:
```c
typedef bool (*ComptimeIteratorFilter)(AstNode *node);

void comptimeIteratorSetFilter(ComptimeIterator *it, 
                                ComptimeIteratorFilter filter);
```

### Fields-Only / Methods-Only Iterators

Provide specialized member access:
- `T.fields` - only fields
- `T.methods` - only methods

Implementation via filter or separate member getter.

### Custom Step for Ranges

Support `0..10..2` syntax for custom step values.

### Reverse Iteration

Support `10..0..-1` for reverse iteration.

### Bidirectional Iterators

Add `comptimeIteratorPrev()` for backward iteration (if needed).

## Implementation Status

### Phase 1: ✅ Complete

1. ✅ Added `comptimeOnly` union member to `AstNode` (ast.h)
2. ✅ Implemented `makeAstComptimeOnly()` and `makeAstComptimeIterable()` helpers (ast.c)
3. ✅ Created `iterable.h` with API definitions
4. ✅ Implemented `iterable.c` with all iterator kinds:
   - Members iterator (struct/class)
   - TupleType iterator
   - UnionType iterator
   - ArrayExpr iterator
   - TupleExpr iterator
   - VarArgs iterator
   - StringLit iterator (byte-level)
   - Range iterator
5. ✅ Updated CMakeLists.txt to include iterable.c
6. ✅ Build succeeds without errors

### Phase 2: ✅ Complete

1. ✅ Updated comptime member getters to use `makeAstComptimeIterable()`
   - getMembers, getAttributes, getAnnotations, getParams, getTypeInfo
   - Pass full nodes to iterator, let it extract iterables based on node->tag
2. ✅ Integrated iterator API into `#for` evaluation
   - Replaced all specialized iteration functions with unified `evalForStmtWithIterator()`
   - Handles all iterable types through single code path
   - Variadic parameter resolution handled before iterator creation
3. ✅ Added `comptimeIteratorAt()` and `comptimeIteratorCount()` APIs for random access
4. ✅ Updated comptime integer index operator (`[i]`) to use iterator APIs
5. ✅ Updated comptime string index operator (`["name"]`) to use iterator APIs
6. ✅ Updated `len!()` macro to support comptime iterables
7. ✅ Exported `getNamedNodeName()` in ast.h for name-based member lookup
8. ✅ Added `comptimeIteratorIsEmpty()` helper for empty check
9. ✅ Fixed boolean cast for comptimeOnly nodes to use iterator API
10. ❌ Add tests for each iterator kind (TODO)
11. ❌ Document in `comptime.md` (TODO)

## Implementation Notes

### Architecture
- Iterator is self-contained and doesn't depend on `EvalContext`
- Uses `MemPool *pool` and `TypeTable *types` directly for efficiency
- Source location stored in iterator for accurate error reporting

### Node Handling
- **Full nodes passed to `makeAstComptimeIterable()`** - iterator extracts iterables based on `node->tag`
- Supports: StructDecl, ClassDecl, UnionDecl, EnumDecl, TupleType, FuncDecl, FuncType, ClosureExpr, Attr, FuncParamDecl (variadic)
- For attributes/annotations: pass list directly (not full decl) to avoid ambiguity

### Type-Based Iteration (Canonical Source of Truth)
- **Struct/Class members**: Uses `type->tStruct.members` or `type->tClass.members` container
  - Iterates by index through `NamedTypeMember` array
  - Returns member declaration nodes (`member->decl`)
  - Includes inherited members for classes (canonical after type-checking)
- **Union alternatives**: Uses `type->tUnion.members` array
  - Iterates by index through `UnionMember` array
  - Returns **TypeRef nodes** for each alternative type
- **Tuple members**: Uses `type->tuple.members` array
  - Iterates by index through type array
  - Returns **TypeRef nodes** for each member type
- **Fallback**: Only uses AST node lists when type is unavailable

### Linked List vs Single Node Handling
- **Linked list detection**: If `node->next != NULL`, treats node as part of a list
  - Iterates the list directly without extraction
  - Handles attribute lists, annotation lists, field lists, etc.
  - Example: `decl->attrs` is a linked list of attributes → iterates attributes themselves
- **Single node extraction**: If `node->next == NULL`, applies smart extraction
  - Extracts iterable based on node type (e.g., `structDecl.members`)
  - Example: single `StructDecl` → extracts `structDecl.members` to iterate
- **Preserves old behavior**: Matches original `comptimeWrapped` which used `.next` for list iteration

### Random Access APIs
- **`comptimeIteratorCount(it)`**: Returns total element count
  - O(1) for container/type-based iterators (direct count access)
  - O(n) for AST list iterators (uses `countAstNodes()` helper)
- **`comptimeIteratorAt(it, index)`**: Returns element at index without advancing iterator
  - O(1) for container/type-based iterators (direct array access)
  - O(n) for AST list iterators (uses `getNodeAtIndex()` helper)
  - Allocates new nodes for string chars and range values (same as `next()`)
- **`comptimeIteratorIsEmpty(pool, types, source)`**: Check if iterable is empty
  - Convenience helper that creates iterator and checks `hasNext()`
  - Used for boolean cast of comptimeOnly nodes
  - Avoids need to manually create and check iterator
- **Comptime Integer Index Operator**: `T.members[3]` uses `comptimeIteratorAt()`
  - Works with all iterable types
  - Proper bounds checking with iterator count
  - Returns TypeRef nodes for type-based iterables
- **Comptime String Index Operator**: `T.members["name"]` uses iterator for name lookup
  - Iterates through members to find by name using `getNamedNodeName()`
  - Works with all iterable types (members, fields, params, etc.)
  - Returns member node or annotation value
  - Returns null if not found

### Macro Integration
- **`len!()` macro**: Now supports comptime iterables via `comptimeIteratorCount()`
  - `len!(T.members)` returns member count
  - `len!(T.fields)` returns field count  
  - Works with all iterable types (structs, classes, unions, tuples, arrays, etc.)
  - Returns `u64` integer literal at compile time

### Boolean Cast Integration
- **Boolean cast for comptimeOnly nodes**: Uses `comptimeIteratorIsEmpty()`
  - Converts comptime iterables to boolean based on whether they have elements
  - `!!T.members` returns true if type has members, false otherwise
  - Works correctly with all iterable types (not just old `.next` field)

### Return Values
- Returns `astTypeRef` nodes for type iteration (tuple/union types)
- Returns direct AST node pointers for expression/member iteration
- Allocates new nodes only for string chars and range values

### For Loop Integration
- Single unified `evalForStmtWithIterator()` function handles all iterable types
- Replaces previous specialized functions (evalForStmtWithRange, evalForStmtWithString, etc.)
- Variadic parameters resolved before iterator creation
- Cleaner error handling with single code path

## Design Decisions (Resolved)

1. **Member Access Return Type**: 
   - **Decision**: Add `comptimeOnly` member to AstNode union:
     ```c
     struct { 
         AstNode *node;      // member list, first vararg, etc.
         const Type *type;   // associated type if applicable
     } comptimeOnly;
     ```
   - Member access like `T.members` returns node with `comptimeOnly` populated
   - Provides all relevant info for iterator creation

2. **String Iteration Encoding**: 
   - **Decision**: Start with byte-level iteration
   - UTF-8/multi-byte support will come automatically with wide char support

3. **VarArgs Source**: 
   - **Decision**: `comptimeOnly.node` contains the first vararg in the list
   - Iterate through linked list starting from this node

4. **Type Preference**: 
   - **Decision**: Prefer type for struct/class members, tuple types, union types
   - Use AST directly for ArrayExpr, TupleExpr, StringLit, Range (no type preference)

5. **Error Reporting**: 
   - **Decision**: Return invalid iterator (`citInvalid` kind)
   - Caller checks and reports context-specific error messages