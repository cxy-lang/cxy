//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/builtins.h"

#include <core/alloc.h>

static const Type *synthesizeTupleDestructor(TypingContext *ctx,
                                              AstVisitor *visitor,
                                              const Type *tupleType)
{
    const FileLoc *loc = builtinLoc();
    cstring funcName = makeStringf(ctx->strings, "__tuple_dctor_%u", (unsigned)tupleType->index);
    cstring paramName = makeString(ctx->strings, "tup");

    const Type *paramRefType = makeReferenceType(ctx->types, tupleType, flgNone);
    AstNode *paramType = makeTypeReferenceNode(ctx->pool, paramRefType, loc);
    AstNode *param = makeFunctionParam(ctx->pool, loc, paramName, paramType, NULL, flgNone, NULL);
    param->type = paramRefType;

    AstNodeList stmts = {};
    for (u64 i = 0; i < tupleType->tuple.count; i++) {
        const Type *memberType = tupleType->tuple.members[i];
        if (!isDestructible(memberType))
            continue;

        AstNode *memberAccess = makeMemberExpr(
            ctx->pool, loc, flgNone,
            makeResolvedIdentifier(ctx->pool, loc, paramName, flgNone, param, NULL, paramRefType),
            makeIntegerLiteral(ctx->pool, loc, i, NULL, getPrimitiveType(ctx->types, prtU64)),
            NULL, memberType);

        AstNode *dropCall = makeExprStmt(
            ctx->pool, loc, flgNone,
            makeBackendCallExpr(ctx->pool, loc, flgNone, bfiDrop, memberAccess, makeVoidType(ctx->types)),
            NULL, makeVoidType(ctx->types));

        insertAstNode(&stmts, dropCall);
    }

    AstNode *body = makeBlockStmt(ctx->pool, loc, stmts.first, NULL, makeVoidType(ctx->types));
    AstNode *funcDecl = makeFunctionDecl(ctx->pool, loc, funcName, param, makeVoidAstNode(ctx->pool, loc, flgNone, NULL, NULL), body, flgGenerated | flgPure | flgBuiltin, NULL, NULL);

    const Type *funcType = checkType(visitor, funcDecl);
    if (!typeIs(funcType, Error) && ctx->root.parent) {
        astModifierAddAsLast(&ctx->root, funcDecl);
    }
    return funcType;
}

static const Type *synthesizeTupleCopy(TypingContext *ctx,
                                       AstVisitor *visitor,
                                       const Type *tupleType)
{
    const FileLoc *loc = builtinLoc();
    cstring funcName = makeStringf(ctx->strings, "__tuple_copy_%u", (unsigned)tupleType->index);
    cstring paramName = makeString(ctx->strings, "tup");

    const Type *paramRefType = makeReferenceType(ctx->types, tupleType, flgNone);
    AstNode *paramType = makeTypeReferenceNode(ctx->pool, paramRefType, loc);
    AstNode *param = makeFunctionParam(ctx->pool, loc, paramName, paramType, NULL, flgNone, NULL);
    param->type = paramRefType;

    AstNodeList exprs = {};
    for (u64 i = 0; i < tupleType->tuple.count; i++) {
        const Type *memberType = tupleType->tuple.members[i];
        AstNode *memberAccess = makeMemberExpr(
            ctx->pool, loc, flgNone,
            makeResolvedIdentifier(ctx->pool, loc, paramName, flgNone, param, NULL, paramRefType),
            makeIntegerLiteral(ctx->pool, loc, i, NULL, getPrimitiveType(ctx->types, prtU64)),
            NULL, memberType);

        AstNode *elemExpr = memberAccess;
        if (isDestructible(memberType) || isClassType(memberType)) {
            elemExpr = makeBackendCallExpr(ctx->pool, loc, flgNone, bfiCopy, memberAccess, memberType);
        }
        insertAstNode(&exprs, elemExpr);
    }

    AstNode *retTuple = makeTupleExpr(ctx->pool, loc, flgNone, exprs.first, NULL, tupleType);
    AstNode *body = makeBlockStmt(ctx->pool, loc, makeReturnAstNode(ctx->pool, loc, flgNone, retTuple, NULL, tupleType), NULL, NULL);
    AstNode *retTypeNode = makeTypeReferenceNode(ctx->pool, tupleType, loc);
    AstNode *funcDecl = makeFunctionDecl(ctx->pool, loc, funcName, param, retTypeNode, body, flgGenerated | flgPure | flgBuiltin, NULL, NULL);

    const Type *funcType = checkType(visitor, funcDecl);
    if (!typeIs(funcType, Error) && ctx->root.parent) {
        astModifierAddAsLast(&ctx->root, funcDecl);
    }
    return funcType;
}

