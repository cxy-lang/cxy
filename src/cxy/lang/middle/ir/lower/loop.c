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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lowerWhileStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 whileId = ctx->nextWhileId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_cond", whileId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_body", whileId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_exit", whileId));

    // What the condition needs is declared before the loop, in the block that
    // enters it, and assigned again on every iteration
    IrNode *enterBB = ctx->currentBlock;
    IrNode *prevDeclBlock = ctx->tempDeclBlock;
    ctx->tempDeclBlock = enterBB;
    setBlock(ctx, condBB);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->whileStmt.cond);
    ctx->tempDeclBlock = prevDeclBlock;

    IrNode *condEndBB = ctx->currentBlock;
    setBlock(ctx, enterBB);
    emitBranchIfNotTerminated(ctx, condBB);
    setBlock(ctx, condEndBB);
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

void lowerForStmt(AstVisitor *visitor, AstNode *node)
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

void lowerBreakStmt(AstVisitor *visitor, AstNode *node)
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

void lowerContinueStmt(AstVisitor *visitor, AstNode *node)
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

void lowerYieldStmt(AstVisitor *visitor, AstNode *node)
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

