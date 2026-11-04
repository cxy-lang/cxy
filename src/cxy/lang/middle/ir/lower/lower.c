#include "lower.h"
#include "internal.h"

#include "core/hash.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"

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
