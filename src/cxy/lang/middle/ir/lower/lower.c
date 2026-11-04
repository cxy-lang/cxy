#include "lower.h"
#include "internal.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/mangle.h"
#include "lang/middle/memory/memory.h"
#include "lang/middle/builtins.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    astVisit(visitor, node->ternaryExpr.cond);
    IrNode *cond = (IrNode *)node->ternaryExpr.cond->codegen;

    cstring tempName =
        makeStringf(ctx->strings, "__ternary_%u", ctx->tempCounter++);
    Flags flags = flgTemporary;
    if (isIrTypeDestructible(irType))
        flags |= flgReferenceMembers;
    IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    tempName,
                                    tempName,
                                    irType,
                                    ctx->currentScopeId);
    emitStmt(ctx, tempVar);

    u32 ternaryId = ctx->nextTernaryId++;
    IrNode *thenBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_then", ternaryId));
    IrNode *elseBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_else", ternaryId));
    IrNode *mergeBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_merge", ternaryId));

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ternaryExpr.body);
    IrNode *thenVal = (IrNode *)node->ternaryExpr.body->codegen;
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          thenVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    astVisit(visitor, node->ternaryExpr.otherwise);
    IrNode *elseVal = (IrNode *)node->ternaryExpr.otherwise->codegen;
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          elseVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 whileId = ctx->nextWhileId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_cond", whileId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_body", whileId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_exit", whileId));

    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, condBB);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->whileStmt.cond);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = condBB,
                    .exitBlock = exitBB,
                    .scopeId = ctx->currentScopeId,
                    .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->whileStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

static void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);

    if (nodeIs(node->forStmt.range, RangeExpr)) {
        AstNode *startNode = node->forStmt.range->rangeExpr.start;
        AstNode *endNode = node->forStmt.range->rangeExpr.end;
        AstNode *stepNode = node->forStmt.range->rangeExpr.step;

        IrNode *startVal = lowerFlattenedOperand(visitor, startNode);
        IrNode *endVal = lowerFlattenedOperand(visitor, endNode);

        // sema typed the variable with the common type of the range, the
        // start value alone may be a small literal (`0..size`)
        AstNode *loopVarNode = node->forStmt.var;
        const IrType *varType =
            loopVarNode->type ? lowerType(ctx, loopVarNode->type)
                              : (startVal ? startVal->type : u64Type);
        cstring displayName = getDeclarationName(loopVarNode);
        cstring cName = makeUniqueVarName(ctx, displayName);
        IrNode *varDecl = makeIrVarDecl(ctx->pool,
                                        &loopVarNode->loc,
                                        loopVarNode->flags,
                                        cName,
                                        displayName,
                                        varType,
                                        ctx->currentScopeId);
        loopVarNode->ir = varDecl;
        emitStmt(ctx, varDecl);
        emitStmt(
            ctx,
            makeIrAssign(ctx->pool,
                         &startNode->loc,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         startVal));

        u32 forId = ctx->nextForId++;
        IrNode *condBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_cond", forId));
        IrNode *bodyBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_body", forId));
        IrNode *updateBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_update", forId));
        IrNode *exitBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_exit", forId));

        emitBranchIfNotTerminated(ctx, condBB);
        setBlock(ctx, condBB);

        IrNode *cond =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opLt,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         endVal,
                         boolType);
        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

        setBlock(ctx, bodyBB);
        LoopCtx loop = {.condBlock = updateBB,
                        .exitBlock = exitBB,
                        .scopeId = ctx->currentScopeId,
                        .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
        ctx->loopCtx = &loop;
        astVisit(visitor, node->forStmt.body);
        ctx->loopCtx = loop.prev;
        emitBranchIfNotTerminated(ctx, updateBB);

        setBlock(ctx, updateBB);
        IrNode *stepVal =
            stepNode ? lowerFlattenedOperand(visitor, stepNode)
                     : makeIrIntConst(ctx->pool, &node->loc, 1, false, varType);
        IrNode *nextVal =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opAdd,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         stepVal,
                         varType);
        emitStmt(
            ctx,
            makeIrAssign(ctx->pool,
                         &node->loc,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         nextVal));
        emitBranchIfNotTerminated(ctx, condBB);

        setBlock(ctx, exitBB);
        return;
    }

    u32 forId = ctx->nextForId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_cond", forId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_body", forId));
    IrNode *updateBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_update", forId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_exit", forId));

    emitBranchIfNotTerminated(ctx, condBB);
    setBlock(ctx, condBB);

    IrNode *cond = lowerConditionOperand(ctx, visitor, node->forStmt.range);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = updateBB,
                    .exitBlock = exitBB,
                    .scopeId = ctx->currentScopeId,
                    .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->forStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, updateBB);

    setBlock(ctx, updateBB);
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

