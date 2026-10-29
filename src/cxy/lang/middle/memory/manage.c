#include "memory.h"
#include "core/hash.h"
#include "lang/frontend/flag.h"

bool isIrTypeCopyable(const IrType *type)
{
    return isIrTypeDestructible(type);
}

static inline bool isIrVarDestructible(const IrNode *decl)
{
    if (decl == NULL || hasFlag(decl, Returned))
        return false;
    return hasFlag(decl, ReferenceMembers) || isIrTypeDestructible(decl->type);
}

static inline bool compareTrackedVarDecl(const void *a, const void *b)
{
    const TrackedVar *va = *(const TrackedVar **)a;
    const IrNode *decl = (const IrNode *)b;
    return va && va->decl == decl;
}

static TrackedVar *getTrackedVar(MemMgmtContext *ctx, const IrNode *decl)
{
    if (decl == NULL)
        return NULL;
    TrackedVar **found = findInHashTable(&ctx->trackedVars,
                                         decl,
                                         hashPtr(hashInit(), decl),
                                         sizeof(TrackedVar *),
                                         compareTrackedVarDecl);
    return found ? *found : NULL;
}

static TrackedVar *addTrackedVar(MemMgmtContext *ctx, IrNode *decl, cstring name, u32 scopeId, const IrType *type)
{
    u32 index = (u32)ctx->trackedVarList.size;
    TrackedVar *var = allocFromMemPool(ctx->localPool, sizeof(TrackedVar));
    *var = (TrackedVar){
        .decl = decl,
        .name = name,
        .scopeId = scopeId,
        .varIndex = index,
        .dropFlagIndex = 0,
        .needsDropFlag = false,
        .isTemporary = (decl && hasFlag(decl, Temporary)),
        .isEphemeral = (decl && hasFlag(decl, Ephemeral)),
        .type = type,
        .state = irVarUninitialized
    };

    insertInHashTable(&ctx->trackedVars,
                      &var,
                      hashPtr(hashInit(), decl),
                      sizeof(TrackedVar *),
                      compareTrackedVarDecl);

    pushOnDynArray(&ctx->trackedVarList, &var);
    return var;
}

static void collectScopeVariables(MemMgmtContext *ctx, IrNode *func)
{
    for (IrNode *param = func->function.params; param; param = param->next) {
        if (isIrVarDestructible(param)) {
            TrackedVar *tv = addTrackedVar(ctx, param, param->funcParam.name, 0, param->type);
            if (tv)
                tv->state = irVarAssigned;
        }
    }

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irVarDecl && isIrVarDestructible(stmt)) {
                addTrackedVar(ctx, stmt, stmt->varDecl.name, stmt->varDecl.scopeId, stmt->type);
            }
        }
    }
}

static IrNode *findVarDeclInNode(const IrNode *node)
{
    if (node == NULL)
        return NULL;
    if (node->tag == irVarRef)
        return node->varRef.target;
    if (node->tag == irMember)
        return findVarDeclInNode(node->member.target);
    if (node->tag == irIndex)
        return findVarDeclInNode(node->index.target);
    if (node->tag == irDeref)
        return findVarDeclInNode(node->deref.target);
    return NULL;
}

static CfgBlockInfo *getCfgBlock(CfgBlockInfo **blocks, u64 count, const IrNode *bb)
{
    if (bb == NULL)
        return NULL;
    for (u64 i = 0; i < count; i++) {
        if (blocks[i]->bb == bb)
            return blocks[i];
    }
    return NULL;
}

static void addCfgEdge(CfgBlockInfo *src, CfgBlockInfo *dst)
{
    if (src == NULL || dst == NULL)
        return;
    for (size_t i = 0; i < src->succs.size; i++) {
        if (dynArrayAt(CfgBlockInfo **, &src->succs, i) == dst)
            return;
    }
    pushOnDynArray(&src->succs, &dst);
    pushOnDynArray(&dst->preds, &src);
}

static CfgBlockInfo **buildFunctionCfg(MemMgmtContext *ctx, IrNode *func, u64 *outCount)
{
    u64 count = 0;
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag == irBasicBlock)
            count++;
    }

    *outCount = count;
    if (count == 0)
        return NULL;

    CfgBlockInfo **blocks = allocFromMemPool(ctx->localPool, sizeof(CfgBlockInfo *) * count);
    u64 idx = 0;
    u64 numVars = ctx->trackedVarList.size;

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        CfgBlockInfo *info = allocFromMemPool(ctx->localPool, sizeof(CfgBlockInfo));
        info->bb = bb;
        info->gen = makeBitSet(ctx->localPool, numVars);
        info->kill = makeBitSet(ctx->localPool, numVars);
        info->mayIn = makeBitSet(ctx->localPool, numVars);
        info->mayOut = makeBitSet(ctx->localPool, numVars);
        info->mustIn = makeBitSet(ctx->localPool, numVars);
        info->mustOut = makeBitSet(ctx->localPool, numVars);
        info->preds = newDynArray(sizeof(CfgBlockInfo *));
        info->succs = newDynArray(sizeof(CfgBlockInfo *));
        blocks[idx++] = info;
    }

    for (u64 i = 0; i < count; i++) {
        CfgBlockInfo *src = blocks[i];
        for (IrNode *stmt = src->bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irBranch) {
                addCfgEdge(src, getCfgBlock(blocks, count, stmt->branch.target));
            }
            else if (stmt->tag == irBranchIf) {
                addCfgEdge(src, getCfgBlock(blocks, count, stmt->branchIf.trueTarget));
                addCfgEdge(src, getCfgBlock(blocks, count, stmt->branchIf.falseTarget));
            }
            else if (stmt->tag == irSwitch) {
                for (u64 c = 0; c < stmt->switchStmt.casesCount; c++) {
                    addCfgEdge(src, getCfgBlock(blocks, count, stmt->switchStmt.cases[c].target));
                }
                if (stmt->switchStmt.defaultTarget)
                    addCfgEdge(src, getCfgBlock(blocks, count, stmt->switchStmt.defaultTarget));
            }
        }
    }

    return blocks;
}

