#include "internal.h"

#include "core/alloc.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"
#include "lang/middle/mangle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Module globals get the declaring module's prefix; references reuse it via
// decl->ir, so only the declaration decides the C name.
static cstring makeGlobalVarSymbolName(LowerContext *ctx,
                                       const AstNode *node,
                                       cstring name)
{
    if (name == NULL || hasFlag(node, Extern))
        return name;
    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    if (modName == NULL || modName == S_main)
        return name;
    return makeStringf(ctx->strings, "%s_%s", modName, name);
}

// `@register("x0")` pins a local to a machine register, see inline asm operands
static cstring getRegisterAttr(const AstNode *node)
{
    const AstNode *attr = findAttribute(node, S_register);
    const AstNode *arg = attr ? attr->attr.args : NULL;
    return arg && nodeIs(arg, StringLit) ? arg->stringLiteral.value : NULL;
}

void lowerVarDecl(AstVisitor *visitor, AstNode *node)
{
    if (hasFlag(node, Comptime))
        return;

    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *varAstType =
        (node->varDecl.names && node->varDecl.names->type)
            ? node->varDecl.names->type
            : (node->varDecl.type ? node->varDecl.type->type : node->type);
    varAstType = resolveUnThisUnwrapType(varAstType);
    const IrType *irType = lowerType(ctx, varAstType);
    cstring name = getDeclarationName(node);

    if (ctx->currentFunc == NULL) {
        IrNode *init = NULL;
        if (node->varDecl.init) {
            astVisit(visitor, node->varDecl.init);
            init = (IrNode *)node->varDecl.init->codegen;
        }
        IrNode *glob = makeIrGlobalVar(ctx->driver->pool,
                                       &node->loc,
                                       node->flags,
                                       makeGlobalVarSymbolName(ctx, node, name),
                                       irType,
                                       init);
        // @thread: one instance per thread, not shared by the whole program
        glob->globalVar.isThreadLocal = findAttribute(node, S_thread) != NULL;
        node->ir = glob;
        if (node->varDecl.names)
            node->varDecl.names->ir = glob;

        if (ctx->module->module.globals) {
            IrNode *last = ctx->module->module.globals;
            while (last->next)
                last = last->next;
            last->next = glob;
        }
        else {
            ctx->module->module.globals = glob;
        }
        return;
    }

    cstring displayName = getDeclarationName(node);
    cstring cName = makeUniqueVarName(ctx, displayName);
    Flags flags = node->flags;
    if (!isReferenceType(varAstType) && !isPointerType(varAstType) &&
        isDestructible(varAstType)) {
        flags |= flgReferenceMembers;
    }
    IrNode *varDecl = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    cName,
                                    displayName,
                                    irType,
                                    ctx->currentScopeId);
    varDecl->varDecl.reg = getRegisterAttr(node);
    node->ir = varDecl;
    if (node->varDecl.names)
        node->varDecl.names->ir = varDecl;
    emitStmt(ctx, varDecl);

    if (node->varDecl.init) {
        AstNode *initNode = node->varDecl.init;
        if (hasFlag(varAstType, Slice) || isSliceType(varAstType)) {
            if (initNode->type && isSliceType(initNode->type)) {
                astVisit(visitor, initNode);
                IrNode *val = (IrNode *)initNode->codegen;
                if (val)
                    emitStmt(ctx,
                             makeIrAssign(
                                 ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
            }
            else {
                lowerSliceIntoTarget(ctx,
                                     visitor,
                                     initNode,
                                     makeIrVarRef(ctx->pool, &node->loc, varDecl));
            }
        }
        else if (isOptionalType(varAstType)) {
            if (initNode->type && isOptionalType(initNode->type)) {
                astVisit(visitor, initNode);
                IrNode *val = (IrNode *)initNode->codegen;
                if (val)
                    emitStmt(ctx,
                             makeIrAssign(
                                 ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
            }
            else {
                lowerOptionalIntoTarget(
                    ctx,
                    visitor,
                    initNode,
                    makeIrVarRef(ctx->pool, &node->loc, varDecl));
            }
        }
        else if (typeIs(varAstType, Union)) {
            lowerTaggedUnionIntoTarget(
                ctx,
                visitor,
                initNode,
                makeIrVarRef(ctx->pool, &node->loc, varDecl),
                varAstType);
        }
        else if (nodeIs(initNode, BackendCall) &&
                 initNode->backendCallExpr.func == bfiCopy &&
                 initNode->backendCallExpr.args &&
                 initNode->backendCallExpr.args->next == NULL) {
            IrNode *src =
                lowerFlattenedOperand(visitor, initNode->backendCallExpr.args);
            emitStmt(ctx,
                     markUserWritten(
                         initNode,
                         makeIrCopy(ctx->pool,
                                    &node->loc,
                                    src,
                                    makeIrVarRef(ctx->pool, &node->loc, varDecl))));
        }
        else {
            astVisit(visitor, initNode);
            IrNode *val = (IrNode *)initNode->codegen;
            if (val)
                emitStmt(
                    ctx,
                    makeIrAssign(ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
        }
    }
    else if (isOptionalType(varAstType)) {
        lowerOptionalIntoTarget(
            ctx, visitor, NULL, makeIrVarRef(ctx->pool, &node->loc, varDecl));
    }
}

void lowerExprStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->exprStmt.expr);
    IrNode *expr = (IrNode *)node->exprStmt.expr->codegen;
    node->codegen = expr;
    if (expr) {
        const Type *astType =
            node->exprStmt.expr->type
                ? resolveUnThisUnwrapType(node->exprStmt.expr->type)
                : NULL;
        // the payload already lives in the result temporary, drop it in place
        if (isOwnedResultPayload(expr)) {
            emitStmt(ctx, makeIrDrop(ctx->pool, &node->loc, expr));
            return;
        }
        // the value of a discarded catch lives in its temporary, drop it in place
        if (nodeIs(node->exprStmt.expr, CatchExpr) && expr->tag == irVarRef &&
            isIrTypeDestructible(expr->type)) {
            emitStmt(ctx, makeIrDrop(ctx->pool, &node->loc, expr));
            return;
        }
        if (expr->tag == irVarRef || isIrConst(expr) ||
            (isIrPureValueExpr(expr) && expr->tag != irCall)) {
            return;
        }
        if (expr->tag == irCall && astType && !isReferenceType(astType) &&
            !isPointerType(astType) && isDestructible(astType)) {
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                            &node->loc,
                                            flgEphemeral | flgReferenceMembers,
                                            tempName,
                                            tempName,
                                            expr->type,
                                            ctx->currentScopeId);
            emitStmt(ctx, tempVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, tempVar),
                                  expr));
        }
        else {
            emitStmt(ctx, expr);
        }
    }
}

void lowerStmtExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *stmt = node->stmtExpr.stmt;
    if (stmt == NULL)
        return;

    if (nodeIs(stmt, BlockStmt)) {
        IrNode *lastVal = NULL;
        for (AstNode *s = stmt->blockStmt.stmts; s; s = s->next) {
            astVisit(visitor, s);
            if (s->codegen) {
                lastVal = (IrNode *)s->codegen;
            }
            else if (nodeIs(s, ExprStmt) && s->exprStmt.expr && s->exprStmt.expr->codegen) {
                lastVal = (IrNode *)s->exprStmt.expr->codegen;
            }
        }
        node->codegen = lastVal;
    }
    else {
        astVisit(visitor, stmt);
        node->codegen = stmt->codegen;
    }
}

void lowerBlockStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 scopeId = ctx->nextScopeId++;
    u32 prevScopeId = ctx->currentScopeId;
    csAssert0(scopeId == ctx->scopes.size);
    pushOnDynArray(&ctx->scopes,
                   &(ScopeInfo){.parent = prevScopeId, .hasDrops = false});
    ctx->currentScopeId = scopeId;
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, scopeId));

    for (AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        astVisit(visitor, stmt);
    }

    if (!isBlockTerminated(ctx->currentBlock))
        emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, scopeId));
    ctx->currentScopeId = prevScopeId;
}

void lowerIfStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->ifStmt.cond);
    u32 ifId = ctx->nextIfId++;
    IrNode *thenBB =
        createBlock(ctx, makeStringf(ctx->strings, "IF%u_then", ifId));
    IrNode *elseBB =
        node->ifStmt.otherwise
            ? createBlock(ctx, makeStringf(ctx->strings, "IF%u_else", ifId))
            : NULL;
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "IF%u_end", ifId));

    emitStmt(
        ctx,
        makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB ?: mergeBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ifStmt.body);
    emitBranchIfNotTerminated(ctx, mergeBB);

    if (elseBB) {
        setBlock(ctx, elseBB);
        astVisit(visitor, node->ifStmt.otherwise);
        emitBranchIfNotTerminated(ctx, mergeBB);
    }

    setBlock(ctx, mergeBB);
}

void lowerReturnStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->returnStmt.expr && ctx->funcRetVar) {
        AstNode *retExpr = node->returnStmt.expr;
        IrNode *retVarRef =
            makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        const Type *retAstType = ctx->funcRetAstType;
        const Type *exprAstType = retExpr->type;

        if (isResultType(retAstType)) {
            const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
            IrNode *tagSlot = makeIrMember(ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
            const IrType *retIrType = ctx->funcRetVar->type;
            const IrType *valFieldType = retIrType->strct.fields[1].type;
            IrNode *valMember = makeIrMember(ctx->pool, &node->loc, retVarRef, 1, S_val, valFieldType);

            if (node->returnStmt.isRaise) {
                IrNode *errVal = lowerFlattenedOperand(visitor, retExpr);
                emitExceptionTracePush(ctx, &node->loc, errVal);
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));
                if (valFieldType->tag == irTypeUntaggedUnion) {
                    const Type *baseExType = findBuiltinType(S_Exception);
                    const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
                    IrNode *errSlot = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
                    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, errSlot, errVal));
                }
                else {
                    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valMember, errVal));
                }
            }
            else {
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type)));
                if (retExpr && valFieldType->tag == irTypeUntaggedUnion) {
                    const IrType *elemType = valFieldType->strct.fields[0].type;
                    IrNode *valSlot = makeIrMember(ctx->pool, &node->loc, valMember, 0, NULL, elemType);
                    const Type *resTargetType = getResultTargetType(retAstType);
                    if (isSliceType(resTargetType) && (exprAstType == NULL || !isSliceType(exprAstType))) {
                        lowerSliceIntoTarget(ctx, visitor, retExpr, valSlot);
                    }
                    else if (isOptionalType(resTargetType) && !isOptionalType(exprAstType)) {
                        lowerOptionalIntoTarget(ctx, visitor, retExpr, valSlot);
                    }
                    else {
                        IrNode *val = lowerFlattenedOperand(visitor, retExpr);
                        if (val)
                            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valSlot, val));
                    }
                }
            }
        }
        else if (isSliceType(retAstType) && (exprAstType == NULL ||!isSliceType(exprAstType))) {
            lowerSliceIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else if (isOptionalType(retAstType) && !isOptionalType(exprAstType)) {
            lowerOptionalIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else if (retAstType && typeIs(retAstType, Union) &&
                 (nodeIs(retExpr, UnionValueExpr) ||
                  (exprAstType && !typeIs(exprAstType, Union)))) {
            lowerTaggedUnionIntoTarget(
                ctx, visitor, retExpr, retVarRef, retAstType);
        }
        else if (isBackendCopyExpr(retExpr)) {
            IrNode *src =
                lowerFlattenedOperand(visitor, retExpr->backendCallExpr.args);
            emitStmt(ctx,
                     markUserWritten(
                         retExpr,
                         makeIrCopy(ctx->pool, &node->loc, src, retVarRef)));
        }
        else {
            astVisit(visitor, retExpr);
            IrNode *val = (IrNode *)retExpr->codegen;
            if (val)
                emitStmt(ctx,
                         makeIrAssign(ctx->pool, &node->loc, retVarRef, val));
        }
    }
    else if (ctx->funcRetVar && isResultType(ctx->funcRetAstType)) {
        IrNode *retVarRef = makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *tagSlot = makeIrMember(ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
        if (node->returnStmt.isRaise) {
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));
            if (ctx->currentCatchErr) {
                emitExceptionTracePush(ctx, &node->loc, ctx->currentCatchErr);
                const IrType *retIrType = ctx->funcRetVar->type;
                const IrType *valFieldType = retIrType->strct.fields[1].type;
                IrNode *valMember = makeIrMember(ctx->pool, &node->loc, retVarRef, 1, S_val, valFieldType);
                IrNode *assignStmt = NULL;
                if (valFieldType->tag == irTypeUntaggedUnion) {
                    const Type *baseExType = findBuiltinType(S_Exception);
                    const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
                    IrNode *errSlot = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
                    assignStmt = makeIrAssign(ctx->pool, &node->loc, errSlot, cloneIrNode(ctx->pool, ctx->currentCatchErr));
                }
                else {
                    assignStmt = makeIrAssign(ctx->pool, &node->loc, valMember, cloneIrNode(ctx->pool, ctx->currentCatchErr));
                }
                assignStmt->flags |= flgUnmanaged;
                emitStmt(ctx, assignStmt);
                if (ctx->currentCatchRes) {
                    IrNode *srcTagSlot = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, ctx->currentCatchRes), 0, S_tag, u32Type);
                    IrNode *defuseStmt = makeIrAssign(ctx->pool, &node->loc, srcTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
                    defuseStmt->flags |= flgUnmanaged;
                    emitStmt(ctx, defuseStmt);
                }
            }
        }
        else {
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type)));
        }
    }
    // `raise` moves the caught exception, any other return leaves it owned
    if (!node->returnStmt.isRaise)
        emitReleaseCaughtUntil(ctx, &node->loc, NULL);
    IrNode *targetBB =
        getOrCreateScopeReturnBlock(ctx, ctx->currentScopeId, &node->loc);
    emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, targetBB));
}
