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

static TrackedVar *getTrackedVar(MemMgmtContext *ctx, const IrNode *decl)
{
    if (decl == NULL)
        return NULL;
    return findInHashTable(&ctx->trackedVars,
                           &(TrackedVar){.decl = (IrNode *)decl},
                           hashPtr(hashInit(), decl),
                           sizeof(TrackedVar),
                           comparePointers);
}

static TrackedVar *addTrackedVar(MemMgmtContext *ctx, IrNode *decl, cstring name, u32 scopeId, const IrType *type)
{
    u32 index = (u32)ctx->trackedVarList.size;
    TrackedVar var = {
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
                      sizeof(TrackedVar),
                      comparePointers);

    TrackedVar *stored = getTrackedVar(ctx, decl);
    pushOnDynArray(&ctx->trackedVarList, &stored);
    return stored;
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

    CfgBlockInfo **blocks = allocFromMemPool(ctx->pool, sizeof(CfgBlockInfo *) * count);
    u64 idx = 0;
    u64 numVars = ctx->trackedVarList.size;

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        CfgBlockInfo *info = allocFromMemPool(ctx->pool, sizeof(CfgBlockInfo));
        info->bb = bb;
        info->gen = makeBitSet(ctx->pool, numVars);
        info->kill = makeBitSet(ctx->pool, numVars);
        info->mayIn = makeBitSet(ctx->pool, numVars);
        info->mayOut = makeBitSet(ctx->pool, numVars);
        info->mustIn = makeBitSet(ctx->pool, numVars);
        info->mustOut = makeBitSet(ctx->pool, numVars);
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

static void computeIntraBlockGenKill(MemMgmtContext *ctx, CfgBlockInfo *block)
{
    for (IrNode *stmt = block->bb->basicBlock.first; stmt; stmt = stmt->next) {
        if (stmt->tag == irAssign) {
            IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
            TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
            if (lhsVar && !lhsVar->isTemporary) {
                bitSetSet(&block->gen, lhsVar->varIndex);
                bitSetReset(&block->kill, lhsVar->varIndex);
            }

            IrNode *rhs = stmt->assign.rhs;
            if (rhs && rhs->tag == irMove) {
                IrNode *rhsDecl = findVarDeclInNode(rhs->move.src);
                TrackedVar *rhsVar = getTrackedVar(ctx, rhsDecl);
                if (rhsVar && !rhsVar->isTemporary) {
                    bitSetSet(&block->kill, rhsVar->varIndex);
                    bitSetReset(&block->gen, rhsVar->varIndex);
                }
            }
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

    BitSet tempMay = makeBitSet(ctx->pool, varCount);
    BitSet tempMust = makeBitSet(ctx->pool, varCount);
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

static void identifyDropFlagVariables(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count)
{
    u64 varCount = ctx->trackedVarList.size;
    BitSet currMay = makeBitSet(ctx->pool, varCount);
    BitSet currMust = makeBitSet(ctx->pool, varCount);

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
                    if (bitSetTest(&currMay, lhsVar->varIndex) && !bitSetTest(&currMust, lhsVar->varIndex)) {
                        if (!lhsVar->needsDropFlag) {
                            lhsVar->needsDropFlag = true;
                            lhsVar->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                            pushOnDynArray(&ctx->dropFlagVars, &lhsVar);
                        }
                    }
                    bitSetSet(&currMay, lhsVar->varIndex);
                    bitSetSet(&currMust, lhsVar->varIndex);
                }

                IrNode *rhs = stmt->assign.rhs;
                if (rhs && rhs->tag == irMove) {
                    IrNode *rhsDecl = findVarDeclInNode(rhs->move.src);
                    TrackedVar *rhsVar = getTrackedVar(ctx, rhsDecl);
                    if (rhsVar && !rhsVar->isTemporary) {
                        bitSetReset(&currMay, rhsVar->varIndex);
                        bitSetReset(&currMust, rhsVar->varIndex);
                    }
                }
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

    const IrType *dropFlagsType = makeIrStructType(ctx->types, flgNone, "__drop_flags_t", "__drop_flags_t", fields, flagCount);
    IrNode *dropFlagsDecl = makeIrVarDecl(ctx->pool, &func->loc, flgNone, "__drop_flags", "__drop_flags", dropFlagsType, 0);
    ctx->dropFlagsDecl = dropFlagsDecl;

    IrNode *entryBB = func->function.firstBlock;
    if (entryBB) {
        dropFlagsDecl->next = entryBB->basicBlock.first;
        entryBB->basicBlock.first = dropFlagsDecl;

        IrNode *zeromem = makeIrZeromem(ctx->pool, &func->loc, makeIrVarRef(ctx->pool, &func->loc, dropFlagsDecl));
        zeromem->next = dropFlagsDecl->next;
        dropFlagsDecl->next = zeromem;
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

static void injectCallArgumentCopies(MemMgmtContext *ctx, IrNode *bb, IrNode **prevRef, IrNode *callStmt, IrNode *call)
{
    if (call == NULL || call->tag != irCall || call->call.callee == NULL)
        return;

    const IrType *calleeType = call->call.callee->type;
    if (calleeType && calleeType->tag == irTypeFunc) {
        IrNode *prevArg = NULL;
        IrNode *arg = call->call.args;
        for (u64 i = 0; arg && i < calleeType->func.count; i++) {
            const IrType *paramType = calleeType->func.params[i];
            if (paramType && paramType->category != irTypeCatReference && isIrTypeDestructible(paramType) && arg->tag == irVarRef && !hasFlag(arg->varRef.target, Returned) && isIrVarDestructible(arg->varRef.target)) {
                TrackedVar *argVar = getTrackedVar(ctx, arg->varRef.target);
                if (argVar && !argVar->isTemporary) {
                    cstring tempName = makeStringf(ctx->strings, "__copy%u", ctx->tempCounter++);
                    IrNode *tempVar = makeIrVarDecl(ctx->pool, &arg->loc, flgTemporary | (arg->varRef.target->flags & flgReferenceMembers), tempName, tempName, arg->type, ctx->currentScopeId);
                    TrackedVar *tv = addTrackedVar(ctx, tempVar, tempName, ctx->currentScopeId, arg->type);
                    if (tv)
                        tv->state = irVarMoved;

                    IrNode *dstRef = makeIrVarRef(ctx->pool, &arg->loc, tempVar);
                    IrNode *copyStmt = makeIrCopy(ctx->pool, &arg->loc, arg, dstRef);

                    tempVar->next = copyStmt;
                    copyStmt->next = callStmt;
                    if (*prevRef)
                        (*prevRef)->next = tempVar;
                    else
                        bb->basicBlock.first = tempVar;
                    *prevRef = copyStmt;

                    dstRef->next = arg->next;
                    if (prevArg)
                        prevArg->next = dstRef;
                    else
                        call->call.args = dstRef;
                    arg = dstRef;
                }
            }
            prevArg = arg;
            arg = arg->next;
        }
    }
}

static bool shouldPromoteAssignToCopy(const IrNode *lhsDecl, const IrNode *lhs, const IrNode *rhs)
{
    if (!isIrVarDestructible(lhsDecl) && !isIrTypeDestructible(lhs->type))
        return false;
    if (rhs->tag == irVarRef) {
        return rhs->varRef.target && !hasFlag(rhs->varRef.target, Temporary);
    }
    return rhs->tag == irMember || rhs->tag == irIndex;
}

static void elaborateDropsAndReassignments(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count)
{
    u64 varCount = ctx->trackedVarList.size;
    BitSet currMay = makeBitSet(ctx->pool, varCount);
    BitSet currMust = makeBitSet(ctx->pool, varCount);

    for (u64 b = 0; b < count; b++) {
        CfgBlockInfo *block = blocks[b];
        IrNode *bb = block->bb;
        bitSetCopy(&currMay, &block->mayIn);
        bitSetCopy(&currMust, &block->mustIn);

        IrNode *prev = NULL;
        for (IrNode *stmt = bb->basicBlock.first; stmt; ) {
            IrNode *next = stmt->next;

            if (stmt->tag == irAssign) {
                IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
                TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
                IrNode *rhs = stmt->assign.rhs;

                if (lhsVar && !lhsVar->isTemporary) {
                    if (bitSetTest(&currMay, lhsVar->varIndex)) {
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

                    bitSetSet(&currMay, lhsVar->varIndex);
                    bitSetSet(&currMust, lhsVar->varIndex);

                    if (lhsVar->needsDropFlag) {
                        IrNode *setFlag = makeDropFlagAssign(ctx, &stmt->loc, lhsVar, true);
                        if (setFlag) {
                            setFlag->next = next;
                            stmt->next = setFlag;
                            next = setFlag;
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

                if (rhs) {
                    if (rhs->tag == irMove) {
                        IrNode *rhsDecl = findVarDeclInNode(rhs->move.src);
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
                    else if (rhs->tag == irCall) {
                        injectCallArgumentCopies(ctx, bb, &prev, stmt, rhs);
                    }
                    else if (shouldPromoteAssignToCopy(lhsDecl, stmt->assign.lhs, rhs)) {
                        IrNode *lhs = stmt->assign.lhs;
                        stmt->tag = irCopy;
                        stmt->copy.src = rhs;
                        stmt->copy.dst = lhs;
                    }
                }
            }
            else if (stmt->tag == irCall) {
                injectCallArgumentCopies(ctx, bb, &prev, stmt, stmt);
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
    ctx->trackedVars = newHashTable(sizeof(TrackedVar), ctx->pool);
    ctx->trackedVarList = newDynArray(sizeof(TrackedVar *));
    ctx->dropFlagVars = newDynArray(sizeof(TrackedVar *));
    ctx->dropFlagsDecl = NULL;

    collectScopeVariables(ctx, func);

    u64 blockCount = 0;
    CfgBlockInfo **blocks = buildFunctionCfg(ctx, func, &blockCount);

    if (blockCount > 0 && ctx->trackedVarList.size > 0) {
        runDataflowAnalysis(ctx, blocks, blockCount, ctx->trackedVarList.size);
        identifyDropFlagVariables(ctx, blocks, blockCount);
        synthesizeDropFlags(ctx, func);
        elaborateDropsAndReassignments(ctx, blocks, blockCount);
    }

    freeHashTable(&ctx->trackedVars);
    freeDynArray(&ctx->trackedVarList);
    freeDynArray(&ctx->dropFlagVars);
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