static IrNode *rebuildCastChain(MemPool *pool, const IrNode *castTree, IrNode *newRoot)
{
    if (castTree == NULL || castTree->tag != irCast)
        return newRoot;
    IrNode *inner = rebuildCastChain(pool, castTree->cast.expr, newRoot);
    return makeIrCast(pool, &castTree->loc, inner, castTree->type);
}

static inline IrNode *unwrapCasts(IrNode *node)
{
    while (node && node->tag == irCast)
        node = node->cast.expr;
    return node;
}

static inline const IrNode *unwrapCastsConst(const IrNode *node)
{
    while (node && node->tag == irCast)
        node = node->cast.expr;
    return node;
}

static inline IrNode *findMovedVarDecl(const IrNode *src)
{
    src = unwrapCastsConst(src);
    if (src && src->tag == irVarRef)
        return src->varRef.target;
    return NULL;
}

// The single definition of "this operand is a move": returns the slot holding an
// irMove, looking through casts (`<Base>(&&x)`), or NULL.
static IrNode **moveSlot(IrNode **slot)
{
    while (slot && *slot && (*slot)->tag == irCast)
        slot = &(*slot)->cast.expr;
    return (slot && *slot && (*slot)->tag == irMove) ? slot : NULL;
}


// Visits every slot through which a value is handed over (assign rhs itself,
// call args, composite payloads).
static void forEachOwningSlot(IrNode **slot, void (*cb)(IrNode **slot, void *ctx), void *cbCtx)
{
    IrNode *v = slot ? *slot : NULL;
    if (v == NULL)
        return;
    switch (v->tag) {
    case irUnionValue:
        cb(&v->unionValue.value, cbCtx);
        break;
    case irOptionalValue:
        if (v->optionalValue.hasValue)
            cb(&v->optionalValue.value, cbCtx);
        break;
    case irStructConst:
        for (IrNode **f = &v->structConst.fields; *f; f = &(*f)->next)
            cb(((*f)->tag == irFieldConst) ? &(*f)->fieldConst.value : f, cbCtx);
        break;
    case irArrayConst:
        for (IrNode **e = &v->arrayConst.elements; *e; e = &(*e)->next)
            cb(e, cbCtx);
        break;
    case irClosureValue:
        cb(&v->closureValue.env, cbCtx);
        break;
    case irCall:
        for (IrNode **a = &v->call.args; *a; a = &(*a)->next)
            cb(a, cbCtx);
        break;
    default:
        cb(slot, cbCtx);
        break;
    }
}

typedef struct MovedSourceVisit {
    void (*cb)(IrNode *src, void *ctx);
    void *ctx;
} MovedSourceVisit;

static void onOwningSlotMovedSource(IrNode **slot, void *ctxPtr)
{
    MovedSourceVisit *visit = ctxPtr;
    IrNode **mv = moveSlot(slot);
    if (mv)
        visit->cb((*mv)->move.src, visit->ctx);
}

static void forEachMovedSource(IrNode *rhs, void (*cb)(IrNode *src, void *ctx), void *cbCtx)
{
    MovedSourceVisit visit = {.cb = cb, .ctx = cbCtx};
    forEachOwningSlot(&rhs, onOwningSlotMovedSource, &visit);
}

typedef struct MoveGenKillContext {
    MemMgmtContext *ctx;
    CfgBlockInfo *block;
} MoveGenKillContext;

static void onMoveInGenKill(IrNode *src, void *ctxPtr)
{
    MoveGenKillContext *data = ctxPtr;
    IrNode *rhsDecl = findMovedVarDecl(src);
    TrackedVar *rhsVar = getTrackedVar(data->ctx, rhsDecl);
    if (rhsVar && !rhsVar->isTemporary) {
        bitSetSet(&data->block->kill, rhsVar->varIndex);
        bitSetReset(&data->block->gen, rhsVar->varIndex);
    }
}

static void computeIntraBlockGenKill(MemMgmtContext *ctx, CfgBlockInfo *block)
{
    for (IrNode *stmt = block->bb->basicBlock.first; stmt; stmt = stmt->next) {
        if (stmt->tag == irAssign) {
            IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
            TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
            if (lhsVar && !lhsVar->isTemporary) {
                if (stmt->assign.rhs && (stmt->assign.rhs->tag == irNullConst || (stmt->assign.rhs->tag == irOptionalValue && !stmt->assign.rhs->optionalValue.hasValue))) {
                    if (stmt->assign.lhs->tag == irVarRef) {
                        bitSetSet(&block->kill, lhsVar->varIndex);
                        bitSetReset(&block->gen, lhsVar->varIndex);
                    }
                }
                else {
                    bitSetSet(&block->gen, lhsVar->varIndex);
                    bitSetReset(&block->kill, lhsVar->varIndex);
                }
            }

            MoveGenKillContext moveCtx = {.ctx = ctx, .block = block};
            forEachMovedSource(stmt->assign.rhs, onMoveInGenKill, &moveCtx);
        }
        else if (stmt->tag == irCall) {
            MoveGenKillContext moveCtx = {.ctx = ctx, .block = block};
            forEachMovedSource(stmt, onMoveInGenKill, &moveCtx);
        }
        else if (stmt->tag == irCopy) {
            IrNode *dstDecl = findVarDeclInNode(stmt->copy.dst);
            TrackedVar *dstVar = getTrackedVar(ctx, dstDecl);
            if (dstVar && !dstVar->isTemporary) {
                bitSetSet(&block->gen, dstVar->varIndex);
                bitSetReset(&block->kill, dstVar->varIndex);
            }
        }
        else if (stmt->tag == irDrop) {
            IrNode *targetDecl = findVarDeclInNode(stmt->drop.target);
            TrackedVar *var = getTrackedVar(ctx, targetDecl);
            if (var && !var->isTemporary) {
                bitSetSet(&block->kill, var->varIndex);
                bitSetReset(&block->gen, var->varIndex);
            }
        }
    }
}

