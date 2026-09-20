#pragma once

#include "node.h"
#include "type.h"
#include "driver/driver.h"
#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LoopCtx {
    IrNode *condBlock;
    IrNode *exitBlock;
    struct LoopCtx *prev;
} LoopCtx;

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
    u32 currentScopeId;
    u32 nextScopeId;
    u32 nextBlockId;
    u32 tempCounter;
    HashTable varNames;
    LoopCtx *loopCtx;
} LowerContext;

const IrType *lowerType(LowerContext *ctx, const Type *type);
IrModule *lowerToIr(CompilerDriver *driver, AstNode *node);

#ifdef __cplusplus
}
#endif
