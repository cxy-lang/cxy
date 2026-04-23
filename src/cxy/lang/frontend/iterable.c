//
// Created by Carter on 2024-01-XX.
//

#include "iterable.h"
#include "types.h"

#include "ast.h"
#include "flag.h"
#include "strings.h"
#include "ttable.h"

#include "lang/middle/builtins.h"

#include <string.h>

// Helper to get range start value
static inline i64 getRangeStart(const AstNode *range)
{
    if (range->rangeExpr.start)
        return range->rangeExpr.start->intLiteral.value;
    return 0;
}

// Helper to get range end value
static inline i64 getRangeEnd(const AstNode *range)
{
    if (range->rangeExpr.end)
        return range->rangeExpr.end->intLiteral.value;
    return 0;
}

ComptimeIterator newComptimeIterator(MemPool *pool,
                                     TypeTable *types,
                                     AstNode *source)
{
    ComptimeIterator it = {.kind = citInvalid, .pool = pool, .types = types};

    if (source == NULL)
        return it;

    it.loc = source->loc;

    // Check for comptimeOnly node with metadata (from member access like
    // T.members)
    if (nodeIs(source, ComptimeOnly) && hasFlag(source, ComptimeIterable)) {
        AstNode *node = source->comptimeOnly.node;
        const Type *type = source->comptimeOnly.type;

        // Extract iterable list based on node tag
        AstNode *iterableList = NULL;
        if (node != NULL) {
            switch (node->tag) {
            case astStructDecl:
            case astClassDecl:
                // Prefer type as source of truth for struct/class members
                // Iterate type member container (includes inherited members for
                // classes)
                if (type && (typeIs(type, Struct) || typeIs(type, Class))) {
                    it.kind = citMembers;
                    it.state.members.container = typeIs(type, Struct)
                                                     ? type->tStruct.members
                                                     : type->tClass.members;
                    it.state.members.index = 0;
                    it.state.members.current = NULL;
                    it.state.members.type = type;
                    return it;
                }
                else {
                    iterableList = node->structDecl.members;
                }
                break;
            case astUnionDecl:
                // Prefer type as source of truth for union type members
                if (type && typeIs(type, Union)) {
                    it.kind = citUnionType;
                    it.state.unionType.union_ = type;
                    it.state.unionType.index = 0;
                    return it;
                }
                else {
                    iterableList = node->unionDecl.members;
                }
                break;
            case astEnumDecl:
                iterableList = node->enumDecl.options;
                break;
            case astTupleType:
                // Prefer type as source of truth for tuple type members
                if (type && typeIs(type, Tuple)) {
                    it.kind = citTupleType;
                    it.state.tupleType.tuple = type;
                    it.state.tupleType.index = 0;
                    return it;
                }
                else {
                    iterableList = node->tupleType.elements;
                }
                break;
            case astFuncDecl:
                iterableList = node->funcDecl.signature->params;
                break;
            case astFuncType:
                iterableList = node->funcType.params;
                break;
            case astClosureExpr:
                iterableList = node->closureExpr.params;
                break;

            case astFuncParamDecl:
                if (hasFlag(node, Variadic) && node->funcParam.type) {
                    iterableList = node->funcParam.type->tupleType.elements;
                }
                break;
            default:
                // For unknown nodes or direct lists (attrs, annotations),
                // iterate node as-is
                iterableList = node;
                break;
            }
        }

        // Set up iterator for node list (AST-based iteration)
        if (iterableList != NULL) {
            it.kind = citMembers;
            it.state.members.container = NULL;
            it.state.members.index = 0;
            it.state.members.current = iterableList;
            it.state.members.type = type;
            return it;
        }

        // Type-based iteration (for tuple/union types)
        if (type != NULL) {
            if (typeIs(type, Tuple)) {
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
        it.state.range.current = getRangeStart(source);
        it.state.range.end = getRangeEnd(source);
        it.state.range.step = source->rangeExpr.down ? -1 : 1;
        break;

    case astStringLit:
        it.kind = citStringLit;
        it.state.stringLit.str = source->stringLiteral.value;
        it.state.stringLit.index = 0;
        it.state.stringLit.length = strlen(source->stringLiteral.value);
        break;

    case astFuncParamDecl:
        if (hasFlag(source, Variadic)) {
            it.kind = citVarArgs;
            it.state.tupleExpr.current = source;
        }
        break;

    default:
        // Unsupported iterable
        break;
    }

    return it;
}

bool comptimeIteratorHasNext(ComptimeIterator *it)
{
    if (it == NULL || it->kind == citInvalid)
        return false;

    switch (it->kind) {
    case citMembers:
        // Check container-based iteration first (type members)
        if (it->state.members.container != NULL)
            return it->state.members.index < it->state.members.container->count;
        // Fall back to AST list iteration
        return it->state.members.current != NULL;

    case citTupleType:
        return it->state.tupleType.index <
               it->state.tupleType.tuple->tuple.count;

    case citUnionType:
        return it->state.unionType.index <
               it->state.unionType.union_->tUnion.count;

    case citArrayExpr:
        return it->state.arrayExpr.current != NULL;

    case citTupleExpr:
        return it->state.tupleExpr.current != NULL;

    case citVarArgs:
        return it->state.varArgs.current != NULL;

    case citStringLit:
        return it->state.stringLit.index < it->state.stringLit.length;

    case citRange:
        if (it->state.range.step > 0)
            return it->state.range.current < it->state.range.end;
        else
            return it->state.range.current > it->state.range.end;

    case citInvalid:
    default:
        return false;
    }
}

AstNode *comptimeIteratorNext(ComptimeIterator *it)
{
    if (it == NULL || it->kind == citInvalid)
        return NULL;

    switch (it->kind) {
    case citMembers: {
        // Container-based iteration (type members)
        if (it->state.members.container != NULL) {
            if (it->state.members.index >= it->state.members.container->count)
                return NULL;
            const NamedTypeMember *member =
                &it->state.members.container
                     ->members[it->state.members.index++];
            // Return the declaration node from type member
            return (AstNode *)member->decl;
        }

        // AST list iteration (fallback)
        AstNode *current = it->state.members.current;
        if (current == NULL)
            return NULL;
        it->state.members.current = current->next;
        return current;
    }

    case citTupleType: {
        if (it->state.tupleType.index >= it->state.tupleType.tuple->tuple.count)
            return NULL;

        const Type *memberType = it->state.tupleType.tuple->tuple
                                     .members[it->state.tupleType.index++];

        // Return TypeRef node
        return makeTypeReferenceNode(it->pool, memberType, &it->loc);
    }

    case citUnionType: {
        if (it->state.unionType.index >=
            it->state.unionType.union_->tUnion.count)
            return NULL;

        const Type *altType = it->state.unionType.union_->tUnion
                                  .members[it->state.unionType.index++]
                                  .type;

        // Return TypeRef node
        return makeTypeReferenceNode(it->pool, altType, &it->loc);
    }

    case citArrayExpr: {
        AstNode *current = it->state.arrayExpr.current;
        if (current == NULL)
            return NULL;
        it->state.arrayExpr.current = current->next;
        return current;
    }

    case citTupleExpr: {
        AstNode *current = it->state.tupleExpr.current;
        if (current == NULL)
            return NULL;
        it->state.tupleExpr.current = current->next;
        return current;
    }

    case citVarArgs: {
        AstNode *current = it->state.varArgs.current;
        if (current == NULL)
            return NULL;
        it->state.varArgs.current = current->next;
        return current;
    }

    case citStringLit: {
        if (it->state.stringLit.index >= it->state.stringLit.length)
            return NULL;

        char c = it->state.stringLit.str[it->state.stringLit.index++];

        // Create character literal node
        return makeCharLiteral(
            it->pool, &it->loc, c, NULL, getPrimitiveType(it->types, prtChar));
    }

    case citRange: {
        if (!comptimeIteratorHasNext(it))
            return NULL;

        i64 value = it->state.range.current;
        it->state.range.current += it->state.range.step;

        // Create integer literal node
        return makeIntegerLiteral(it->pool,
                                  &it->loc,
                                  value,
                                  NULL,
                                  getPrimitiveType(it->types, prtI64));
    }

    case citInvalid:
    default:
        return NULL;
    }
}

u64 comptimeIteratorCount(ComptimeIterator *it)
{
    if (it == NULL || it->kind == citInvalid)
        return 0;

    switch (it->kind) {
    case citMembers:
        // Container-based: return count directly
        if (it->state.members.container != NULL)
            return it->state.members.container->count;

        // AST list: use countAstNodes helper
        return countAstNodes(it->state.members.current);

    case citTupleType:
        return it->state.tupleType.tuple->tuple.count;

    case citUnionType:
        return it->state.unionType.union_->tUnion.count;

    case citArrayExpr:
        return countAstNodes(it->state.arrayExpr.current);

    case citTupleExpr:
        return countAstNodes(it->state.tupleExpr.current);

    case citVarArgs:
        return countAstNodes(it->state.varArgs.current);

    case citStringLit:
        return it->state.stringLit.length;

    case citRange: {
        i64 start = it->state.range.current;
        i64 end = it->state.range.end;
        i64 step = it->state.range.step;

        if (step == 0)
            return 0;

        if (step > 0) {
            if (start >= end)
                return 0;
            return (u64)((end - start + step - 1) / step);
        }
        else {
            if (start <= end)
                return 0;
            return (u64)((start - end - step - 1) / (-step));
        }
    }

    case citInvalid:
    default:
        return 0;
    }
}

AstNode *comptimeIteratorAt(ComptimeIterator *it, u64 index)
{
    if (it == NULL || it->kind == citInvalid)
        return NULL;

    switch (it->kind) {
    case citMembers:
        // Container-based: direct index access
        if (it->state.members.container != NULL) {
            if (index >= it->state.members.container->count)
                return NULL;
            const NamedTypeMember *member =
                &it->state.members.container->members[index];
            return (AstNode *)member->decl;
        }

        // AST list: use getNodeAtIndex helper
        return getNodeAtIndex(it->state.members.current, index);

    case citTupleType:
        if (index >= it->state.tupleType.tuple->tuple.count)
            return NULL;
        {
            const Type *memberType =
                it->state.tupleType.tuple->tuple.members[index];
            return makeTypeReferenceNode(it->pool, memberType, &it->loc);
        }

    case citUnionType:
        if (index >= it->state.unionType.union_->tUnion.count)
            return NULL;
        {
            const Type *altType =
                it->state.unionType.union_->tUnion.members[index].type;
            return makeTypeReferenceNode(it->pool, altType, &it->loc);
        }

    case citArrayExpr:
        return getNodeAtIndex(it->state.arrayExpr.current, index);

    case citTupleExpr:
        return getNodeAtIndex(it->state.tupleExpr.current, index);

    case citVarArgs:
        return getNodeAtIndex(it->state.varArgs.current, index);

    case citStringLit:
        if (index >= it->state.stringLit.length)
            return NULL;
        {
            char c = it->state.stringLit.str[index];
            return makeCharLiteral(it->pool,
                                   &it->loc,
                                   c,
                                   NULL,
                                   getPrimitiveType(it->types, prtChar));
        }

    case citRange: {
        i64 value = it->state.range.current + (i64)index * it->state.range.step;
        // Check bounds
        if (it->state.range.step > 0) {
            if (value >= it->state.range.end)
                return NULL;
        }
        else {
            if (value <= it->state.range.end)
                return NULL;
        }
        return makeIntegerLiteral(it->pool,
                                  &it->loc,
                                  value,
                                  NULL,
                                  getPrimitiveType(it->types, prtI64));
    }

    case citInvalid:
    default:
        return NULL;
    }
}

bool comptimeIteratorIsEmpty(MemPool *pool,
                              TypeTable *types,
                              AstNode *source)
{
    ComptimeIterator it = newComptimeIterator(pool, types, source);
    return !comptimeIteratorHasNext(&it);
}