static void runDataflowAnalysis(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count, u64 varCount)
{
    if (count == 0 || varCount == 0)
        return;

    for (u64 i = 0; i < count; i++) {
        computeIntraBlockGenKill(ctx, blocks[i]);
    }

    CfgBlockInfo *entry = blocks[0];
    for (u64 i = 0; i < ctx->trackedVarList.size; i++) {
        TrackedVar *v = ((TrackedVar **)ctx->trackedVarList.elems)[i];
        if (v && v->decl && v->decl->tag == irFuncParam) {
            bitSetSet(&entry->mayIn, v->varIndex);
            bitSetSet(&entry->mustIn, v->varIndex);
        }
    }

    bitSetCopy(&entry->mayOut, &entry->mayIn);
    for (u64 v = 0; v < varCount; v++) {
        if (bitSetTest(&entry->kill, v))
            bitSetReset(&entry->mayOut, v);
    }
    bitSetUnion(&entry->mayOut, &entry->gen);

    bitSetCopy(&entry->mustOut, &entry->mustIn);
    for (u64 v = 0; v < varCount; v++) {
        if (bitSetTest(&entry->kill, v))
            bitSetReset(&entry->mustOut, v);
    }
    bitSetUnion(&entry->mustOut, &entry->gen);

    for (u64 i = 1; i < count; i++) {
        bitSetClearAll(&blocks[i]->mayIn);
        bitSetCopy(&blocks[i]->mayOut, &blocks[i]->gen);

        bitSetSetAll(&blocks[i]->mustIn);
        bitSetCopy(&blocks[i]->mustOut, &blocks[i]->mustIn);
        for (u64 v = 0; v < varCount; v++) {
            if (bitSetTest(&blocks[i]->kill, v))
                bitSetReset(&blocks[i]->mustOut, v);
        }
        bitSetUnion(&blocks[i]->mustOut, &blocks[i]->gen);
    }

    BitSet tempMay = makeBitSet(ctx->localPool, varCount);
    BitSet tempMust = makeBitSet(ctx->localPool, varCount);
    bool changed = true;

    while (changed) {
        changed = false;
        for (u64 i = 0; i < count; i++) {
            CfgBlockInfo *block = blocks[i];
            if (block->preds.size > 0) {
                bitSetClearAll(&tempMay);
                bitSetSetAll(&tempMust);

                for (size_t p = 0; p < block->preds.size; p++) {
                    CfgBlockInfo *pred = dynArrayAt(CfgBlockInfo **, &block->preds, p);
                    bitSetUnion(&tempMay, &pred->mayOut);
                    bitSetIntersect(&tempMust, &pred->mustOut);
                }

                if (i == 0) {
                    for (u64 v = 0; v < varCount; v++) {
                        TrackedVar *tv = ((TrackedVar **)ctx->trackedVarList.elems)[v];
                        if (tv && tv->decl && tv->decl->tag == irFuncParam) {
                            bitSetSet(&tempMay, v);
                            bitSetSet(&tempMust, v);
                        }
                    }
                }

                if (!bitSetEquals(&block->mayIn, &tempMay)) {
                    bitSetCopy(&block->mayIn, &tempMay);
                    changed = true;
                }
                if (!bitSetEquals(&block->mustIn, &tempMust)) {
                    bitSetCopy(&block->mustIn, &tempMust);
                    changed = true;
                }
            }

            bitSetCopy(&tempMay, &block->mayIn);
            for (u64 v = 0; v < varCount; v++) {
                if (bitSetTest(&block->kill, v))
                    bitSetReset(&tempMay, v);
            }
            bitSetUnion(&tempMay, &block->gen);

            bitSetCopy(&tempMust, &block->mustIn);
            for (u64 v = 0; v < varCount; v++) {
                if (bitSetTest(&block->kill, v))
                    bitSetReset(&tempMust, v);
            }
            bitSetUnion(&tempMust, &block->gen);

            if (!bitSetEquals(&block->mayOut, &tempMay)) {
                bitSetCopy(&block->mayOut, &tempMay);
                changed = true;
            }
            if (!bitSetEquals(&block->mustOut, &tempMust)) {
                bitSetCopy(&block->mustOut, &tempMust);
                changed = true;
            }
        }
    }
}

typedef struct MoveDropFlagContext {
    MemMgmtContext *ctx;
    BitSet *currMay;
    BitSet *currMust;
} MoveDropFlagContext;

static void onMoveInDropFlagId(IrNode *src, void *ctxPtr)
{
    MoveDropFlagContext *data = ctxPtr;
    IrNode *rhsDecl = findMovedVarDecl(src);
    TrackedVar *rhsVar = getTrackedVar(data->ctx, rhsDecl);
    if (rhsVar && !rhsVar->isTemporary) {
        bitSetReset(data->currMay, rhsVar->varIndex);
        bitSetReset(data->currMust, rhsVar->varIndex);
    }
}