// Drops a caught exception and defuses its result so it is not dropped twice
static void emitReleaseCaught(LowerContext *ctx,
                              const FileLoc *loc,
                              const CatchCtx *c)
{
    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    emitStmt(ctx, makeIrDrop(ctx->pool, loc, cloneIrNode(ctx->pool, c->err)));
    IrNode *tagSlot = makeIrMember(
        ctx->pool, loc, cloneIrNode(ctx->pool, c->res), 0, S_tag, u32Type);
    IrNode *defuse = makeIrAssign(
        ctx->pool,
        loc,
        tagSlot,
        makeIrIntConst(ctx->pool, loc, 0, false, u32Type));
    defuse->flags |= flgUnmanaged;
    emitStmt(ctx, defuse);
}

// Releases the catch bodies entered since `outer`, innermost first
void emitReleaseCaughtUntil(LowerContext *ctx,
                            const FileLoc *loc,
                            const CatchCtx *outer)
{
    for (const CatchCtx *c = ctx->catchCtx; c != outer; c = c->prev)
        emitReleaseCaught(ctx, loc, c);
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->exitBlock) {
        emitReleaseCaughtUntil(ctx, &node->loc, ctx->loopCtx->catchCtx);
        for (u32 s = ctx->currentScopeId; s > ctx->loopCtx->scopeId; s--) {
            emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, s));
        }
        emitStmt(ctx,
                 makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->exitBlock));
    }
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->condBlock) {
        emitReleaseCaughtUntil(ctx, &node->loc, ctx->loopCtx->catchCtx);
        for (u32 s = ctx->currentScopeId; s > ctx->loopCtx->scopeId; s--) {
            emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, s));
        }
        emitStmt(ctx,
                 makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->condBlock));
    }
}

static u32 nearestDirtyScope(LowerContext *ctx, u32 scopeId)
{
    while (scopeId != 0 && scopeId < ctx->scopes.size &&
           !dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).hasDrops)
        scopeId = dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).parent;
    return scopeId;
}

IrNode *getOrCreateScopeReturnBlock(LowerContext *ctx,
                                    u32 scopeId,
                                    const FileLoc *loc)
{
    scopeId = nearestDirtyScope(ctx, scopeId);
    if (scopeId == 0)
        return ctx->funcExitBlock;

    cstring name = makeStringf(ctx->strings, "_L_UNWIND%u_return", scopeId);
    if (ctx->currentFunc) {
        for (IrNode *b = ctx->currentFunc->function.firstBlock; b;
             b = b->next) {
            if (b->tag == irBasicBlock && b->basicBlock.name == name)
                return b;
        }
    }

    IrNode *savedBlock = ctx->currentBlock;
    IrNode *unwindBB =
        createBlock(ctx, makeStringf(ctx->strings, "UNWIND%u_return", scopeId));
    setBlock(ctx, unwindBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, loc, scopeId));
    IrNode *parentUnwindBB = getOrCreateScopeReturnBlock(
        ctx, dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).parent, loc);
    emitStmt(ctx, makeIrBranch(ctx->pool, loc, parentUnwindBB));
    ctx->currentBlock = savedBlock;
    return unwindBB;
}

