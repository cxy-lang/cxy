#pragma once

#include "lang/middle/ir/node.h"
#include "lang/middle/ir/type.h"
#include "core/array.h"
#include "driver/driver.h"
#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

// A catch body being lowered, innermost first
typedef struct CatchCtx {
    IrNode *err;
    IrNode *res;
    struct CatchCtx *prev;
} CatchCtx;

typedef struct LoopCtx {
    IrNode *condBlock;
    IrNode *exitBlock;
    u32 scopeId;
    CatchCtx *catchCtx; // catch bodies enclosing the loop
    struct LoopCtx *prev;
} LoopCtx;

typedef struct ScopeInfo {
    u32 parent;    // enclosing scope id (0 = function body)
    bool hasDrops; // scope declared a destructible variable
} ScopeInfo;

typedef struct LowerContext {
    CompilerDriver *driver;
    MemPool *pool;
    StrPool *strings;
    IrTypeTable *types;
    HashTable typeMap;
    IrModule *module;
    IrFunction *currentFunc;
    IrNode *currentBlock;
    // Where the temporaries of a loop condition are declared: they live across
    // iterations, so they are declared once, before the loop
    IrNode *tempDeclBlock;
    IrNode *funcExitBlock;
    IrNode *funcRetVar;
    const Type *funcRetAstType;
    u32 currentScopeId;
    u32 nextScopeId;
    u32 nextBlockId;
    u32 nextForId;
    u32 nextWhileId;
    u32 nextIfId;
    u32 nextSwitchId;
    u32 nextMatchId;
    u32 nextOptId;
    u32 nextTernaryId;
    u32 nextLogicId;
    u32 tempCounter;
    HashTable varNames;
    DynArray scopes; // ScopeInfo, indexed by scope id
    LoopCtx *loopCtx;
    IrNode *catchTargetVar;
    IrNode *catchMergeBB;
    IrNode *currentCatchErr;
    IrNode *currentCatchRes;
    CatchCtx *catchCtx;
    IrNode *exceptionPushCallee;
    bool explicitCatch;
} LowerContext;

const IrType *lowerType(LowerContext *ctx, const Type *type);
IrModule *lowerToIr(CompilerDriver *driver, AstNode *node);

#ifdef __cplusplus
}
#endif
