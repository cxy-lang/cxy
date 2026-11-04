#pragma once

// Helpers shared by the files that make up the AST to IR lowering. Nothing here
// is part of the public interface, that is `lower.h`.

#include "lower.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Blocks and statements
// ---------------------------------------------------------------------------

static inline bool isBlockTerminated(const IrNode *bb)
{
    if (bb == NULL || bb->tag != irBasicBlock || bb->basicBlock.last == NULL)
        return false;
    switch (bb->basicBlock.last->tag) {
    case irBranch:
    case irBranchIf:
    case irSwitch:
    case irReturn:
    case irUnreachable:
        return true;
    default:
        return false;
    }
}

static inline void emitStmt(LowerContext *ctx, IrNode *stmt)
{
    if (ctx->currentBlock && stmt && !isBlockTerminated(ctx->currentBlock)) {
        irAppendBlockStmt(ctx->currentBlock, stmt);
        // Only scopes with destructible vars need an unwind path on return
        if (stmt->tag == irVarDecl && hasFlag(stmt, ReferenceMembers) &&
            stmt->varDecl.scopeId < ctx->scopes.size)
            dynArrayAt(ScopeInfo *, &ctx->scopes, stmt->varDecl.scopeId)
                .hasDrops = true;
    }
}

static inline IrNode *createBlock(LowerContext *ctx, cstring name)
{
    cstring blockName = makeStringf(ctx->strings, "_L_%s", name ?: "bb");
    IrNode *bb = makeIrBasicBlock(ctx->pool, NULL, blockName);
    if (ctx->currentFunc) {
        irAppendFunctionBlock(ctx->currentFunc, bb);
    }
    return bb;
}

static inline void setBlock(LowerContext *ctx, IrNode *bb)
{
    ctx->currentBlock = bb;
}

static inline void emitBranchIfNotTerminated(LowerContext *ctx, IrNode *target)
{
    if (ctx->currentBlock && !isBlockTerminated(ctx->currentBlock)) {
        emitStmt(ctx, makeIrBranch(ctx->pool, NULL, target));
    }
}

// A variable name that is unique in the function, `x`, `x_1`, `x_2`... (names.c)
typedef struct VarNameEntry {
    cstring name;
    u32 count;
} VarNameEntry;

cstring makeUniqueVarName(LowerContext *ctx, cstring displayName);

// ---------------------------------------------------------------------------
// Types (type.c)
// ---------------------------------------------------------------------------

typedef struct TypeMapping {
    const Type *astType;
    const IrType *irType;
} TypeMapping;

const IrType *lowerClosureFnPtrType(LowerContext *ctx, const Type *closureType);

// ---------------------------------------------------------------------------
// Operands (operand.c)
// ---------------------------------------------------------------------------

bool isIrPureValueExpr(const IrNode *node);

// Stores `val` in a fresh temporary and returns a reference to it
IrNode *flattenIrValue(LowerContext *ctx,
                       AstNode *node,
                       IrNode *val,
                       bool borrowed);

// An operand evaluated once, in order, before the statement
IrNode *lowerFlattenedOperand(AstVisitor *visitor, AstNode *node);
// Same, for a consumer that only reads it
IrNode *lowerBorrowedOperand(AstVisitor *visitor, AstNode *node);
// An element of a struct, tuple or array literal
IrNode *lowerCompositeElement(AstVisitor *visitor, AstNode *node);
IrNode *lowerConditionOperand(LowerContext *ctx,
                              AstVisitor *visitor,
                              AstNode *condNode);

void lowerSliceIntoTarget(LowerContext *ctx,
                          AstVisitor *visitor,
                          const AstNode *expr,
                          IrNode *targetLValue);
void lowerOptionalIntoTarget(LowerContext *ctx,
                             AstVisitor *visitor,
                             const AstNode *expr,
                             IrNode *targetLValue);
void lowerTaggedUnionIntoTarget(LowerContext *ctx,
                                AstVisitor *visitor,
                                const AstNode *expr,
                                IrNode *targetLValue,
                                const Type *targetAstType);

// ---------------------------------------------------------------------------
// Literals, operators, assignment and coercions (operator.c)
// ---------------------------------------------------------------------------

void lowerNullLit(AstVisitor *visitor, AstNode *node);
void lowerBoolLit(AstVisitor *visitor, AstNode *node);
void lowerCharLit(AstVisitor *visitor, AstNode *node);
void lowerIntegerLit(AstVisitor *visitor, AstNode *node);
void lowerFloatLit(AstVisitor *visitor, AstNode *node);
void lowerStringLit(AstVisitor *visitor, AstNode *node);
void lowerBinaryExpr(AstVisitor *visitor, AstNode *node);
void lowerUnaryExpr(AstVisitor *visitor, AstNode *node);
void lowerCastExpr(AstVisitor *visitor, AstNode *node);
void lowerTypedExpr(AstVisitor *visitor, AstNode *node);
void lowerAssignExpr(AstVisitor *visitor, AstNode *node);

// Sets `UserWritten` on `ir` when `ast` came from the source
IrNode *markUserWritten(const AstNode *ast, IrNode *ir);

// `@copy x` as written, one operand
static inline bool isBackendCopyExpr(const AstNode *node)
{
    return node && nodeIs(node, BackendCall) &&
           node->backendCallExpr.func == bfiCopy &&
           node->backendCallExpr.args &&
           node->backendCallExpr.args->next == NULL;
}

