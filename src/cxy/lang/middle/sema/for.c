//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static const Type *findCustomRange(TypingContext *ctx,
                                   const Type *type,
                                   const FileLoc *loc,
                                   u64 flags)
{
    type = stripReference(type);
    const Type *rangeOp = findMemberInType(type, S_Range);
    if (rangeOp) {
        rangeOp = matchOverloadedFunction(
            ctx->L, rangeOp, (const Type *[]){}, 0, loc, flags & flgConst);
    }
    else {
        type = getTypeBase(type);
        if (type) {
            return findCustomRange(ctx, type, loc, flags);
        }
    }

    return rangeOp;
}

static const Type *findIteratorType(TypingContext *ctx, AstNode *range)
{
    const Type *rangeOp =
        findCustomRange(ctx, range->type, &range->loc, range->flags);
    if (rangeOp == NULL) {
        logError(ctx->L,
                 &range->loc,
                 "type `{t}` does not have a range overload operator",
                 (FormatArg[]){{.t = range->type}});
        return NULL;
    }

    const Type *retType = rangeOp->func.retType;
    const Type *callOp = findStructMemberType(retType, S_CallOverload);
    if (!typeIs(callOp, Func)) {
        logError(ctx->L,
                 &range->loc,
                 "iterator type `{t}` must implement the `()` call operator",
                 (FormatArg[]){{.t = retType}});
        return NULL;
    }

    const Type *truthyOp = findStructMemberType(retType, S_Truthy);
    if (truthyOp == NULL) {
        logError(ctx->L,
                 &range->loc,
                 "iterator type `{t}` must implement the `!!` truthy operator",
                 (FormatArg[]){{.t = retType}});
        return NULL;
    }

    return callOp;
}

static bool checkForStmtCustomRange(TypingContext *ctx,
                                    AstNode *node,
                                    u64 variablesCount,
                                    const Type *range)
{
    AstNode *variable = node->forStmt.var;

    const Type *iterator = findIteratorType(ctx, node->forStmt.range);
    if (iterator == NULL)
        return false;

    const Type *value = getOptionalTargetType(iterator->func.retType);
    if (variable->next == NULL) {
        variable->type = value;
        return true;
    }

    if (!typeIs(value, Tuple)) {
        logError(ctx->L,
                 &node->forStmt.range->loc,
                 "multi-variable `for` loop used with a custom iterator "
                 "requires an iterator that returns a tuple",
                 NULL);

        logNote(ctx->L,
                &iterator->func.decl->loc,
                "iterator defined here returns '{t}'",
                (FormatArg[]){{.t = value}});
        return false;
    }

    if (variablesCount > value->tuple.count) {
        logError(
            ctx->L,
            manyNodesLoc(variable),
            "unexpected number of variables got {u64}, expecting at most {u64}",
            (FormatArg[]){{.u64 = variablesCount},
                          {.u64 = value->tuple.count}});
        return false;
    }

    for (u64 i = 0; variable; variable = variable->next, i++) {
        if (isIgnoreVar(variable->varDecl.name))
            continue;

        const Type *member = value->tuple.members[i];
        if (hasFlag(member, Const) && !hasFlag(variable, Const)) {
            logError(ctx->L,
                     &variable->loc,
                     "cannot bind a const value to a non constant variable",
                     NULL);
            return false;
        }
        variable->type = member;
    }

    return true;
}

// A plain variable or member chain: evaluating it twice has no side effects
static bool isStableLValue(AstNode *node)
{
    if (nodeIs(node, Identifier) || nodeIs(node, Path))
        return nodeIsLeftValue(node);
    if (nodeIs(node, MemberExpr))
        return nodeIsLeftValue(node) && isStableLValue(node->memberExpr.target);
    return false;
}

static AstNode *makeSequenceRef(TypingContext *ctx,
                                const FileLoc *loc,
                                AstNode *range,
                                AstNode *seqDecl,
                                cstring seqName)
{
    if (seqDecl == NULL)
        return deepCloneAstNode(ctx->pool, range);
    return makeResolvedPath(
        ctx->pool, loc, seqName, flgNone, seqDecl, NULL, range->type);
}