static void visitYieldStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->yieldStmt.expr && ctx->catchTargetVar) {
        IrNode *val = lowerFlattenedOperand(visitor, node->yieldStmt.expr);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, ctx->catchTargetVar),
                              val));
    }
    if (ctx->currentCatchErr) {
        emitStmt(ctx,
                 makeIrDrop(ctx->pool,
                            &node->loc,
                            cloneIrNode(ctx->pool, ctx->currentCatchErr)));
    }
    if (ctx->currentCatchRes) {
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *resTagSlot = makeIrMember(
            ctx->pool,
            &node->loc,
            cloneIrNode(ctx->pool, ctx->currentCatchRes),
            0,
            S_tag,
            u32Type);
        IrNode *defuseStmt = makeIrAssign(
            ctx->pool,
            &node->loc,
            resTagSlot,
            makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);
    }
    if (ctx->catchMergeBB) {
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, ctx->catchMergeBB));
    }
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerFlattenedOperand(visitor, node->switchStmt.cond);
    u32 switchId = ctx->nextSwitchId++;
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "SWITCH%u_exit", switchId));

    u64 totalCases = 0;
    for (const AstNode *c = node->switchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match)
            totalCases += countAstNodes(c->caseStmt.match);
        else
            totalCases++;
    }

    IrCase *cases =
        allocFromMemPool(ctx->pool, sizeof(IrCase) * (totalCases ?: 1));
    u64 caseIdx = 0;
    IrNode *defaultBB = NULL;

    u64 caseStmtCount = countAstNodes(node->switchStmt.cases);
    IrNode **caseBBs =
        allocFromMemPool(ctx->pool, sizeof(IrNode *) * (caseStmtCount ?: 1));

    // Pass 1: Create a single basic block per shared body
    u32 cIdx = 0;
    for (AstNode *c = node->switchStmt.cases; c; c = c->next, cIdx++) {
        if (c->caseStmt.body != NULL || c->next == NULL) {
            caseBBs[cIdx] = createBlock(
                ctx, makeStringf(ctx->strings, "SWITCH%u_case_%u", switchId, cIdx));
        }
    }
    // Propagate the target block backwards to preceding cases sharing the same body
    IrNode *currentGroupBB = NULL;
    for (i64 i = (i64)caseStmtCount - 1; i >= 0; i--) {
        if (caseBBs[i] != NULL)
            currentGroupBB = caseBBs[i];
        else
            caseBBs[i] = currentGroupBB;
    }

    // Pass 2: Lower cases and emit bodies
    cIdx = 0;
    for (AstNode *c = node->switchStmt.cases; c; c = c->next, cIdx++) {
        IrNode *caseBB = caseBBs[cIdx];
        if (c->caseStmt.match == NULL) {
            defaultBB = caseBB;
        }
        else {
            for (const AstNode *m = c->caseStmt.match; m; m = m->next) {
                if (nodeIs(m, RangeExpr)) {
                    IrNode *startNode = lowerFlattenedOperand(visitor, m->rangeExpr.start);
                    IrNode *endNode = lowerFlattenedOperand(visitor, m->rangeExpr.end);
                    cases[caseIdx++] = (IrCase){
                        .valNode = startNode, .endNode = endNode, .target = caseBB};
                }
                else {
                    IrNode *valNode = lowerFlattenedOperand(visitor, (AstNode *)m);
                    cases[caseIdx++] = (IrCase){
                        .valNode = valNode, .endNode = NULL, .target = caseBB};
                }
            }
        }

        if (c->caseStmt.body) {
            IrNode *savedBlock = ctx->currentBlock;
            setBlock(ctx, caseBB);
            LoopCtx loop = {
                .condBlock = NULL, .exitBlock = exitBB, .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
            ctx->loopCtx = &loop;
            astVisit(visitor, c->caseStmt.body);
            ctx->loopCtx = loop.prev;
            emitBranchIfNotTerminated(ctx, exitBB);
            ctx->currentBlock = savedBlock;
        }
    }

    IrNode *switchNode = makeIrSwitch(
        ctx->pool, &node->loc, cond, cases, caseIdx, defaultBB ?: exitBB);
    emitStmt(ctx, switchNode);

    setBlock(ctx, exitBB);
}