void implementTupleTypeCopyAndDestructor(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    bool hasCopy = node->type->tuple.copyFunc != NULL;
    if (hasCopy || !isDestructible(node->type))
        return;

    const Type *func = synthesizeTupleCopy(ctx, visitor, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->tuple.copyFunc = func;

    func = synthesizeTupleDestructor(ctx, visitor, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->tuple.destructorFunc = func;
}

void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elements = node->tupleExpr.elements, *element = elements;

    AstNode *prev = NULL;
    for (u64 i = 0; element; element = element->next, i++) {
        if (nodeIs(element, Noop)) {
            if (prev == NULL)
                node->tupleExpr.elements = element->next;
            else
                prev->next = element->next;
            continue;
        }
        prev = element;

        const Type *type = checkType(visitor, element);
        if (typeIs(type, Error)) {
            node->type = element->type;
            continue;
        }

        type = unwrapType(type, NULL);
        if (isClassType(type) || isDestructible(type)) {
            node->flags |= flgReferenceMembers;
        }
        else if (isStructType(type)) {
            AstNode *decl = getTypeDecl(type);
            node->flags |= (decl->flags & flgReferenceMembers);
        }
        else if (isTupleType(type) || isUnionType(type)) {
            node->flags |= (type->flags & flgReferenceMembers);
        }
    }

    if (typeIs(node->type, Error))
        return;

    node->tupleExpr.len = countAstNodes(node->tupleExpr.elements);
    const Type **elements_ = mallocOrDie(sizeof(Type *) * node->tupleExpr.len);
    element = node->tupleExpr.elements;
    for (u64 i = 0; element; element = element->next, i++) {
        elements_[i] = element->type;
    }

    node->type = makeTupleType(
        ctx->types,
        elements_,
        node->tupleExpr.len,
        node->flags & (flgReferenceMembers | flgConst | flgReference));

    free(elements_);

    implementTupleTypeCopyAndDestructor(visitor, node);
}

// A tuple literal stored where a tuple type is expected takes that type, so
// `null` and plain values for optional members are converted by lowering.
void coerceTupleExpr(AstNode *expr, const Type *target)
{
    target = resolveUnThisUnwrapType(target);
    if (!nodeIs(expr, TupleExpr) || !typeIs(target, Tuple) ||
        target->tuple.count != countAstNodes(expr->tupleExpr.elements))
        return;

    u64 i = 0;
    for (AstNode *elem = expr->tupleExpr.elements; elem;
         elem = elem->next, i++) {
        const Type *member = target->tuple.members[i];
        if (nodeIs(elem, TupleExpr)) {
            coerceTupleExpr(elem, member);
        }
        else if (isOptionalType(member) && !isOptionalType(elem->type)) {
            const Type *inner = getOptionalTargetType(member);
            elem->type = nodeIs(elem, NullLit) || inner == NULL ? member : inner;
        }
        else if (nodeIs(elem, NullLit) && isStructType(member)) {
            elem->type = member;
        }
    }
    expr->type = target;
}

void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elems = node->tupleType.elements, *elem = elems;
    u64 count = node->tupleType.len ?: countAstNodes(node->tupleType.elements);
    const Type **elems_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; elem; elem = elem->next, i++) {
        elems_[i] = checkType(visitor, elem);
        const Type *type = unwrapType(elems_[i], NULL);
        if (typeIs(elems_[i], Error)) {
            node->type = ERROR_TYPE(ctx);
        }
        else if (isClassType(type) || isDestructible(type)) {
            node->flags |= flgReferenceMembers;
        }
        else if (isTupleType(type)) {
            node->flags |= (type->flags & flgReferenceMembers);
        }
        else if (isStructType(type) && !hasFlag(type, Closure)) {
            AstNode *decl = getTypeDecl(type);
            node->flags |= (decl->flags & flgReferenceMembers);
        }
    }

    if (typeIs(node->type, Error)) {
        free(elems_);
        return;
    }

    node->type = makeTupleType(
        ctx->types,
        elems_,
        count,
        node->flags & (flgReferenceMembers | flgConst | flgReference));

    free(elems_);

    implementTupleTypeCopyAndDestructor(visitor, node);
}