static void identifyDropFlagVariables(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count)
{
    u64 varCount = ctx->trackedVarList.size;
    BitSet currMay = makeBitSet(ctx->localPool, varCount);
    BitSet currMust = makeBitSet(ctx->localPool, varCount);

    for (u64 i = 0; i < count; i++) {
        CfgBlockInfo *block = blocks[i];
        bitSetCopy(&currMay, &block->mayIn);
        bitSetCopy(&currMust, &block->mustIn);

        for (u64 v = 0; v < varCount; v++) {
            if (bitSetTest(&currMay, v) && !bitSetTest(&currMust, v)) {
                TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[v];
                if (var && !var->isTemporary && !var->needsDropFlag) {
                    var->needsDropFlag = true;
                    var->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                    pushOnDynArray(&ctx->dropFlagVars, &var);
                }
            }
        }

        for (IrNode *stmt = block->bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irAssign) {
                IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
                TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
                if (lhsVar && !lhsVar->isTemporary) {
                    if (stmt->assign.lhs->tag == irVarRef && bitSetTest(&currMay, lhsVar->varIndex) && !bitSetTest(&currMust, lhsVar->varIndex)) {
                        if (!lhsVar->needsDropFlag) {
                            lhsVar->needsDropFlag = true;
                            lhsVar->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                            pushOnDynArray(&ctx->dropFlagVars, &lhsVar);
                        }
                    }
                    if (stmt->assign.rhs && (stmt->assign.rhs->tag == irNullConst || (stmt->assign.rhs->tag == irOptionalValue && !stmt->assign.rhs->optionalValue.hasValue))) {
                        if (stmt->assign.lhs->tag == irVarRef) {
                            bitSetReset(&currMay, lhsVar->varIndex);
                            bitSetReset(&currMust, lhsVar->varIndex);
                        }
                    }
                    else {
                        bitSetSet(&currMay, lhsVar->varIndex);
                        bitSetSet(&currMust, lhsVar->varIndex);
                    }
                }

                MoveDropFlagContext moveCtx = {.ctx = ctx, .currMay = &currMay, .currMust = &currMust};
                forEachMovedSource(stmt->assign.rhs, onMoveInDropFlagId, &moveCtx);
            }
            else if (stmt->tag == irScopeExit) {
                u32 exitingScope = stmt->scopeExit.scopeId;
                for (u64 v = 0; v < varCount; v++) {
                    TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[v];
                    if (var && var->scopeId == exitingScope && !var->isTemporary) {
                        if (bitSetTest(&currMay, v) && !bitSetTest(&currMust, v)) {
                            if (!var->needsDropFlag) {
                                var->needsDropFlag = true;
                                var->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                                pushOnDynArray(&ctx->dropFlagVars, &var);
                            }
                        }
                        bitSetReset(&currMay, v);
                        bitSetReset(&currMust, v);
                    }
                }
            }
        }
    }
}

static IrNode *makeDropFlagAssign(MemMgmtContext *ctx, const FileLoc *loc, TrackedVar *var, bool value);

static void synthesizeDropFlags(MemMgmtContext *ctx, IrNode *func)
{
    u64 flagCount = ctx->dropFlagVars.size;
    if (flagCount == 0)
        return;

    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * flagCount);

    for (u64 i = 0; i < flagCount; i++) {
        TrackedVar *var = ((TrackedVar **)ctx->dropFlagVars.elems)[i];
        fields[i] = (IrField){.name = var->name, .type = u64Type, .index = (u32)i, .bits = 1};
    }

    csAssert0(func->function.name);
    cstring typeName = makeStringf(ctx->strings, "__drop_flags_%s_t", func->function.name);
    const IrType *dropFlagsType = makeIrStructType(
        ctx->types, flgNone, (IrStructOpts){0}, typeName, typeName, fields, flagCount);
    IrNode *dropFlagsDecl = makeIrVarDecl(ctx->pool, &func->loc, flgNone, "__drop_flags", "__drop_flags", dropFlagsType, 0);
    ctx->dropFlagsDecl = dropFlagsDecl;

    IrNode *entryBB = func->function.firstBlock;
    if (entryBB) {
        dropFlagsDecl->next = entryBB->basicBlock.first;
        entryBB->basicBlock.first = dropFlagsDecl;

        IrNode *zeromem = makeIrZeromem(ctx->pool, &func->loc, makeIrVarRef(ctx->pool, &func->loc, dropFlagsDecl));
        zeromem->next = dropFlagsDecl->next;
        dropFlagsDecl->next = zeromem;

        // Parameters are owned on entry, nothing else would raise their flag
        IrNode *last = zeromem;
        for (u64 i = 0; i < flagCount; i++) {
            TrackedVar *var = ((TrackedVar **)ctx->dropFlagVars.elems)[i];
            if (var->decl == NULL || var->decl->tag != irFuncParam)
                continue;
            IrNode *setFlag = makeDropFlagAssign(ctx, &func->loc, var, true);
            if (setFlag == NULL)
                continue;
            setFlag->next = last->next;
            last->next = setFlag;
            last = setFlag;
        }
    }
}

static IrNode *makeDropFlagAssign(MemMgmtContext *ctx, const FileLoc *loc, TrackedVar *var, bool value)
{
    if (ctx->dropFlagsDecl == NULL || !var->needsDropFlag)
        return NULL;
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    IrNode *flagsRef = makeIrVarRef(ctx->pool, loc, ctx->dropFlagsDecl);
    IrNode *member = makeIrMember(ctx->pool, loc, flagsRef, var->dropFlagIndex, var->name, u64Type);
    IrNode *valNode = makeIrIntConst(ctx->pool, loc, value ? 1 : 0, false, u64Type);
    return makeIrAssign(ctx->pool, loc, member, valNode);
}

static IrNode *makeDropFlagAccess(MemMgmtContext *ctx, const FileLoc *loc, TrackedVar *var)
{
    if (ctx->dropFlagsDecl == NULL || !var->needsDropFlag)
        return NULL;
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    IrNode *flagsRef = makeIrVarRef(ctx->pool, loc, ctx->dropFlagsDecl);
    return makeIrMember(ctx->pool, loc, flagsRef, var->dropFlagIndex, var->name, u64Type);
}