static void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *unionVal = lowerFlattenedOperand(visitor, node->matchStmt.expr);
    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->matchStmt.expr->loc, unionVal, 0, "tag", u32Type);
    u32 matchId = ctx->nextMatchId++;
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "MATCH%u_exit", matchId));

    u64 casesCount = 0;
    AstNode *defaultCase = node->matchStmt.defaultCase;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match != NULL)
            casesCount++;
        else if (defaultCase == NULL)
            defaultCase = c;
    }

    IrCase *cases =
        allocFromMemPool(ctx->pool, sizeof(IrCase) * (casesCount ?: 1));
    IrNode **caseBBs =
        allocFromMemPool(ctx->pool, sizeof(IrNode *) * (casesCount ?: 1));

    const Type *sumType =
        stripPointerOrReferenceOnce(node->matchStmt.expr->type, NULL);
    u64 ci = 0;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match == NULL)
            continue;
        IrNode *bb = createBlock(
            ctx,
            makeStringf(
                ctx->strings, "MATCH%u_case_%u", matchId, (unsigned)ci));
        u32 caseIdx = c->caseStmt.idx;
        if (caseIdx == 0 && c->caseStmt.match && sumType &&
            isUnionType(sumType)) {
            u32 found = findUnionTypeIndex(sumType, c->caseStmt.match->type);
            if (found != UINT32_MAX)
                caseIdx = found;
        }
        cases[ci].valNode =
            makeIrIntConst(ctx->pool, &c->loc, caseIdx, false, u32Type);
        cases[ci].target = bb;
        caseBBs[ci] = bb;
        ci++;
    }

    IrNode *defaultBB =
        defaultCase
            ? createBlock(ctx,
                          makeStringf(ctx->strings, "MATCH%u_default", matchId))
            : exitBB;
    emitStmt(ctx,
             makeIrSwitch(
                 ctx->pool, &node->loc, tagVal, cases, casesCount, defaultBB));

    ci = 0;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match == NULL)
            continue;
        u32 caseIdx = cases[ci].valNode->intConst.value;
        setBlock(ctx, caseBBs[ci++]);
        if (c->caseStmt.alias) {
            const IrType *aliasIrType = lowerType(ctx, c->caseStmt.match->type);
            const IrType *unionStructType = unionVal->type;
            if (unionStructType && unionStructType->tag == irTypePointer)
                unionStructType = unionStructType->pointer.pointee;
            const IrType *payloadUnionType =
                (unionStructType && unionStructType->tag == irTypeStruct &&
                 unionStructType->strct.count > 1)
                    ? unionStructType->strct.fields[1].type
                    : NULL;
            IrNode *valMember = makeIrMember(
                ctx->pool, &c->loc, unionVal, 1, "val", payloadUnionType);
            cstring fieldName = makeStringf(ctx->strings, "_%u", caseIdx);
            IrNode *payloadMember = makeIrMember(
                ctx->pool, &c->loc, valMember, caseIdx, fieldName, aliasIrType);
            cstring aliasName = nodeIs(c->caseStmt.alias, AliasExpr)
                                    ? c->caseStmt.alias->aliasExpr.name
                                    : getDeclarationName(c->caseStmt.alias);
            c->caseStmt.alias->ir =
                makeIrAlias(ctx->pool, &c->loc, aliasName, payloadMember);
        }
        if (c->caseStmt.body) {
            astVisit(visitor, c->caseStmt.body);
        }
        emitBranchIfNotTerminated(ctx, exitBB);
    }

    if (defaultCase) {
        setBlock(ctx, defaultBB);
        if (defaultCase->caseStmt.body) {
            astVisit(visitor, defaultCase->caseStmt.body);
        }
        emitBranchIfNotTerminated(ctx, exitBB);
    }

    setBlock(ctx, exitBB);
}

