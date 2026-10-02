//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"

static inline bool isParentAssignExpr(const AstNode *node)
{
    return nodeIs(node->parentScope, AssignExpr);
}

static void checkIndexOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = stripAll(node->indexExpr.target->type);
    // csAssert0(!isParentAssignExpr(node));

    const Type *func = findStructMemberType(target, S_IndexOverload);
    if (func == NULL) {
        logError(ctx->L,
                 &node->indexExpr.target->loc,
                 "index expression target type '{t}' does not overload the "
                 "index operator `[]`",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *index = checkType(visitor, node->indexExpr.index);
    if (typeIs(index, Error)) {
        node->type = index;
        return;
    }

    transformToMemberCallExpr(visitor,
                              node,
                              node->indexExpr.target,
                              S_IndexOverload,
                              node->indexExpr.index);
    checkType(visitor, node);
}

static void checkSliceOperator(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = stripAll(node->sliceExpr.target->type);

    const Type *func = findStructMemberType(target, S_SliceOverload);
    if (func == NULL) {
        logError(ctx->L,
                 &node->sliceExpr.target->loc,
                 "slice expression target type '{t}' does not overload the "
                 "slice operator `[..]`",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *range = node->sliceExpr.range;
    AstNode *startArg = range->rangeExpr.start;
    AstNode *endArg = range->rangeExpr.end;

    if (startArg == NULL) {
        startArg = makeIntegerLiteral(ctx->pool,
                                      &node->loc,
                                      0,
                                      NULL,
                                      getPrimitiveType(ctx->types, prtU64));
    }
    if (endArg == NULL) {
        endArg = makeNullLiteral(
            ctx->pool, &node->loc, NULL, makeNullType(ctx->types));
    }

    startArg->next = endArg;
    endArg->next = NULL;

    transformToMemberCallExpr(
        visitor, node, node->sliceExpr.target, S_SliceOverload, startArg);
    checkType(visitor, node);
}

void checkSliceExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->sliceExpr.target);
    if (typeIs(target, Error)) {
        node->type = target;
        return;
    }

    AstNode *range = node->sliceExpr.range;
    if (range->rangeExpr.start) {
        const Type *startType = checkType(visitor, range->rangeExpr.start);
        if (typeIs(startType, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (!isIntegerType(startType)) {
            logError(ctx->L,
                     &range->rangeExpr.start->loc,
                     "slice start index must be an integer, got '{t}'",
                     (FormatArg[]){{.t = startType}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    if (range->rangeExpr.end) {
        const Type *endType = checkType(visitor, range->rangeExpr.end);
        if (typeIs(endType, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (!isIntegerType(endType)) {
            logError(ctx->L,
                     &range->rangeExpr.end->loc,
                     "slice end index must be an integer, got '{t}'",
                     (FormatArg[]){{.t = endType}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    node->flags |= node->sliceExpr.target->flags;
    const Type *unwrapped = stripReference(resolveUnThisUnwrapType(target)),
               *stripped = stripAll(target);

    if (typeIs(stripped, Array)) {
        node->type = makeSliceType(ctx->types, stripped->array.elementType);
    }
    else if (typeIs(unwrapped, Struct) || typeIs(unwrapped, Class)) {
        checkSliceOperator(visitor, node);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "slice operator (.[..]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});
        node->type = ERROR_TYPE(ctx);
    }
}

void checkIteratorExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *yieldType = checkType(visitor, node->iteratorExpr.type);
    if (typeIs(yieldType, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNodeList initArgs = {};
    for (AstNode *arg = node->iteratorExpr.args; arg; arg = arg->next) {
        csAssert0(nodeIs(arg, FieldDecl));
        AstNode *actualArg = arg->structField.value;

        const Type *argType = checkType(visitor, actualArg);
        if (typeIs(argType, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }

        arg->type = argType;
        arg->structField.type =
            makeTypeReferenceNode(ctx->pool, argType, &arg->loc);
        insertAstNode(&initArgs,
                      makeFieldExpr(ctx->pool,
                                    &actualArg->loc,
                                    arg->structField.name,
                                    flgNone,
                                    actualArg,
                                    arg,
                                    NULL));
        arg->structField.value = NULL;
    }

    AstNode *nextFunc = node->iteratorExpr.next;
    AstNode *truthyFunc = node->iteratorExpr.truthy;

    AstNode *members = node->iteratorExpr.args;
    if (members) {
        AstNode *last = getLastAstNode(members);
        last->next = nextFunc;
        nextFunc->next = truthyFunc;
    }
    else {
        members = nextFunc;
        nextFunc->next = truthyFunc;
    }

    cstring structName = makeAnonymousVariable(ctx->strings, "__Iterator");
    AstNode *anonStruct = makeStructDecl(ctx->pool,
                                         &node->loc,
                                         flgIterator | flgAnonymous,
                                         structName,
                                         members,
                                         NULL,
                                         NULL);

    astModifierAdd(&ctx->root, anonStruct);
    const Type *structType = checkType(visitor, anonStruct);
    if (typeIs(structType, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    AstNode *structPath = makeResolvedPath(ctx->pool,
                                           &node->loc,
                                           structName,
                                           flgNone,
                                           anonStruct,
                                           NULL,
                                           structType);

    clearAstBody(node);
    node->tag = astStructExpr;
    node->structExpr.left = structPath;
    node->structExpr.fields = initArgs.first;
    node->structExpr.isLiteral = false;
    node->type = structType;

    checkType(visitor, node);
}

void checkIndexExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->indexExpr.target);
    if (typeIs(target, Error)) {
        node->type = target;
        return;
    }

    const Type *index = checkType(visitor, node->indexExpr.index);
    if (typeIs(index, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags |= node->indexExpr.target->flags;
    const Type *unwrapped = stripReference(resolveUnThisUnwrapType(target)),
               *stripped = stripAll(target);
    if (typeIs(stripped, Array)) {
        if (!isIntegerType(index)) {
            logError(ctx->L,
                     &node->indexExpr.index->loc,
                     "unexpected array expression index type, expecting an "
                     "integer, got '{t}'",
                     (FormatArg[]){{.t = index}});
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = unwrapped->array.elementType;
    }
    else if (typeIs(unwrapped, Map)) {
        if (!isTypeAssignableFrom(unwrapped->map.key, index)) {
            logError(ctx->L,
                     &node->indexExpr.index->loc,
                     "unexpected map expression index type, expecting type "
                     "'{t}', got '{t}'",
                     (FormatArg[]){{.t = unwrapped->map.key}, {.t = index}});
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = unwrapped->map.value;
    }
    else if (typeIs(unwrapped, Struct) || typeIs(unwrapped, Class)) {
        checkIndexOperator(visitor, node);
    }
    else if (typeIs(unwrapped, Pointer)) {
        if (!isIntegerType(index)) {
            logError(
                ctx->L,
                &node->indexExpr.index->loc,
                "unexpected pointer offset expression index type, expecting an "
                "integer, got '{t}'",
                (FormatArg[]){{.t = index}});
            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = unwrapped->pointer.pointed;
    }
    else if (typeIs(unwrapped, String)) {
        if (!isIntegerType(index)) {
            logError(ctx->L,
                     &node->loc,
                     "unexpected index type on operator (.[]), expecting "
                     "integer, got '{t}'",
                     (FormatArg[]){{.t = index}});

            node->type = ERROR_TYPE(ctx);
        }
        else
            node->type = getPrimitiveType(ctx->types, prtCChar);
    }
    else {
        logError(ctx->L,
                 &node->loc,
                 "index operator (.[]) not supported on type '{t}'",
                 (FormatArg[]){{.t = target}});

        node->type = ERROR_TYPE(ctx);
    }
}
