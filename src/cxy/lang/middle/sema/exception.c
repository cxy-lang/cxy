//
// Created by Carter Mbotho on 2025-01-09.
//

#include "check.h"

#include <lang/frontend/flag.h>
#include <lang/frontend/strings.h>

bool exceptionVerifyRaiseExpr(TypingContext *ctx,
                              const AstNode *ret,
                              AstNode *node)
{
    if (node->returnStmt.isRaise) {
        AstNode *expr = node->returnStmt.expr;
        if (ret == NULL || !isResultType(ret->type)) {
            logError(
                ctx->L,
                &node->loc,
                "function return type must be marked as explicitly raising",
                NULL);
            node->type = ERROR_TYPE(ctx);
            return false;
        }

        const Type *type =
            nodeIs(expr, CastExpr) ? expr->castExpr.expr->type : expr->type;
        bool isException = nodeIs(expr, CastExpr) ? isExceptionType(type)
                                                  : isBaseExceptionType(type);
        if (!isException) {
            logError(ctx->L,
                     &expr->loc,
                     "expecting an exception, got expression of type {t}",
                     (FormatArg[]){{.t = type}});
            node->type = ERROR_TYPE(ctx);
            return false;
        }

        if (ctx->catcher.block != NULL) {
            ctx->catcher.block->type =
                getResultTargetType(ctx->catcher.expr->type);
        }
    }
    return true;
}

static bool isDiscardableException(AstNode *node)
{
    const Type *lhs = node->binaryExpr.lhs->type;
    if (isVoidResultType(lhs))
        return true;
    AstNode *parent = node->parentScope;
    switch (parent->tag) {
    case astVarDecl:
#define f(TAG) case ast##TAG:
        CXY_LANG_AST_EXP_TAGS(f)
        return false;
#undef f
    default:
        return true;
    }
}

static bool isDiscardExpr(const AstNode *node)
{
    return nodeIs(node, BlockStmt) && node->blockStmt.stmts == NULL;
}

void checkCatchBinaryOperator(AstVisitor *visitor,
                              AstNode *node,
                              struct ExceptionCather *catcher)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    const Type *lhsType = resolveUnThisUnwrapType(lhs->type),
               *rhsType = resolveUnThisUnwrapType(rhs->type);

    if (!isResultType(lhsType)) {
        logError(ctx->L,
                 &lhs->loc,
                 "catch used with a statement that does not throw",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *targetType =
        isVoidResultType(lhsType)
            ? makeVoidType(ctx->types)
            : (isDiscardExpr(rhs) ? makeVoidType(ctx->types)
                                  : getResultTargetType(lhsType));

    if (!ctx->returnState && !catcher->hasBreakOrContinue && !isDiscardExpr(rhs)) {
        if (isVoidType(rhsType) && !isDiscardableException(node)) {
            logError(ctx->L,
                     &rhs->loc,
                     "catch block must yield a default value",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!isTypeAssignableFrom(targetType, rhsType)) {
            logError(
                ctx->L,
                &rhs->loc,
                "catch default value type `{t}` not assignable to expression "
                "value type `{t}`",
                (FormatArg[]){{.t = rhsType}, {.t = targetType}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    node->type = targetType;
}

void checkCallExceptionBubbleUp(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *type = node->type;
    if (!isResultType(type))
        return;
    AstNode *func = ctx->currentEnclosure;
    const Type *ret = NULL;
    if (nodeIs(func, FuncDecl)) {
        ret = func->type->func.retType;
    }
    else {
        ret =
            func && func->closureExpr.ret ? func->closureExpr.ret->type : NULL;
    }

    if (!isResultType(ret)) {
        logError(ctx->L,
                 &node->loc,
                 "all exception must be handled when callees that raise are "
                 "called in a non-throwing function",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isVoidResultType(type)) {
        node->type = getResultTargetType(type);
    }
    else {
        node->type = makeVoidType(ctx->types);
    }
}
