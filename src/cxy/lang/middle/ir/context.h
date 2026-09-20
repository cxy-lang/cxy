#pragma once

#include "node.h"
#include "type.h"
#include "core/log.h"
#include "core/mempool.h"
#include "core/strpool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IrContext {
    MemPool *pool;
    StrPool *strings;
    IrTypeTable *types;
    Log *L;
    IrModule *module;
    IrFunction *currentFunc;
    IrNode *currentBlock;
    u32 nextScopeId;
    u32 tempCounter;
} IrContext;

IrContext *newIrContext(MemPool *pool, StrPool *strings, Log *L);
void freeIrContext(IrContext *ctx);

cstring makeIrTempName(IrContext *ctx, cstring prefix);

#ifdef __cplusplus
}
#endif
