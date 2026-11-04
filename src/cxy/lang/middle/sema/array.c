//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include <string.h>

static AstNode *getArrayDimension(AstNode *node)
{
    if (node == NULL)
        return node;
    const Type *type = node->type;
    switch (node->tag) {
    case astCastExpr:
        return node->castExpr.expr;
    case astTypedExpr:
        return node->typedExpr.expr;
    default:
        return node;
    }
}

static const Type *synthesizeArrayDestructor(TypingContext *ctx,
                                              AstVisitor *visitor,
                                              const Type *arrayType)
{
    const FileLoc *loc = builtinLoc();
    const Type *sliceType = makeSliceType(ctx->types, arrayType->array.elementType);
    if (sliceType->array.destructorFunc)
        return sliceType->array.destructorFunc;

    cstring funcName = makeStringf(ctx->strings, "__array_dctor_%u", (unsigned)sliceType->index);
    cstring paramName = makeString(ctx->strings, "arr");
    cstring elemVarName = makeString(ctx->strings, "__i");

    AstNode *paramType = makeTypeReferenceNode(ctx->pool, sliceType, loc);
    AstNode *param = makeFunctionParam(ctx->pool, loc, paramName, paramType, NULL, flgNone, NULL);
    param->type = sliceType;

    const Type *elemType = arrayType->array.elementType;

    // elem in slice
    AstNode *loopVar =
        makeVarDecl(ctx->pool, loc, flgNone, elemVarName,
                    makeTypeReferenceNode(ctx->pool, getPrimitiveType(ctx->types, prtU64), loc),
                    NULL, NULL, getPrimitiveType(ctx->types, prtU64));
    AstNode *rangeExpr = makeAstNode(ctx->pool, loc, &(AstNode){
        .tag = astRangeExpr,
        .rangeExpr = {
            .start = makeIntegerLiteral(ctx->pool, loc, 0, NULL, getPrimitiveType(ctx->types, prtU64)),
            .end = makeBackendCallExpr(
                ctx->pool,
                loc, flgNone,
                bfiLen,
                makeResolvedPath(ctx->pool, loc, paramName, flgNone, param, NULL, sliceType),
                getPrimitiveType(ctx->types, prtU64)),
            .step = NULL,
        }
    });

    AstNode *dropCall = makeExprStmt(
        ctx->pool, loc, flgNone,
        makeBackendCallExpr(ctx->pool, loc, flgNone, bfiDrop,
            makeIndexExpr(ctx->pool, loc, flgNone, makeResolvedPath(ctx->pool, loc, paramName, flgNone, param, NULL, sliceType),
                makeResolvedPath(ctx->pool, loc, elemVarName, 0, loopVar, NULL, NULL),
                NULL, sliceType->array.elementType),
            makeVoidType(ctx->types)),
        NULL, makeVoidType(ctx->types));

    AstNode *forBody = makeBlockStmt(ctx->pool, loc, dropCall, NULL, makeVoidType(ctx->types));
    AstNode *forStmt = makeForStmt(ctx->pool, loc, flgNone, loopVar, rangeExpr, NULL, forBody, NULL);

    AstNode *body = makeBlockStmt(ctx->pool, loc, forStmt, NULL, makeVoidType(ctx->types));
    AstNode *funcDecl = makeFunctionDecl(ctx->pool, loc, funcName, param, makeVoidAstNode(ctx->pool, loc, flgNone, NULL, NULL), body, flgGenerated | flgPure | flgBuiltin, NULL, NULL);
    markGlue(funcDecl);

    const Type *funcType = checkType(visitor, funcDecl);
    if (!typeIs(funcType, Error)) {
        ((Type *)sliceType)->array.destructorFunc = funcType;
        if (ctx->root.parent)
            astModifierAddAsLast(&ctx->root, funcDecl);
    }
    return funcType;
}

void implementArrayTypeCopyAndDestructor(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (!typeIs(node->type, Array) || hasFlag(node->type, Slice) || node->type->array.len == UINT64_MAX)
        return;

    const Type *sliceType = makeSliceType(ctx->types, node->type->array.elementType);
    if (node->type->array.destructorFunc == NULL && sliceType->array.destructorFunc != NULL) {
        ((Type *)node->type)->array.destructorFunc = sliceType->array.destructorFunc;
        return;
    }

    if (node->type->array.destructorFunc != NULL || !isDestructible(node->type))
        return;

    const Type *func = synthesizeArrayDestructor(ctx, visitor, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->array.destructorFunc = func;
    ((Type *)sliceType)->array.destructorFunc = func;
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *element = checkType(visitor, node->arrayType.elementType);
    if (typeIs(element, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    u64 size = UINT64_MAX;
    const Type *dim_ = checkType(visitor, node->arrayType.dim);
    if (typeIs(dim_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    if (typeIs(dim_, Literal)) {
        if (!nodeIs(dim_->literal.value, IntegerLit)) {
            logError(ctx->L,
                &node->loc,
                "expecting array dimension to be constant integral type "
                "at compile time",
                NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        clearAstBody(node->arrayType.dim);
        node->arrayType.dim->tag = astIntegerLit;
        node->arrayType.dim->intLiteral.value =
            dim_->literal.value->intLiteral.value;
    }

    AstNode *dim = getArrayDimension(node->arrayType.dim);
    if (dim == NULL) {
        node->type = makeArrayType(ctx->types, element, size);
        return;
    }

    if (!nodeIs(dim, IntegerLit)) {
        logError(ctx->L,
                 &node->loc,
                 "expecting array dimension to be constant integral type "
                 "at compile time",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }
    size = integerLiteralValue(dim);
    node->type = makeArrayType(ctx->types, element, size);
    implementArrayTypeCopyAndDestructor(visitor, node);
}

void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    bool isLiteral = true;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = checkType(visitor, elem);
        isLiteral = isLiteral && isLiteralExpr(elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(elementType, type)) {
            logError(ctx->L,
                     &elem->loc,
                     "inconsistent array types in array, expecting '{t}', "
                     "got '{t}'",
                     (FormatArg[]){{.t = elementType}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        // TODO maybe cast
        elem->type = elementType;
    }

    node->arrayExpr.isLiteral = isLiteral;
    if (elementType == NULL) {
        node->type = makeArrayType(ctx->types, makeAutoType(ctx->types), 0);
    }
    else {
        node->type = makeArrayType(ctx->types, elementType, count);
    }

    implementArrayTypeCopyAndDestructor(visitor, node);
}
