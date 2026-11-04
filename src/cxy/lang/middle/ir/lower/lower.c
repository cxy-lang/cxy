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
        [astProgram] = lowerProgram,
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
        [astYieldStmt] = lowerYieldStmt,
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
        [astWhileStmt] = lowerWhileStmt,
        [astForStmt] = lowerForStmt,
        [astBreakStmt] = lowerBreakStmt,
        [astContinueStmt] = lowerContinueStmt,
        [astReturnStmt] = lowerReturnStmt,
        [astSwitchStmt] = lowerSwitchStmt,
        [astMatchStmt] = lowerMatchStmt,
        [astAsm] = lowerAsmStmt,
        [astFuncDecl] = lowerFuncDecl,
        [astExternDecl] = lowerExternDecl,
        [astStructDecl] = lowerClassOrStructDecl,
        [astClassDecl] = lowerClassOrStructDecl
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