// ---------------------------------------------------------------------------
// Calls (call.c)
// ---------------------------------------------------------------------------

void lowerCallExpr(AstVisitor *visitor, AstNode *node);

// ---------------------------------------------------------------------------
// The `!T` protocol (result.c)
// ---------------------------------------------------------------------------

// Check the tag of a call result, return the error to the caller or give back
// the unwrapped payload
IrNode *emitResultCheckAndUnwrap(LowerContext *ctx,
                                 AstNode *node,
                                 IrNode *resVar,
                                 const Type *resAstType);

// Pushes a raised exception on the exception stack trace
void emitExceptionTracePush(LowerContext *ctx,
                            const FileLoc *loc,
                            IrNode *errVal);

// The payload of a call result is owned by the caller
bool isOwnedResultPayload(const IrNode *expr);

void lowerCatchExpr(AstVisitor *visitor, AstNode *node);

// `return x` and `raise x` in a function that returns `!T`
void lowerReturnIntoResult(AstVisitor *visitor, AstNode *node);
// A bare `return` or `raise` in a function that returns `!T`
void lowerReturnWithoutValue(LowerContext *ctx, AstNode *node);

// ---------------------------------------------------------------------------
// Loops, jumps and scope unwinding (loop.c)
// ---------------------------------------------------------------------------

void lowerWhileStmt(AstVisitor *visitor, AstNode *node);
void lowerForStmt(AstVisitor *visitor, AstNode *node);
void lowerBreakStmt(AstVisitor *visitor, AstNode *node);
void lowerContinueStmt(AstVisitor *visitor, AstNode *node);
void lowerYieldStmt(AstVisitor *visitor, AstNode *node);

// Releases the catch bodies entered since `outer`, innermost first
void emitReleaseCaughtUntil(LowerContext *ctx,
                            const FileLoc *loc,
                            const CatchCtx *outer);

// The block a `return` in `scopeId` jumps to, unwinding the scopes in between
IrNode *getOrCreateScopeReturnBlock(LowerContext *ctx,
                                    u32 scopeId,
                                    const FileLoc *loc);

// ---------------------------------------------------------------------------
// `switch` and `match` (match.c)
// ---------------------------------------------------------------------------

void lowerSwitchStmt(AstVisitor *visitor, AstNode *node);
void lowerMatchStmt(AstVisitor *visitor, AstNode *node);

// ---------------------------------------------------------------------------
// Functions, types, the program and `asm` (decl.c)
// ---------------------------------------------------------------------------

void lowerProgram(AstVisitor *visitor, AstNode *node);
void lowerFuncDecl(AstVisitor *visitor, AstNode *node);
void lowerExternDecl(AstVisitor *visitor, AstNode *node);
void lowerClassOrStructDecl(AstVisitor *visitor, AstNode *node);
void lowerAsmStmt(AstVisitor *visitor, AstNode *node);

// Runs after the whole program: copy and destructor glue of every type
void finalizeLifecycleTypes(LowerContext *ctx);

// ---------------------------------------------------------------------------
// Declarations and statements (stmt.c)
// ---------------------------------------------------------------------------

void lowerVarDecl(AstVisitor *visitor, AstNode *node);
void lowerExprStmt(AstVisitor *visitor, AstNode *node);
void lowerStmtExpr(AstVisitor *visitor, AstNode *node);
void lowerBlockStmt(AstVisitor *visitor, AstNode *node);
void lowerIfStmt(AstVisitor *visitor, AstNode *node);
void lowerReturnStmt(AstVisitor *visitor, AstNode *node);

// Wrappers that only pass their operand through (operand.c)
void lowerGroupExpr(AstVisitor *visitor, AstNode *node);
void lowerAliasExpr(AstVisitor *visitor, AstNode *node);

// ---------------------------------------------------------------------------
// Array, tuple, struct and union values (aggregate.c)
// ---------------------------------------------------------------------------

void lowerArrayExpr(AstVisitor *visitor, AstNode *node);
void lowerTupleExpr(AstVisitor *visitor, AstNode *node);
void lowerStructExpr(AstVisitor *visitor, AstNode *node);
void lowerUnionValueExpr(AstVisitor *visitor, AstNode *node);

// Closure values and environments (closure.c)
void lowerClosureValue(AstVisitor *visitor, AstNode *node);

// Calls of the compiler builtins: copy, move, drop, alloc... (backend.c)
void lowerBackendCall(AstVisitor *visitor, AstNode *node);

// ---------------------------------------------------------------------------
// Names and member access (access.c)
// ---------------------------------------------------------------------------

void lowerIdentifier(AstVisitor *visitor, AstNode *node);
void lowerPath(AstVisitor *visitor, AstNode *node);
void lowerMemberExpr(AstVisitor *visitor, AstNode *node);
void lowerIndexExpr(AstVisitor *visitor, AstNode *node);
void lowerSliceExpr(AstVisitor *visitor, AstNode *node);
// `&x` and `^x`, also used for `ReferenceOf`
void lowerPointerOf(AstVisitor *visitor, AstNode *node);

// ---------------------------------------------------------------------------
// Symbol names (names.c)
// ---------------------------------------------------------------------------

cstring makeScopedSymbolName(LowerContext *ctx,
                             const AstNode *decl,
                             cstring baseName);
cstring getFunctionSymbolName(LowerContext *ctx, const AstNode *funcDecl);
cstring getNormalizedName(LowerContext *ctx, const AstNode *node);

#ifdef __cplusplus
}
#endif