static void transformForSequence(AstVisitor *visitor,
                                 AstNode *node,
                                 u64 numVariables)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    FileLoc loc = node->loc;
    AstNode *range = node->forStmt.range;
    AstNode *var = node->forStmt.var;
    AstNode *body = node->forStmt.body;
    const Type *u64Type = getPrimitiveType(ctx->types, prtU64);

    // An lvalue is iterated in place, a copy of it would duplicate (and then
    // drop) every element. Anything else is evaluated once into `_seq`.
    cstring seqName = makeAnonymousVariable(ctx->strings, "_seq");
    AstNode *seqDecl = NULL;
    if (!isStableLValue(range)) {
        seqDecl = makeVarDecl(
            ctx->pool, &loc, flgNone, seqName, NULL, range, NULL, range->type);
        astModifierAdd(&ctx->blockModifier, seqDecl);
    }

    AstNode *lenExpr =
        makeBackendCallExpr(ctx->pool,
                            &loc,
                            flgNone,
                            bfiLen,
                            makeSequenceRef(ctx, &loc, range, seqDecl, seqName),
                            u64Type);

    cstring idxName = makeAnonymousVariable(ctx->strings, "_idx");
    AstNode *idxVar = makeVarDecl(
        ctx->pool, &loc, flgNone, idxName, NULL, NULL, NULL, u64Type);
    AstNode *rangeExpr = makeAstNode(
        ctx->pool,
        &loc,
        &(AstNode){.tag = astRangeExpr,
                   .type = u64Type,
                   .rangeExpr = {.start = makeIntegerLiteral(
                                     ctx->pool, &loc, 0, NULL, u64Type),
                                 .end = lenExpr,
                                 .step = NULL,
                                 .down = false}});

    AstNode *elemInit = makeIndexExpr(
        ctx->pool,
        &loc,
        flgNone,
        makeSequenceRef(ctx, &loc, range, seqDecl, seqName),
        makeResolvedPath(
            ctx->pool, &loc, idxName, flgNone, idxVar, NULL, u64Type),
        NULL,
        var->type);

    AstNode *nextVar = var->next;
    var->next = NULL;

    cstring varName = var->varDecl.name ?: (var->varDecl.names ? var->varDecl.names->ident.value : var->_namedNode.name);
    clearAstBody(var);
    var->tag = astAliasExpr;
    var->aliasExpr.name = varName;
    var->aliasExpr.expr = elemInit;
    var->type = elemInit->type;

    AstNodeList bodyStmts = {};
    insertAstNode(&bodyStmts, var);

    if (nextVar) {
        AstNode *userI = nextVar;
        userI->next = NULL;
        cstring iName = userI->varDecl.name ?: (userI->varDecl.names ? userI->varDecl.names->ident.value : userI->_namedNode.name);
        if (iName && !isIgnoreVar(iName)) {
            clearAstBody(userI);
            userI->tag = astAliasExpr;
            userI->aliasExpr.name = iName;
            userI->aliasExpr.expr = makeResolvedPath(
                ctx->pool, &loc, idxName, flgNone, idxVar, NULL, u64Type);
            userI->type = u64Type;
            insertAstNode(&bodyStmts, userI);
        }
    }

    insertAstNode(&bodyStmts, body->blockStmt.stmts);
    body->blockStmt.stmts = bodyStmts.first;

    node->forStmt.var = idxVar;
    node->forStmt.range = rangeExpr;

    checkType(visitor, node);
}