// A member of a call result temporary holds nothing else on success, so its
// ownership moves to the consumer instead of being copied
static bool isUnwrappedCallResult(const IrNode *src)
{
    IrNode *decl = findVarDeclInNode(src);
    return decl && hasFlag(decl, Temporary) && decl->type &&
           decl->type->category == irTypeCatResult;
}

static void processPayloadItem(MemMgmtContext *ctx,
                               IrNode *bb,
                               IrNode **prevRef,
                               IrNode *stmt,
                               IrNode **itemRef,
                               IrNode **afterStmtRef,
                               BitSet *currMay,
                               BitSet *currMust);


// Splices `node` in front of `stmt`; `*prevRef` tracks the node before `stmt`
static void insertBeforeStmt(IrNode *bb, IrNode **prevRef, IrNode *stmt, IrNode *node)
{
    node->next = stmt;
    if (*prevRef)
        (*prevRef)->next = node;
    else
        bb->basicBlock.first = node;
    *prevRef = node;
}

static void injectCallArgumentCopies(MemMgmtContext *ctx,
                                     IrNode *bb,
                                     IrNode **prevRef,
                                     IrNode *callStmt,
                                     IrNode *call,
                                     IrNode **afterStmtRef,
                                     BitSet *currMay,
                                     BitSet *currMust)
{
    if (call == NULL || call->tag != irCall || call->call.callee == NULL)
        return;

    const IrType *calleeType = call->call.callee->type;
    if (calleeType && calleeType->tag == irTypeFunc) {
        IrNode **argRef = &call->call.args;
        u64 i = 0;
        for (; *argRef && i < calleeType->func.count; i++) {
            const IrType *paramType = calleeType->func.params[i];
            csAssert(paramType != NULL, "Compiler error: paramType is NULL in call argument processing");
            if (paramType->category == irTypeCatReference) {
                // References don't own: drop the move, keep any outer casts
                IrNode **mvRef = moveSlot(argRef);
                if (mvRef) {
                    IrNode *src = (*mvRef)->move.src;
                    src->next = (*mvRef)->next;
                    *mvRef = src;
                }
                argRef = &(*argRef)->next;
                continue;
            }

            processPayloadItem(ctx, bb, prevRef, callStmt, argRef, afterStmtRef, currMay, currMust);
            argRef = &(*argRef)->next;
        }

        // Variadic arguments have no declared parameter: manage them by their own
        // type (values are copied/moved, references are passed as-is).
        for (; *argRef; argRef = &(*argRef)->next) {
            const IrType *argType = (*argRef)->type;
            if (argType && argType->category == irTypeCatReference) {
                IrNode **mvRef = moveSlot(argRef);
                if (mvRef) {
                    IrNode *src = (*mvRef)->move.src;
                    src->next = (*mvRef)->next;
                    *mvRef = src;
                }
                continue;
            }
            processPayloadItem(ctx, bb, prevRef, callStmt, argRef, afterStmtRef, currMay, currMust);
        }
    }
}

