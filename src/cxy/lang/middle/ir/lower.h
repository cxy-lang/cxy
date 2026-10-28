#pragma once

#include "node.h"
#include "type.h"
#include "core/array.h"
#include "driver/driver.h"
#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LoopCtx {
    IrNode *condBlock;
    IrNode *exitBlock;
    u32 scopeId;
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
    IrNode *exceptionPushCallee;
    bool explicitCatch;
} LowerContext;

const IrType *lowerType(LowerContext *ctx, const Type *type);
IrModule *lowerToIr(CompilerDriver *driver, AstNode *node);

#ifdef __cplusplus
}
#endif
