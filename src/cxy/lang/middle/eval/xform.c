//
// Created by Carter Mbotho on 2025-03-12.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/middle/sema/check.h"

#include <core/alloc.h>

static bool evalXformCondition(AstVisitor *visitor, AstNode *node, bool *result)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = deepCloneAstNode(ctx->pool, node);
    if (!evaluate(visitor, cond)) {
        return false;
    }
    if (!nodeIs(cond, BoolLit)) {
        logError(ctx->L,
                 &node->loc,
                 "comptime transform condition must evaluate to a "
                 "boolean literal",
                 NULL);
        return false;
    }
    result[0] = cond->boolLiteral.value;
    return true;
}

static void evalXformRangeExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->xForm.target;
    if (!nodeIs(range->rangeExpr.start, IntegerLit) ||
        !nodeIs(range->rangeExpr.end, IntegerLit)) {
        logError(ctx->L,
                 &range->loc,
                 "unsupported range expression for transform expression, "
                 "start and stop must be compile time integer literals",
                 NULL);
        node->tag = astError;
        return;
    }

    AstNode *arg = node->xFormExpr.arg;
    bool down = range->rangeExpr.down;
    i64 start = nodeGetNumericLiteral(range->rangeExpr.start);
    i64 end = nodeGetNumericLiteral(range->rangeExpr.end);
    i64 step = range->rangeExpr.step
                   ? nodeGetNumericLiteral(range->rangeExpr.step)
                   : (down ? -1 : 1);
    if (start > end) {
        logError(ctx->L,
                 &range->loc,
                 "unsupported range expression for transform expression, "
                 "start must be less than or equal to stop",
                 NULL);
        node->tag = astError;
    }
    if (down) {
        i64 tmp = start;
        start = end;
        end = tmp;
    }

    AstNodeList members = {};
    Flags flg = flgNone;
    for (i64 i = start; i != end; i += step) {
        arg->varDecl.init =
            makeIntegerLiteral(ctx->pool,
                               &arg->loc,
                               i,
                               NULL,
                               getPrimitiveType(ctx->types, prtI64));
        arg->type = arg->varDecl.init->type;
        if (node->xForm.cond) {
            bool cond = true;
            if (!evalXformCondition(visitor, node->xForm.cond, &cond)) {
                node->tag = astError;
                return;
            }
            if (!cond)
                continue;
        }
        AstNode *xForm = deepCloneAstNode(ctx->pool, node->xForm.xForm);
        const Type *result = evalType(ctx, xForm);
        if (typeIs(result, Error)) {
            node->tag = astError;
            return;
        }
        insertAstNode(&members, xForm);
        if (isClassType(xForm->type) ||
            hasFlag(xForm->type, ReferenceMembers)) {
            flg |= flgReferenceMembers;
        }
    }
    if (node->xFormExpr.isArray) {
        node->tag = astAliasExpr;
        clearAstBody(node);
        node->flags = flg;
        node->type = NULL;
        node->arrayExpr.elements = members.first;
        node->arrayExpr.len = countAstNodes(members.first);
        node->arrayExpr.isLiteral = false;
    }
    else {
        node->tag = astTupleExpr;
        clearAstBody(node);
        node->flags = flg;
        node->type = NULL;
        node->tupleExpr.elements = members.first;
        node->tupleExpr.len = countAstNodes(members.first);
        node->tupleExpr.isLiteral = false;
    }
    astVisit(ctx->typer, node);
}

static void evalXformArrayExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    const Type *target = resolveUnThisUnwrapType(node->xForm.target->type);

    AstNode *arg = node->xFormExpr.arg;
    AstNodeList members = {};
    Flags flg = flgNone;
    for (u64 i = 0; i < target->array.len; i++) {
        arg->varDecl.init =
            makeIntegerLiteral(ctx->pool,
                               &arg->loc,
                               i,
                               NULL,
                               getPrimitiveType(ctx->types, prtI64));
        arg->type = arg->varDecl.init->type;

        if (node->xForm.cond) {
            bool cond = true;
            if (!evalXformCondition(visitor, node->xForm.cond, &cond)) {
                node->tag = astError;
                return;
            }
            if (!cond)
                continue;
        }
        AstNode *xForm = deepCloneAstNode(ctx->pool, node->xForm.xForm);
        const Type *result = evalType(ctx, xForm);
        if (typeIs(result, Error)) {
            node->tag = astError;
            return;
        }
        insertAstNode(&members, xForm);
        if (isClassType(xForm->type) ||
            hasFlag(xForm->type, ReferenceMembers)) {
            flg |= flgReferenceMembers;
        }
    }
    if (node->xFormExpr.isArray) {
        node->tag = astAliasExpr;
        clearAstBody(node);
        node->flags = flg;
        node->type = NULL;
        node->arrayExpr.elements = members.first;
        node->arrayExpr.len = countAstNodes(members.first);
        node->arrayExpr.isLiteral = false;
    }
    else {
        node->tag = astTupleExpr;
        clearAstBody(node);
        node->flags = flg;
        node->type = NULL;
        node->tupleExpr.elements = members.first;
        node->tupleExpr.len = countAstNodes(members.first);
        node->tupleExpr.isLiteral = false;
    }
    astVisit(ctx->typer, node);
}