static void processPayloadItem(MemMgmtContext *ctx,
                               IrNode *bb,
                               IrNode **prevRef,
                               IrNode *stmt,
                               IrNode **itemRef,
                               IrNode **afterStmtRef,
                               BitSet *currMay,
                               BitSet *currMust)
{
    IrNode *val = *itemRef;
    if (val == NULL)
        return;

    // Moves take effect when the operand is evaluated, i.e. before `stmt`.
    // Clearing after `stmt` is wrong once the parent can resume mid-call (async).
    (void)afterStmtRef;
    IrNode **mvRef = moveSlot(itemRef);
    if (mvRef) {
        IrNode *mv = *mvRef;
        IrNode *src = mv->move.src;
        IrNode *rhsDecl = findMovedVarDecl(src);
        bool isPtrLike = src && src->type &&
                         (src->type->tag == irTypePointer ||
                          src->type->category == irTypeCatClass);
        bool isMemberSrc =
            src && (src->tag == irMember || src->tag == irIndex) &&
            (isPtrLike || isIrTypeDestructible(src->type));

        if (rhsDecl) {
            src->next = mv->next;
            *mvRef = src;
            TrackedVar *rhsVar = getTrackedVar(ctx, rhsDecl);
            if (rhsVar && !rhsVar->isTemporary) {
                bitSetReset(currMay, rhsVar->varIndex);
                bitSetReset(currMust, rhsVar->varIndex);
                if (rhsVar->needsDropFlag) {
                    // The operand still reads the variable; only ownership moves
                    IrNode *clearFlag = makeDropFlagAssign(ctx, &stmt->loc, rhsVar, false);
                    if (clearFlag)
                        insertBeforeStmt(bb, prevRef, stmt, clearFlag);
                }
            }
        }
        else if (isMemberSrc) {
            // __mv = src; src = null; then pass __mv
            cstring tempName = makeStringf(ctx->strings, "__mv%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool, &src->loc, flgTemporary, tempName, tempName, src->type, ctx->currentScopeId);
            TrackedVar *tv = addTrackedVar(ctx, tempVar, tempName, ctx->currentScopeId, src->type);
            if (tv)
                tv->state = irVarMoved;

            IrNode *capture = makeIrAssign(
                ctx->pool, &src->loc, makeIrVarRef(ctx->pool, &src->loc, tempVar), src);
            capture->flags |= flgUnmanaged;

            IrNode *srcClone = cloneIrNode(ctx->pool, src);
            IrNode *nullify =
                isPtrLike
                    ? makeIrAssign(ctx->pool, &stmt->loc, srcClone, makeIrNullConst(ctx->pool, &stmt->loc, src->type))
                    : makeIrZeromem(ctx->pool, &stmt->loc, srcClone);
            nullify->flags |= flgUnmanaged;

            insertBeforeStmt(bb, prevRef, stmt, tempVar);
            insertBeforeStmt(bb, prevRef, stmt, capture);
            insertBeforeStmt(bb, prevRef, stmt, nullify);

            IrNode *tempRef = makeIrVarRef(ctx->pool, &src->loc, tempVar);
            tempRef->next = mv->next;
            *mvRef = tempRef;
        }
        else {
            src->next = mv->next;
            *mvRef = src;
        }
        return;
    }

    if (isIrTypeDestructible(val->type)) {
        IrNode *inner = unwrapCasts(val);
        bool needsCopy = false;
        if (inner && inner->tag == irVarRef) {
            needsCopy = inner->varRef.target && !hasFlag(inner->varRef.target, Temporary) && !hasFlag(inner->varRef.target, Returned);
        }
        else if (inner && (inner->tag == irMember || inner->tag == irIndex)) {
            needsCopy = !isUnwrappedCallResult(inner);
        }

        if (needsCopy) {
            cstring tempName = makeStringf(ctx->strings, "__copy%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool, &inner->loc, flgTemporary, tempName, tempName, inner->type, ctx->currentScopeId);
            TrackedVar *tv = addTrackedVar(ctx, tempVar, tempName, ctx->currentScopeId, inner->type);
            if (tv)
                tv->state = irVarMoved;

            IrNode *dstRef = makeIrVarRef(ctx->pool, &inner->loc, tempVar);
            IrNode *copyStmt = makeIrCopy(ctx->pool, &inner->loc, inner, dstRef);

            tempVar->next = copyStmt;
            copyStmt->next = stmt;
            if (*prevRef)
                (*prevRef)->next = tempVar;
            else
                bb->basicBlock.first = tempVar;
            *prevRef = copyStmt;

            IrNode *finalArg = rebuildCastChain(ctx->pool, val, dstRef);
            finalArg->next = val->next;
            *itemRef = finalArg;
        }
    }
}

static void injectCompositePayloadCopies(MemMgmtContext *ctx,
                                         IrNode *bb,
                                         IrNode **prevRef,
                                         IrNode *stmt,
                                         IrNode *rhs,
                                         IrNode **afterStmtRef,
                                         BitSet *currMay,
                                         BitSet *currMust)
{
    if (rhs == NULL)
        return;

    if (rhs->tag == irUnionValue) {
        processPayloadItem(ctx, bb, prevRef, stmt, &rhs->unionValue.value, afterStmtRef, currMay, currMust);
    }
    else if (rhs->tag == irOptionalValue) {
        processPayloadItem(ctx, bb, prevRef, stmt, &rhs->optionalValue.value, afterStmtRef, currMay, currMust);
    }
    else if (rhs->tag == irStructConst) {
        for (IrNode **fld = &rhs->structConst.fields; *fld; fld = &(*fld)->next) {
            IrNode **item = ((*fld)->tag == irFieldConst) ? &(*fld)->fieldConst.value : fld;
            processPayloadItem(ctx, bb, prevRef, stmt, item, afterStmtRef, currMay, currMust);
        }
    }
    else if (rhs->tag == irArrayConst) {
        for (IrNode **elem = &rhs->arrayConst.elements; *elem; elem = &(*elem)->next) {
            processPayloadItem(ctx, bb, prevRef, stmt, elem, afterStmtRef, currMay, currMust);
        }
    }
    else if (rhs->tag == irClosureValue) {
        if (rhs->closureValue.env) {
            processPayloadItem(ctx, bb, prevRef, stmt, &rhs->closureValue.env, afterStmtRef, currMay, currMust);
        }
    }
}

static bool shouldPromoteAssignToCopy(const IrNode *lhsDecl, const IrNode *lhs, const IrNode *rhs)
{
    if (!isIrVarDestructible(lhsDecl) && !isIrTypeDestructible(lhs->type))
        return false;
    IrNode *rhsDecl = findVarDeclInNode(rhs);
    if (rhsDecl && hasFlag(rhsDecl, Temporary))
        return false;
    if (rhs->tag == irVarRef) {
        return rhs->varRef.target && !hasFlag(rhs->varRef.target, Temporary);
    }
    return rhs->tag == irMember || rhs->tag == irIndex;
}

static void elaborateDropsAndReassignments(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count)
{
    u64 varCount = ctx->trackedVarList.size;
    BitSet currMay = makeBitSet(ctx->localPool, varCount);
    BitSet currMust = makeBitSet(ctx->localPool, varCount);

    for (u64 b = 0; b < count; b++) {
        CfgBlockInfo *block = blocks[b];
        IrNode *bb = block->bb;
        bitSetCopy(&currMay, &block->mayIn);
        bitSetCopy(&currMust, &block->mustIn);

        IrNode *prev = NULL;
        for (IrNode *stmt = bb->basicBlock.first; stmt; ) {
            IrNode *next = stmt->next;

            if (stmt->tag == irAssign && !hasFlag(stmt, Unmanaged)) {
                if (stmt->assign.lhs->tag == irIndex && isIrTypeDestructible(stmt->assign.lhs->type)) {
                    IrNode *base = stmt->assign.lhs->index.target;
                    // Do not auto-drop if indexing into a raw pointer buffer (^T)
                    if (base && base->type && !isIrPointerType(base->type)) {
                        IrNode *dropStmt = makeIrDrop(ctx->pool, &stmt->loc, stmt->assign.lhs);
                        dropStmt->next = stmt;
                        if (prev)
                            prev->next = dropStmt;
                        else
                            bb->basicBlock.first = dropStmt;
                        prev = dropStmt;
                    }
                }
                else if (stmt->assign.lhs->tag == irMember && isIrTypeDestructible(stmt->assign.lhs->type)) {
                    IrNode *baseDecl = findVarDeclInNode(stmt->assign.lhs);
                    bool shouldDrop = true;
                    if (baseDecl && baseDecl->tag == irVarDecl) {
                        TrackedVar *baseVar = getTrackedVar(ctx, baseDecl);
                        shouldDrop = (baseVar && !baseVar->isTemporary && bitSetTest(&currMust, baseVar->varIndex));
                    }
                    if (shouldDrop) {
                        IrNode *dropStmt = makeIrDrop(ctx->pool, &stmt->loc, stmt->assign.lhs);
                        dropStmt->next = stmt;
                        if (prev)
                            prev->next = dropStmt;
                        else
                            bb->basicBlock.first = dropStmt;
                        prev = dropStmt;
                    }
                }

                IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
                TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
                IrNode *rhs = stmt->assign.rhs;

                if (lhsVar && !lhsVar->isTemporary) {
                    if (stmt->assign.lhs->tag == irVarRef && bitSetTest(&currMay, lhsVar->varIndex)) {
                        if (lhsVar->needsDropFlag && !bitSetTest(&currMust, lhsVar->varIndex)) {
                            IrNode *flagAccess = makeDropFlagAccess(ctx, &stmt->loc, lhsVar);
                            IrNode *dropStmt = makeIrDropIf(ctx->pool, &stmt->loc, stmt->assign.lhs, flagAccess);
                            dropStmt->next = stmt;
                            if (prev)
                                prev->next = dropStmt;
                            else
                                bb->basicBlock.first = dropStmt;
                            prev = dropStmt;
                        }
                        else if (bitSetTest(&currMust, lhsVar->varIndex)) {
                            IrNode *dropStmt = makeIrDrop(ctx->pool, &stmt->loc, stmt->assign.lhs);
                            dropStmt->next = stmt;
                            if (prev)
                                prev->next = dropStmt;
                            else
                                bb->basicBlock.first = dropStmt;
                            prev = dropStmt;
                        }
                    }

                    if (rhs && (rhs->tag == irNullConst || (rhs->tag == irOptionalValue && !rhs->optionalValue.hasValue))) {
                        if (stmt->assign.lhs->tag == irVarRef) {
                            bitSetReset(&currMay, lhsVar->varIndex);
                            bitSetReset(&currMust, lhsVar->varIndex);
                            if (lhsVar->needsDropFlag) {
                                IrNode *clearFlag = makeDropFlagAssign(ctx, &stmt->loc, lhsVar, false);
                                if (clearFlag) {
                                    clearFlag->next = next;
                                    stmt->next = clearFlag;
                                    next = clearFlag;
                                }
                            }
                        }
                    }
                    else {
                        bitSetSet(&currMay, lhsVar->varIndex);
                        bitSetSet(&currMust, lhsVar->varIndex);

                        if (stmt->assign.lhs->tag == irVarRef && lhsVar->needsDropFlag) {
                            IrNode *setFlag = makeDropFlagAssign(ctx, &stmt->loc, lhsVar, true);
                            if (setFlag) {
                                setFlag->next = next;
                                stmt->next = setFlag;
                                next = setFlag;
                            }
                        }
                    }
                }
                else if (lhsVar && lhsVar->isEphemeral) {
                    if (isIrTypeDestructible(lhsVar->type)) {
                        IrNode *dropTemp = makeIrDrop(ctx->pool, &stmt->loc, stmt->assign.lhs);
                        dropTemp->next = next;
                        stmt->next = dropTemp;
                        next = dropTemp;
                    }
                }

                IrNode **rhsMove = rhs ? moveSlot(&stmt->assign.rhs) : NULL;
                if (rhs) {
                    if (rhsMove) {
                        IrNode *src = (*rhsMove)->move.src;
                        *rhsMove = src;
                        IrNode *rhsDecl = findMovedVarDecl(src);
                        if (rhsDecl) {
                            TrackedVar *rhsVar = getTrackedVar(ctx, rhsDecl);
                            if (rhsVar && !rhsVar->isTemporary) {
                                bitSetReset(&currMay, rhsVar->varIndex);
                                bitSetReset(&currMust, rhsVar->varIndex);
                                if (rhsVar->needsDropFlag) {
                                    IrNode *clearFlag = makeDropFlagAssign(ctx, &stmt->loc, rhsVar, false);
                                    if (clearFlag) {
                                        clearFlag->next = next;
                                        stmt->next = clearFlag;
                                        next = clearFlag;
                                    }
                                }
                            }
                        }
                        else if (src && (src->tag == irMember || src->tag == irIndex)) {
                            if (src->type && (src->type->tag == irTypePointer || src->type->category == irTypeCatClass)) {
                                IrNode *nullify = makeIrAssign(ctx->pool, &stmt->loc, src, makeIrNullConst(ctx->pool, &stmt->loc, src->type));
                                nullify->flags |= flgUnmanaged;
                                nullify->next = next;
                                stmt->next = nullify;
                                next = nullify;
                            }
                            else if (isIrTypeDestructible(src->type)) {
                                IrNode *nullify = makeIrZeromem(ctx->pool, &stmt->loc, src);
                                nullify->flags |= flgUnmanaged;
                                nullify->next = next;
                                stmt->next = nullify;
                                next = nullify;
                            }
                        }
                    }
                    else if (rhs->tag == irCall) {
                        injectCallArgumentCopies(ctx, bb, &prev, stmt, rhs, &next, &currMay, &currMust);
                    }
                    else if (rhs->tag == irUnionValue || rhs->tag == irOptionalValue || rhs->tag == irStructConst || rhs->tag == irArrayConst || rhs->tag == irClosureValue) {
                        injectCompositePayloadCopies(ctx, bb, &prev, stmt, rhs, &next, &currMay, &currMust);
                    }
                    else if (shouldPromoteAssignToCopy(lhsDecl, stmt->assign.lhs, rhs)) {
                        IrNode *lhs = stmt->assign.lhs;
                        stmt->tag = irCopy;
                        stmt->copy.src = rhs;
                        stmt->copy.dst = lhs;
                    }
                }
            }
            else if (stmt->tag == irDrop && !hasFlag(stmt, Unmanaged)) {
                IrNode *target = stmt->drop.target;
                if (target && target->tag == irMember) {
                    IrNode *zero = NULL;
                    if (target->type && (target->type->tag == irTypePointer || target->type->category == irTypeCatClass)) {
                        zero = makeIrAssign(ctx->pool, &stmt->loc, target, makeIrNullConst(ctx->pool, &stmt->loc, target->type));
                    }
                    else if (isIrTypeDestructible(target->type)) {
                        zero = makeIrZeromem(ctx->pool, &stmt->loc, target);
                    }
                    if (zero) {
                        zero->flags |= flgUnmanaged;
                        zero->next = next;
                        stmt->next = zero;
                        next = zero;
                    }
                }
            }
            else if (stmt->tag == irCall) {
                injectCallArgumentCopies(ctx, bb, &prev, stmt, stmt, &next, &currMay, &currMust);
            }
            else if (stmt->tag == irScopeExit) {
                u32 exitingScope = stmt->scopeExit.scopeId;
                for (i64 i = (i64)ctx->trackedVarList.size - 1; i >= 0; i--) {
                    TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[i];
                    if (var == NULL || var->scopeId != exitingScope || !isIrVarDestructible(var->decl) || var->isTemporary)
                        continue;

                    IrNode *targetRef = makeIrVarRef(ctx->pool, &stmt->loc, var->decl);
                    if (var->needsDropFlag && bitSetTest(&currMay, var->varIndex) && !bitSetTest(&currMust, var->varIndex)) {
                        IrNode *flagAccess = makeDropFlagAccess(ctx, &stmt->loc, var);
                        IrNode *dropStmt = makeIrDropIf(ctx->pool, &stmt->loc, targetRef, flagAccess);
                        dropStmt->next = stmt;
                        if (prev)
                            prev->next = dropStmt;
                        else
                            bb->basicBlock.first = dropStmt;
                        prev = dropStmt;

                        IrNode *clearFlag = makeDropFlagAssign(ctx, &stmt->loc, var, false);
                        if (clearFlag) {
                            clearFlag->next = stmt;
                            dropStmt->next = clearFlag;
                            prev = clearFlag;
                        }

                        bitSetReset(&currMay, var->varIndex);
                        bitSetReset(&currMust, var->varIndex);
                    }
                    else if (bitSetTest(&currMust, var->varIndex)) {
                        IrNode *dropStmt = makeIrDrop(ctx->pool, &stmt->loc, targetRef);
                        dropStmt->next = stmt;
                        if (prev)
                            prev->next = dropStmt;
                        else
                            bb->basicBlock.first = dropStmt;
                        prev = dropStmt;

                        if (var->needsDropFlag) {
                            IrNode *clearFlag = makeDropFlagAssign(ctx, &stmt->loc, var, false);
                            if (clearFlag) {
                                clearFlag->next = stmt;
                                dropStmt->next = clearFlag;
                                prev = clearFlag;
                            }
                        }

                        bitSetReset(&currMay, var->varIndex);
                        bitSetReset(&currMust, var->varIndex);
                    }
                }
            }

            prev = stmt;
            stmt = next;
        }

        IrNode *lastStmt = NULL;
        for (IrNode *s = bb->basicBlock.first; s; s = s->next)
            lastStmt = s;
        bb->basicBlock.last = lastStmt;
    }
}

static void processFunctionMemory(MemMgmtContext *ctx, IrNode *func)
{
    MemPool localPool = newMemPool();
    ctx->localPool = &localPool;
    ctx->trackedVars = newHashTable(sizeof(TrackedVar *), ctx->localPool);
    ctx->trackedVarList = newDynArray(sizeof(TrackedVar *));
    ctx->dropFlagVars = newDynArray(sizeof(TrackedVar *));
    ctx->dropFlagsDecl = NULL;

    collectScopeVariables(ctx, func);

    u64 blockCount = 0;
    CfgBlockInfo **blocks = buildFunctionCfg(ctx, func, &blockCount);

    if (blockCount > 0) {
        if (ctx->trackedVarList.size > 0) {
            runDataflowAnalysis(ctx, blocks, blockCount, ctx->trackedVarList.size);
            identifyDropFlagVariables(ctx, blocks, blockCount);
            synthesizeDropFlags(ctx, func);
        }
        elaborateDropsAndReassignments(ctx, blocks, blockCount);
    }

    freeHashTable(&ctx->trackedVars);
    freeDynArray(&ctx->trackedVarList);
    freeDynArray(&ctx->dropFlagVars);
    freeMemPool(&localPool);
    ctx->localPool = NULL;
}

IrModule *runMemoryManagement(CompilerDriver *driver, IrModule *module)
{
    if (module == NULL)
        return NULL;

    MemMgmtContext ctx = {
        .driver = driver,
        .pool = driver->pool,
        .strings = driver->strings,
        .types = module->module.types,
        .module = module,
        .currentFunc = NULL,
        .tempCounter = 0
    };

    for (IrNode *decl = module->module.functions; decl; decl = decl->next) {
        if (decl->tag == irFunction && decl->function.firstBlock) {
            ctx.currentFunc = decl;
            processFunctionMemory(&ctx, decl);
        }
    }

    return module;
}