static u8 lowerExternFunctionAttrs(const AstNode *node)
{
    u8 attrs = 0;
    if (findAttribute(node, S_naked))
        attrs |= irFuncAttrNaked;
    if (findAttribute(node, S_noreturn))
        attrs |= irFuncAttrNoReturn;
    if (findAttribute(node, S_returns_twice))
        attrs |= irFuncAttrReturnsTwice;
    return attrs;
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Extern)) {
        bool isHint = findAttribute(node, S_hint) != NULL;

        const IrType *funcType = lowerType(ctx, node->type);
        u64 paramsCount = 0;
        IrNode *paramsFirst = NULL, *paramsLast = NULL;
        u32 pidx = 0;
        for (AstNode *p = node->funcDecl.signature->params; p;
             p = p->next, pidx++) {
            // C varargs marker, emitted as `...` from the function flag
            if (hasFlag(p, Variadic))
                continue;
            paramsCount++;
            const IrType *pType = lowerType(ctx, p->type);
            cstring pname =
                p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
            IrNode *param = makeIrFuncParam(
                ctx->pool, &p->loc, p->flags, pname, pidx, pType);
            p->ir = param;
            if (paramsLast) {
                paramsLast->next = param;
                paramsLast = param;
            }
            else {
                paramsFirst = paramsLast = param;
            }
        }

        cstring funcName = getDeclarationName(node);
        cstring cName = getFunctionSymbolName(ctx, node);
        cstring displayName = funcName;
        IrNode *func = makeIrFunction(ctx->pool,
                                      &node->loc,
                                      node->flags | flgExtern,
                                      cName,
                                      displayName,
                                      funcType,
                                      paramsFirst,
                                      paramsCount);
        func->ast = node;
        func->function.isHint = isHint;
        func->function.attrs = lowerExternFunctionAttrs(node);
        node->ir = func;

        if (ctx->module->module.functions) {
            IrNode *last = ctx->module->module.functions;
            while (last->next)
                last = last->next;
            last->next = func;
        }
        else {
            ctx->module->module.functions = func;
        }
        return;
    }

    const IrType *funcType = lowerType(ctx, node->type);
    u64 paramsCount = countAstNodes(node->funcDecl.signature->params);
    IrNode *paramsFirst = NULL, *paramsLast = NULL;
    u32 pidx = 0;
    for (AstNode *p = node->funcDecl.signature->params; p;
         p = p->next, pidx++) {
        const IrType *pType = lowerType(ctx, p->type);
        cstring pname =
            p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
        Flags pFlags = p->flags;
        const Type *pAstType =
            p->type ? resolveUnThisUnwrapType(p->type) : NULL;
        if (pAstType && !isReferenceType(pAstType) &&
            !isPointerType(pAstType) && !hasFlag(p, Reference) &&
            isDestructible(pAstType)) {
            pFlags |= flgReferenceMembers;
        }
        IrNode *param =
            makeIrFuncParam(ctx->pool, &p->loc, pFlags, pname, pidx, pType);
        p->ir = param;
        if (paramsLast) {
            paramsLast->next = param;
            paramsLast = param;
        }
        else {
            paramsFirst = paramsLast = param;
        }
    }

    cstring funcName = getDeclarationName(node);
    AstNode *parent = node->parentScope;
    cstring modName = ctx->module->module.name;
    cstring cName = getFunctionSymbolName(ctx, node);
    cstring displayName = funcName;

    if (parent && isClassOrStructAstNode(parent)) {
        cstring typeName = getDeclarationName(parent);
        displayName =
            makeStringf(ctx->strings, "%s.%s.%s", modName, typeName, funcName);
    }
    else if (hasFlag(node, Main) || funcName == S_main) {
        displayName = S_main;
    }
    else if (!hasFlag(node, Extern) && !hasFlag(node, Builtin) && modName &&
             modName != S_main) {
        displayName = makeStringf(ctx->strings, "%s.%s", modName, funcName);
    }

    IrNode *func = makeIrFunction(ctx->pool,
                                  &node->loc,
                                  node->flags,
                                  cName,
                                  displayName,
                                  funcType,
                                  paramsFirst,
                                  paramsCount);
    func->ast = node;
    node->ir = func;
    if (node->glue)
        irSetInfo(func, irInfoGlue);
    if (node->funcDecl.isInitCover || findAttribute(node, S_memtrace))
        func->function.attrs |= irFuncAttrCallSite;

    ctx->currentFunc = func;
    ctx->nextBlockId = 0;
    ctx->nextForId = 0;
    ctx->nextWhileId = 0;
    ctx->nextIfId = 0;
    ctx->nextSwitchId = 0;
    ctx->nextMatchId = 0;
    ctx->nextOptId = 0;
    ctx->nextTernaryId = 0;
    ctx->nextLogicId = 0;
    ctx->currentScopeId = 0;
    ctx->nextScopeId = 1;
    clearDynArray(&ctx->scopes);
    pushOnDynArray(&ctx->scopes, &(ScopeInfo){.parent = 0, .hasDrops = false});
    ctx->varNames = newHashTable(sizeof(VarNameEntry), ctx->pool);
    // Params own their C names; locals that shadow a param get a suffix
    for (IrNode *p = func->function.params; p; p = p->next)
        makeUniqueVarName(ctx, p->funcParam.name);

    IrNode *entryBB = createBlock(ctx, "entry");
    IrNode *exitBB = makeIrBasicBlock(ctx->pool, NULL, "_L_exit");
    ctx->funcExitBlock = exitBB;

    setBlock(ctx, entryBB);
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, 0));

    bool hasRet = funcType->func.ret && funcType->func.ret->tag != irTypeVoid;
    if (hasRet) {
        ctx->funcRetVar = makeIrVarDecl(ctx->pool,
                                        &node->loc,
                                        flgReturned,
                                        "__ret",
                                        "__ret",
                                        funcType->func.ret,
                                        0);
        ctx->funcRetAstType = (node->type && typeIs(node->type, Func))
                                  ? node->type->func.retType
                                  : NULL;
        emitStmt(ctx, ctx->funcRetVar);
    }
    else {
        ctx->funcRetVar = NULL;
        ctx->funcRetAstType = NULL;
    }

    if (node->funcDecl.body) {
        if (nodeIs(node->funcDecl.body, BlockStmt)) {
            for (AstNode *stmt = node->funcDecl.body->blockStmt.stmts; stmt;
                 stmt = stmt->next) {
                astVisit(visitor, stmt);
            }
        }
        else {
            astVisit(visitor, node->funcDecl.body);
        }
    }

    emitBranchIfNotTerminated(ctx, exitBB);

    irAppendFunctionBlock(ctx->currentFunc, exitBB);
    setBlock(ctx, exitBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, 0));
    IrNode *retVal = ctx->funcRetVar
                         ? makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar)
                         : NULL;
    emitStmt(ctx, makeIrReturn(ctx->pool, &node->loc, retVal));

    if (ctx->module->module.functions) {
        IrNode *last = ctx->module->module.functions;
        while (last->next)
            last = last->next;
        last->next = func;
    }
    else {
        ctx->module->module.functions = func;
    }

    ctx->currentFunc = NULL;
    ctx->currentBlock = NULL;
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    if (node->externDecl.func && hasFlag(node->externDecl.func, Extern)) {
        astVisit(visitor, node->externDecl.func);
    }
}

