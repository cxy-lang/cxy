#pragma once

// Helpers shared by the files that make up the AST to IR lowering. Nothing here
// is part of the public interface, that is `lower.h`.

#include "lower.h"

#include "lang/frontend/flag.h"

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

const IrType *findCachedType(LowerContext *ctx, const Type *type);
void cacheType(LowerContext *ctx, const Type *type, const IrType *irType);
cstring getIrTypeName(LowerContext *ctx, const IrType *type);
cstring readableTypeName(LowerContext *ctx, const Type *type);
const IrType *lowerClosureFnPtrType(LowerContext *ctx, const Type *closureType);

cstring makeDescriptiveTupleName(LowerContext *ctx,
                                 const Type *type,
                                 IrField *fields,
                                 u64 count);
cstring makeDescriptiveOptionalName(LowerContext *ctx,
                                    const Type *type,
                                    const IrType *targetType);
cstring makeDescriptiveResultName(LowerContext *ctx,
                                  const Type *type,
                                  const IrType *targetType);
cstring makeDescriptiveUnionName(LowerContext *ctx,
                                 const Type *type,
                                 IrField *fields,
                                 u64 count);
cstring makeDescriptiveUnionDisplay(LowerContext *ctx,
                                    IrField *fields,
                                    u64 count);
cstring makeDescriptiveTupleDisplay(LowerContext *ctx,
                                    IrField *fields,
                                    u64 count);
cstring makeDescriptiveOptionalDisplay(LowerContext *ctx,
                                       const IrType *targetType);
cstring makeDescriptiveResultDisplay(LowerContext *ctx,
                                     const IrType *targetType);

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