static void transformForCustomRange(AstVisitor *visitor,
                                    AstNode *node,
                                    u64 variablesCount)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *vars = node->forStmt.var,
            *body = node->forStmt.body;

    const Type *rangeOperator =
                   findCustomRange(ctx, range->type, &range->loc, range->flags),
               *callOp = findStructMemberType(rangeOperator->func.retType,
                                              S_CallOverload);
    AstNode *rangeOperatorDecl = rangeOperator->func.decl;

    cstring itVarName = makeAnonymousVariable(ctx->strings, "__it");
    AstNode *rangeCall = makeCallExpr(
        ctx->pool,
        &range->loc,
        makeMemberExpr(ctx->pool,
                       &range->loc,
                       range->flags,
                       range,
                       makeResolvedPath(ctx->pool,
                                        &range->loc,
                                        S_Range,
                                        rangeOperatorDecl->flags,
                                        rangeOperatorDecl,
                                        NULL,
                                        rangeOperatorDecl->type),
                       NULL,
                       rangeOperatorDecl->type),
        NULL,
        flgNone,
        NULL,
        rangeOperator->func.retType);

    AstNode *itVar = makeVarDecl(ctx->pool,
                                 &range->loc,
                                 flgNone,
                                 itVarName,
                                 NULL,
                                 rangeCall,
                                 NULL,
                                 rangeOperator->func.retType);
    astModifierAdd(&ctx->blockModifier, itVar);

    const Type *truthyOp = findStructMemberType(rangeOperator->func.retType, S_Truthy);
    AstNode *truthyDecl = truthyOp->func.decl;
    AstNode *callOpDecl = callOp->func.decl;

    AstNode *condition = makeCallExpr(
        ctx->pool,
        &range->loc,
        makeMemberExpr(ctx->pool,
                       &range->loc,
                       flgNone,
                       makeResolvedPath(ctx->pool,
                                        &range->loc,
                                        itVarName,
                                        flgNone,
                                        itVar,
                                        NULL,
                                        itVar->type),
                       makeResolvedPath(ctx->pool,
                                        &range->loc,
                                        S_Truthy,
                                        truthyDecl->flags,
                                        truthyDecl,
                                        NULL,
                                        truthyDecl->type),
                       NULL,
                       truthyDecl->type),
        NULL,
        flgNone,
        NULL,
        getPrimitiveType(ctx->types, prtBool));

    AstNode *callStep = makeCallExpr(
        ctx->pool,
        &range->loc,
        makeMemberExpr(ctx->pool,
                       &range->loc,
                       flgNone,
                       makeResolvedPath(ctx->pool,
                                        &range->loc,
                                        itVarName,
                                        flgNone,
                                        itVar,
                                        NULL,
                                        itVar->type),
                       makeResolvedPath(ctx->pool,
                                        &range->loc,
                                        S_CallOverload,
                                        callOpDecl->flags,
                                        callOpDecl,
                                        NULL,
                                        callOpDecl->type),
                       NULL,
                       callOpDecl->type),
        NULL,
        flgNone,
        NULL,
        callOp->func.retType);

    cstring itemVarName = makeAnonymousVariable(ctx->strings, "__item");
    AstNode *itemVar = makeVarDecl(ctx->pool,
                                   &range->loc,
                                   flgNone,
                                   itemVarName,
                                   NULL,
                                   callStep,
                                   NULL,
                                   callOp->func.retType);

    AstNodeList bodyStmts = {};
    insertAstNode(&bodyStmts, itemVar);

    const Type *yieldType = callOp->func.retType;
    bool isOpt = isOptionalType(yieldType);
    const Type *itemPayloadType = isOpt ? getOptionalTargetType(yieldType) : yieldType;

    if (vars->next == NULL) {
        AstNode *valExpr = NULL;
        if (isOpt) {
            valExpr = makeUnaryExpr(
                ctx->pool,
                &range->loc,
                flgNone,
                true,
                opDeref,
                makeResolvedPath(ctx->pool,
                                 &range->loc,
                                 itemVarName,
                                 flgNone,
                                 itemVar,
                                 NULL,
                                 itemVar->type),
                NULL,
                itemPayloadType);
        }
        else {
            valExpr = makeResolvedPath(ctx->pool,
                                       &range->loc,
                                       itemVarName,
                                       flgNone,
                                       itemVar,
                                       NULL,
                                       itemVar->type);
        }
        vars->varDecl.init = valExpr;
        vars->type = itemPayloadType;
        insertAstNode(&bodyStmts, vars);
    }
    else {
        AstNode *var = vars;
        for (u64 i = 0; var; i++) {
            AstNode *nextVar = var->next;
            var->next = NULL;
            if (isIgnoreVar(var->varDecl.names->ident.value)) {
                var = nextVar;
                continue;
            }

            cstring name = var->varDecl.name ?: (var->varDecl.names ? var->varDecl.names->ident.value : var->_namedNode.name);
            clearAstBody(var);
            var->tag = astAliasExpr;
            var->aliasExpr.name = name;
            AstNode *itemRef = makeResolvedPath(ctx->pool,
                                                &range->loc,
                                                itemVarName,
                                                flgNone,
                                                itemVar,
                                                NULL,
                                                itemVar->type);
            AstNode *idxLiteral = makeIntegerLiteral(ctx->pool,
                                                     &range->loc,
                                                     i,
                                                     NULL,
                                                     getPrimitiveType(ctx->types, prtU64));
            AstNode *memberAccess = makeMemberExpr(
                ctx->pool,
                &range->loc,
                flgNone,
                itemRef,
                idxLiteral,
                NULL,
                NULL);
            memberAccess->memberExpr.isRedirect = isOpt;
            checkType(visitor, memberAccess);
            var->aliasExpr.expr = memberAccess;
            var->type = memberAccess->type;
            insertAstNode(&bodyStmts, var);
            var = nextVar;
        }
    }

    if (body->blockStmt.stmts) {
        insertAstNode(&bodyStmts, body->blockStmt.stmts);
    }
    body->blockStmt.stmts = bodyStmts.first;

    clearAstBody(node);
    node->tag = astWhileStmt;
    node->whileStmt.cond = condition;
    node->whileStmt.body = body;
    node->whileStmt.update = NULL;
    body->parentScope = node;
    checkType(visitor, node);
}

void checkRangeExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *start = checkType(visitor, node->rangeExpr.start);
    if (typeIs(start, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!isNumericType(start)) {
        logError(ctx->L,
                 &node->rangeExpr.start->loc,
                 "`range` start type '{t}' is not supported, expecting a "
                 "numeric type",
                 (FormatArg[]){{.t = start}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *end = checkType(visitor, node->rangeExpr.end);
    if (typeIs(end, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // Range takes the common (larger) type, e.g. `0..len` is u64 not i8
    const Type *type = isNumericType(end)
                           ? unwrapType(promoteType(ctx->types, start, end), NULL)
                           : NULL;
    if (type == NULL) {
        logError(ctx->L,
                 &node->rangeExpr.end->loc,
                 "`range` inconsistent types, expect type `{t}`, but got `{t}`",
                 (FormatArg[]){{.t = start}, {.t = end}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (node->rangeExpr.step) {
        const Type *step = checkType(visitor, node->rangeExpr.step);
        if (typeIs(step, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (!isNumericType(step)) {
            logError(ctx->L,
                     &node->rangeExpr.step->loc,
                     "`range` step type '{t}' is not supported, expecting a "
                     "numeric type",
                     (FormatArg[]){{.t = step}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }
    node->type = type;
}

void checkForStmt(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *variable = node->forStmt.var;
    u64 numVariables = countAstNodes(variable);

    const Type *range = checkType(visitor, node->forStmt.range);
    if (typeIs(range, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *range_ = stripAll(range);
    if (nodeIs(node->forStmt.range, RangeExpr)) {
        if (numVariables != 1) {
            logError(ctx->L,
                     &node->forStmt.var->loc,
                     "unexpected number of for statement variables, expecting "
                     "1, got {u64}",
                     (FormatArg[]){{.u64 = numVariables}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        variable->type = range_;
    }
    else if (typeIs(range_, Array)) {
        if (numVariables > 2) {
            logError(
                ctx->L,
                &node->forStmt.var->loc,
                "unexpected number of `for` statement variables, expecting "
                "at most 2, got {u64}",
                (FormatArg[]){{.u64 = numVariables}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        variable->type = range_->array.elementType;
        if (variable->next)
            variable->next->type = getPrimitiveType(ctx->types, prtI64);
        transformForSequence(visitor, node, numVariables);
        return;
    }
    else if (typeIs(range_, String)) {
        if (numVariables > 2) {
            logError(
                ctx->L,
                &node->forStmt.var->loc,
                "unexpected number of `for` statement variables, expecting "
                "at most 2, got {u64}",
                (FormatArg[]){{.u64 = numVariables}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        variable->type = getPrimitiveType(ctx->types, prtCChar);
        if (variable->next)
            variable->next->type = getPrimitiveType(ctx->types, prtI64);
        transformForSequence(visitor, node, numVariables);
        return;
    }
    else if (isStructType(range_) || isClassType(range_)) {
        if (!checkForStmtCustomRange(ctx, node, numVariables, range_)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        transformForCustomRange(visitor, node, numVariables);
        return;
    }
    else {
        logError(ctx->L,
                 &node->forStmt.range->loc,
                 "unexpected range expression, range of type `{t}` cannot be "
                 "enumerated",
                 (FormatArg[]){{.t = range_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    bool currentReturnState = ctx->returnState;
    ctx->returnState = false;
    node->type = checkType(visitor, node->forStmt.body);
    ctx->returnState = currentReturnState;
}
