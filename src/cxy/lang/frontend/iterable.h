//
// Created by Carter on 2024-01-XX.
//

#pragma once

#include "core/log.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AstNode;
struct MemPool;
struct TypeTable;

/**
 * Iterator kinds for different compile-time iterables
 */
typedef enum {
    citMembers,      // struct/class members (fields + methods)
    citTupleType,    // tuple type members
    citUnionType,    // union type alternatives
    citArrayExpr,    // array expression elements
    citTupleExpr,    // tuple expression elements
    citVarArgs,      // variadic arguments
    citStringLit,    // string characters (byte-level)
    citRange,        // numeric range
    citInvalid       // error state - unsupported iterable
} ComptimeIteratorKind;

/**
 * Compile-time iterator for #for loops
 * Stack-allocated value type with polymorphic state
 */
typedef struct ComptimeIterator {
    ComptimeIteratorKind kind;
    struct MemPool *pool;
    struct TypeTable *types;
    FileLoc loc;  // source location for created nodes

    // Polymorphic state - different per kind
    union {
        // Members iterator (struct/class)
        struct {
            const TypeMembersContainer *container;  // type member container
            u64 index;  // current index in container
            struct AstNode *current;  // fallback AST list iteration
            const Type *type;  // source of truth if available
        } members;

        // Tuple type iterator
        struct {
            const struct Type *tuple;
            u64 index;
        } tupleType;

        // Union type iterator
        struct {
            const struct Type *union_;
            u64 index;
        } unionType;

        // Array expression iterator
        struct {
            struct AstNode *current;
        } arrayExpr;

        // Tuple expression iterator
        struct {
            struct AstNode *current;
        } tupleExpr;

        // Varargs iterator
        struct {
            struct AstNode *current;
        } varArgs;

        // String literal iterator (byte-level)
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

/**
 * Create iterator from a comptime iterable node
 * @param pool Memory pool for allocating nodes
 * @param types Type table for primitive types
 * @param source Iterable source (comptimeOnly node or direct expression)
 * @return Stack-allocated iterator (kind=citInvalid if unsupported)
 */
ComptimeIterator newComptimeIterator(struct MemPool *pool,
                                      struct TypeTable *types,
                                      struct AstNode *source);

/**
 * Check if iterator has more elements
 * @param it Iterator pointer
 * @return true if more elements available, false otherwise
 */
bool comptimeIteratorHasNext(ComptimeIterator *it);

/**
 * Get next element from iterator
 * @param it Iterator pointer
 * @return Next AST node element, or NULL if none
 */
struct AstNode *comptimeIteratorNext(ComptimeIterator *it);

/**
 * Get element at specific index without advancing iterator
 * @param it Iterator pointer
 * @param index Index of element to retrieve
 * @return AST node at index, or NULL if out of bounds
 */
struct AstNode *comptimeIteratorAt(ComptimeIterator *it, u64 index);

/**
 * Get total count of elements in iterator
 * @param it Iterator pointer
 * @return Total number of elements, or 0 if invalid/empty
 */
u64 comptimeIteratorCount(ComptimeIterator *it);

/**
 * Check if an iterable is empty without creating a persistent iterator
 * @param pool Memory pool for iterator creation
 * @param types Type table for primitive types
 * @param source Iterable source node
 * @return true if iterable has no elements, false otherwise
 */
bool comptimeIteratorIsEmpty(struct MemPool *pool,
                              struct TypeTable *types,
                              struct AstNode *source);

#ifdef __cplusplus
}
#endif