static void evalXformTupleExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    const Type *target = resolveUnThisUnwrapType(node->xForm.target->type);

    AstNode *arg = node->xFormExpr.arg;
    AstNodeList members = {};
    Flags flg = flgNone;
    for (u64 i = 0; i < target->tuple.count; i++) {
        arg->varDecl.init =
            makeIntegerLiteral(ctx->pool,
                               &arg->loc,
                               i,
                               NULL,
                               getPrimitiveType(ctx->types, prtI64));
        arg->type = arg->varDecl.init->type;

        if (node->xForm.cond) {
            bool cond = true;
            if (!evalXformCondition(visitor, node->xForm.cond, &cond)) {
                node->tag = astError;
                return;
            }
            if (!cond)
                continue;
        }
        AstNode *xForm = deepCloneAstNode(ctx->pool, node->xForm.xForm);
        const Type *result = evalType(ctx, xForm);
        if (result == NULL) {
            node->tag = astError;
            return;
        }
        insertAstNode(&members, xForm);
        if (isClassType(xForm->type) ||
            hasFlag(xForm->type, ReferenceMembers)) {
            flg |= flgReferenceMembers;
        }
    }
    if (node->xFormExpr.isArray) {
        node->tag = astArrayExpr;
        clearAstBody(node);
        node->flags = flg;
        node->type = NULL;
        node->arrayExpr.elements = members.first;
        node->arrayExpr.len = countAstNodes(members.first);
        node->arrayExpr.isLiteral = false;
    }
    else {
        node->tag = astTupleExpr;
        clearAstBody(node);
        node->flags = flg;
        node->type = NULL;
        node->tupleExpr.elements = members.first;
        node->tupleExpr.len = countAstNodes(members.first);
        node->tupleExpr.isLiteral = false;
    }
    astVisit(ctx->typer, node);
}

void evalXformType(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!evaluate(visitor, node->xForm.target)) {
        node->tag = astError;
        return;
    }
    const Type *target = resolveUnThisUnwrapType(node->xForm.target->type);
    if (!typeIs(target, Tuple)) {
        logError(ctx->L,
                 &node->xForm.target->loc,
                 "unsupported type {t}, transforms only supported on tuple "
                 "extressions",
                 (FormatArg[]){{.t = target ?: makeErrorType(ctx->types)}});
        node->tag = astError;
        return;
    }

    const Type **members =
        mallocOrDie(sizeof(const Type *) * target->tuple.count);
    AstNode *arg1 = node->xForm.args, *arg2 = arg1->next;
    int j = 0;
    Flags flg = flgNone;
    for (int i = 0; i < target->tuple.count; i++) {
        const Type *member = target->tuple.members[i];
        arg1->varDecl.init =
            makeTypeReferenceNode(ctx->pool, member, &arg1->loc);
        arg1->type = member;
        if (arg2) {
            arg2->varDecl.init =
                makeIntegerLiteral(ctx->pool,
                                   &arg2->loc,
                                   i,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtI64));
            arg2->type = arg2->varDecl.init->type;
        }

        if (node->xForm.cond) {
            bool cond = true;
            if (!evalXformCondition(visitor, node->xForm.cond, &cond)) {
                node->tag = astError;
                free(members);
                return;
            }
            if (!cond)
                continue;
        }

        AstNode *xForm = deepCloneAstNode(ctx->pool, node->xForm.xForm);
        const Type *result = evalType(ctx, xForm);
        if (typeIs(result, Error)) {
            node->tag = astError;
            free(members);
            return;
        }
        members[j++] = xForm->type;
        if (isClassType(xForm->type) ||
            hasFlag(xForm->type, ReferenceMembers)) {
            flg |= flgReferenceMembers;
        }
    }

    const Type *type = makeTupleType(ctx->types, members, j, flg);
    free(members);
    node->tag = astTypeRef;
    node->type = type;
    clearAstBody(node);
    if (flg & flgReferenceMembers) {
        implementTupleTypeCopyAndDestructor(visitor, node);
    }
}

void evalXformExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    const Type *target;
    if (!evaluate(visitor, node->xFormExpr.target)) {
        node->tag = astError;
        return;
    }
    target = node->xFormExpr.target->type
                 ?: checkType(ctx->typer, node->xFormExpr.target);
    if (nodeIs(node->xFormExpr.target, RangeExpr)) {
        evalXformRangeExpr(visitor, node);
    }
    else if (isArrayType(target)) {
        evalXformArrayExpr(visitor, node);
    }
    else if (isTupleType(target)) {
        evalXformTupleExpr(visitor, node);
    }
    else {
        logError(ctx->L,
                 &node->xFormExpr.target->loc,
                 "unsupported expression of type {t} for transform expression, "
                 "it must be a tuple, array or range",
                 (FormatArg[]){{.t = target ?: makeErrorType(ctx->types)}});
        node->tag = astError;
    }
}