static void visitClassOrStructDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    lowerType(ctx, node->type);
    AstNode *members = nodeIs(node, StructDecl) ? node->structDecl.members
                                                : node->classDecl.members;
    for (AstNode *m = members; m; m = m->next) {
        if (nodeIs(m, FuncDecl)) {
            astVisit(visitor, m);
        }
    }
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    cstring moduleName =
        node->program.module
            ? node->program.module->moduleDecl.name
            : (hasFlag(node, BuiltinsModule) ? S_builtins : S_main);
    ctx->module =
        makeIrModule(ctx->driver->pool, &node->loc, moduleName, ctx->types);

    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        astVisit(visitor, decl);
    }
}

static void finalizeLifecycleTypes(LowerContext *ctx)
{
    HashtableIt it = newHashTableIt(&ctx->types->types, sizeof(IrType *));
    while (hashTableItHasNext(&it)) {
        IrType *type = *(IrType **)hashTableItNext(&it);
        if (type && type->category == irTypeCatArray) {
            const AstNode *dAst = type->array.dctorAst;
            if (dAst != NULL) {
                csAssert(dAst->ir != NULL,
                         "array destructor '%s' was never lowered to IR",
                         getDeclarationName(dAst));
                type->array.dctor = dAst->ir;
            }
            else {
                type->array.dctor = NULL;
            }
        }
        else if (type && (type->category == irTypeCatStruct ||
                          type->category == irTypeCatTuple ||
                          type->category == irTypeCatUnion ||
                          type->category == irTypeCatClass)) {
            const AstNode *dAst = type->strct.dctorAst;
            const AstNode *fAst = type->strct.dctorFwdAst;
            const AstNode *cAst = type->strct.copyAst;

            if (dAst != NULL) {
                csAssert(dAst->ir != NULL,
                         "destructor '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(dAst),
                         type->name ?: "<anonymous>");
                type->strct.dctor = dAst->ir;
            }
            else {
                type->strct.dctor = NULL;
            }

            if (fAst != NULL) {
                csAssert(fAst->ir != NULL,
                         "destructor forward '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(fAst),
                         type->name ?: "<anonymous>");
                type->strct.dctorFwd = fAst->ir;
            }
            else {
                type->strct.dctorFwd = NULL;
            }

            if (cAst != NULL) {
                csAssert(cAst->ir != NULL,
                         "copy function '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(cAst),
                         type->name ?: "<anonymous>");
                type->strct.copy = cAst->ir;
            }
            else {
                type->strct.copy = NULL;
            }
        }
    }
}

