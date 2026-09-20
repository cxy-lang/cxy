#include "context.h"
#include <stdio.h>

IrContext *newIrContext(MemPool *pool, StrPool *strings, Log *L)
{
    IrContext *ctx = allocFromMemPool(pool, sizeof(IrContext));
    ctx->pool = pool;
    ctx->strings = strings;
    ctx->types = newIrTypeTable(pool, strings);
    ctx->L = L;
    ctx->module = NULL;
    ctx->currentFunc = NULL;
    ctx->currentBlock = NULL;
    ctx->nextScopeId = 0;
    ctx->tempCounter = 0;
    return ctx;
}

void freeIrContext(IrContext *ctx)
{
    if (ctx && ctx->types) {
        freeIrTypeTable(ctx->types);
    }
}

cstring makeIrTempName(IrContext *ctx, cstring prefix)
{
    return makeStringf(ctx->strings, "%s%u", prefix ?: "__t", ctx->tempCounter++);
}
