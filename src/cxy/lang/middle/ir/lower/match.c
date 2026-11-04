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
#include "lang/middle/memory/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lowerSwitchStmt(AstVisitor *visitor, AstNode *node)
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

void lowerMatchStmt(AstVisitor *visitor, AstNode *node)
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

