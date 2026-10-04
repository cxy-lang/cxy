//
// Created by Carter Mbotho on 2025-01-09.
//

#include "check.h"

#include <lang/frontend/flag.h>
#include <lang/frontend/strings.h>
#include <lang/middle/builtins.h>

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

        if (expr == NULL) {
            if (ctx->catcher.block == NULL) {
                logError(ctx->L,
                         &node->loc,
                         "re-raise statement only allowed inside a catch block",
                         NULL);
                node->type = ERROR_TYPE(ctx);
                return false;
            }
            ctx->catcher.block->type =
                getResultTargetType(ctx->catcher.expr->type);
            return true;
        }

        const Type *type =
            nodeIs(expr, CastExpr) ? expr->castExpr.expr->type : expr->type;
        const Type *baseException = findBuiltinType(S_Exception);
        bool isException =
            baseException && isTypeAssignableFrom(baseException, type);
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

static bool isDiscardableCatch(AstNode *node)
{
    const Type *exprType = node->catchExpr.expr->type;
    if (isVoidResultType(exprType))
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

void checkCatchExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->catchExpr.expr;
    AstNode *var = node->catchExpr.var;
    AstNode *body = node->catchExpr.body;

    bool prevExplicitCatch = ctx->explicitCatch;
    ctx->explicitCatch = true;
    const Type *exprType = checkType(visitor, expr);
    ctx->explicitCatch = prevExplicitCatch;

    if (typeIs(exprType, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    exprType = resolveUnThisUnwrapType(exprType);
    if (!isResultType(exprType)) {
        logError(ctx->L,
                 &expr->loc,
                 "catch used with an expression that does not raise an exception",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (var) {
        var->type = findBuiltinType(S_Exception) ?: makeVoidType(ctx->types);
    }

    const Type *targetType = isVoidResultType(exprType)
                                 ? makeVoidType(ctx->types)
                                 : getResultTargetType(exprType);

    bool prevReturnState = ctx->returnState;
    ctx->returnState = false;
    struct ExceptionCather prevCatcher = ctx->catcher;
    ctx->catcher = (struct ExceptionCather){
        .block = body,
        .expr = expr,
        .variable = var,
        .hasBreakOrContinue = false};

    const Type *bodyType = checkType(visitor, body);
    bool bodyReturns = ctx->returnState;
    ctx->returnState = prevReturnState;
    ctx->catcher = prevCatcher;

    if (typeIs(bodyType, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isDiscardExpr(body)) {
        if (!isDiscardableCatch(node)) {
            logError(ctx->L,
                     &body->loc,
                     "cannot discard exception when a value is expected in this context",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = makeVoidType(ctx->types);
        return;
    }

    if (!bodyReturns && !isDiscardableCatch(node)) {
        if (isVoidType(bodyType)) {
            logError(ctx->L,
                     &body->loc,
                     "catch block must yield a default value",
                     NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!isTypeAssignableFrom(targetType, bodyType)) {
            logError(
                ctx->L,
                &body->loc,
                "catch default value type `{t}` not assignable to expression "
                "value type `{t}`",
                (FormatArg[]){{.t = bodyType}, {.t = targetType}});
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
}