static void visitAsmStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    bool isVolatile = findAttribute(node, S_volatile) != NULL;
    for (AstNode *out = node->inlineAssembly.outputs; out; out = out->next) {
        if (out->asmOperand.operand)
            out->asmOperand.operand->codegen =
                lowerFlattenedOperand(visitor, out->asmOperand.operand);
    }
    for (AstNode *in = node->inlineAssembly.inputs; in; in = in->next) {
        if (in->asmOperand.operand)
            in->asmOperand.operand->codegen =
                lowerFlattenedOperand(visitor, in->asmOperand.operand);
    }
    IrNode *asmNode = makeIrInlineAsm(
        ctx->pool,
        &node->loc,
        node->inlineAssembly.text,
        NULL,
        NULL,
        NULL,
        isVolatile);
    asmNode->ast = node;
    emitStmt(ctx, asmNode);
}

IrModule *lowerToIr(CompilerDriver *driver, AstNode *node)
{
    AstNode *program = nodeIs(node, Metadata) ? node->metadata.node : node;
    csAssert0(program && nodeIs(program, Program));

    LowerContext context = {
        .driver = driver,
        .pool = driver->pool,
        .strings = driver->strings,
        .types = newIrTypeTable(driver->pool, driver->strings),
        .typeMap = newHashTable(sizeof(TypeMapping), driver->pool),
        .currentScopeId = 0,
        .nextScopeId = 1,
        .nextBlockId = 0,
        .tempCounter = 0,
        .scopes = newDynArray(sizeof(ScopeInfo)),
        .loopCtx = NULL};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astNullLit] = lowerNullLit,
        [astBoolLit] = lowerBoolLit,
        [astCharLit] = lowerCharLit,
        [astIntegerLit] = lowerIntegerLit,
        [astFloatLit] = lowerFloatLit,
        [astStringLit] = lowerStringLit,
        [astIdentifier] = lowerIdentifier,
        [astPath] = lowerPath,
        [astMemberExpr] = lowerMemberExpr,
        [astIndexExpr] = lowerIndexExpr,
        [astSliceExpr] = lowerSliceExpr,
        [astCatchExpr] = lowerCatchExpr,
        [astYieldStmt] = visitYieldStmt,
        [astBinaryExpr] = lowerBinaryExpr,
        [astUnaryExpr] = lowerUnaryExpr,
        [astPointerOf] = lowerPointerOf,
        [astReferenceOf] = lowerPointerOf,
        [astCastExpr] = lowerCastExpr,
        [astTypedExpr] = lowerTypedExpr,
        [astCallExpr] = lowerCallExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astGroupExpr] = lowerGroupExpr,
        [astBackendCall] = lowerBackendCall,
        [astArrayExpr] = lowerArrayExpr,
        [astTupleExpr] = lowerTupleExpr,
        [astStructExpr] = lowerStructExpr,
        [astUnionValueExpr] = lowerUnionValueExpr,
        [astClosureValue] = lowerClosureValue,
        [astAliasExpr] = lowerAliasExpr,
        [astVarDecl] = lowerVarDecl,
        [astAssignExpr] = lowerAssignExpr,
        [astExprStmt] = lowerExprStmt,
        [astStmtExpr] = lowerStmtExpr,
        [astBlockStmt] = lowerBlockStmt,
        [astIfStmt] = lowerIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astForStmt] = visitForStmt,
        [astBreakStmt] = visitBreakStmt,
        [astContinueStmt] = visitContinueStmt,
        [astReturnStmt] = lowerReturnStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astMatchStmt] = visitMatchStmt,
        [astAsm] = visitAsmStmt,
        [astFuncDecl] = visitFuncDecl,
        [astExternDecl] = visitExternDecl,
        [astStructDecl] = visitClassOrStructDecl,
        [astClassDecl] = visitClassOrStructDecl
    });
    // clang-format on

    pushOnDynArray(&context.scopes,
                   &(ScopeInfo){.parent = 0, .hasDrops = false});
    astVisit(&visitor, program);

    finalizeLifecycleTypes(&context);
    freeDynArray(&context.scopes);

    if (nodeIs(node, Metadata)) {
        node->metadata.ir = context.module;
    }

    return context.module;
}
