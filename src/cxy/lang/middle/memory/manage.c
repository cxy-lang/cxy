#include "memory.h"
#include "core/hash.h"
#include "lang/frontend/flag.h"

bool isIrTypeDestructible(const IrType *type)
{
    if (type == NULL)
        return false;

    if (type->category == irTypeCatClass || type->category == irTypeCatStruct ||
        type->category == irTypeCatTuple || type->category == irTypeCatUnion) {
        return true;
    }

    if (type->tag == irTypeArray) {
        return isIrTypeDestructible(type->array.elem);
    }
    return false;
}

bool isIrTypeCopyable(const IrType *type)
{
    return isIrTypeDestructible(type);
}

static inline bool isIrVarDestructible(const IrNode *decl)
{
    if (decl == NULL || hasFlag(decl, Returned))
        return false;
    return hasFlag(decl, ReferenceMembers);
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
    TrackedVar var = {
        .decl = decl,
        .name = name,
        .scopeId = scopeId,
        .dropFlagIndex = 0,
        .needsDropFlag = false,
        .isTemporary = (name && (strncmp(name, "__t", 3) == 0 || strncmp(name, "__opt", 5) == 0 || strncmp(name, "__copy", 6) == 0)),
        .type = type,
        .state = irVarUninitialized
    };

    insertInHashTable(&ctx->trackedVars,
                      &var,
                      hashPtr(hashInit(), decl),
                      sizeof(TrackedVar),
                      comparePointers);

    return getTrackedVar(ctx, decl);
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
    return NULL;
}

static void collectUsedTemporaries(MemMgmtContext *ctx, const IrNode *expr, DynArray *usedTemps)
{
    if (expr == NULL)
        return;

    if (expr->tag == irVarRef) {
        TrackedVar *var = getTrackedVar(ctx, expr->varRef.target);
        if (var && var->isTemporary && var->state == irVarAssigned) {
            pushOnDynArray(usedTemps, &var);
        }
        return;
    }

    if (expr->tag == irCall) {
        for (const IrNode *arg = expr->call.args; arg; arg = arg->next) {
            collectUsedTemporaries(ctx, arg, usedTemps);
        }
        return;
    }

    if (expr->tag == irMember) {
        collectUsedTemporaries(ctx, expr->member.target, usedTemps);
        return;
    }

    if (expr->tag == irIndex) {
        collectUsedTemporaries(ctx, expr->index.target, usedTemps);
        collectUsedTemporaries(ctx, expr->index.index, usedTemps);
        return;
    }

    if (expr->tag == irBinary) {
        collectUsedTemporaries(ctx, expr->binary.lhs, usedTemps);
        collectUsedTemporaries(ctx, expr->binary.rhs, usedTemps);
        return;
    }

    if (expr->tag == irUnary) {
        collectUsedTemporaries(ctx, expr->unary.operand, usedTemps);
        return;
    }
}

static void scanForConditionalAssignments(MemMgmtContext *ctx, IrNode *func)
{
    IrNode *entryBB = func->function.firstBlock;
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock || bb == entryBB)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irAssign) {
                IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
                TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
                if (lhsVar && !lhsVar->isTemporary && lhsVar->decl && lhsVar->decl->tag == irVarDecl) {
                    if (!lhsVar->needsDropFlag) {
                        lhsVar->needsDropFlag = true;
                        lhsVar->state = irVarMaybeAssigned;
                        lhsVar->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                        pushOnDynArray(&ctx->dropFlagVars, &lhsVar);
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

    const IrType *dropFlagsType = makeIrStructType(ctx->types, "__drop_flags_t", "__drop_flags_t", fields, flagCount);
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
            if (isIrTypeDestructible(paramType)) {
                if (arg->tag == irVarRef && !hasFlag(arg->varRef.target, Returned)) {
                    TrackedVar *argVar = getTrackedVar(ctx, arg->varRef.target);
                    if (argVar && !argVar->isTemporary) {
                        cstring tempName = makeStringf(ctx->strings, "__copy%u", ctx->tempCounter++);
                        IrNode *tempVar = makeIrVarDecl(ctx->pool, &arg->loc, flgNone, tempName, tempName, arg->type, ctx->currentScopeId);
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
            }
            prevArg = arg;
            arg = arg->next;
        }
    }
}

static void processFunctionMemory(MemMgmtContext *ctx, IrNode *func)
{
    ctx->trackedVars = newHashTable(sizeof(TrackedVar), ctx->pool);
    ctx->dropFlagVars = newDynArray(sizeof(TrackedVar *));
    ctx->dropFlagsDecl = NULL;

    collectScopeVariables(ctx, func);
    scanForConditionalAssignments(ctx, func);
    synthesizeDropFlags(ctx, func);

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;

        IrNode *prev = NULL;
        for (IrNode *stmt = bb->basicBlock.first; stmt; ) {
            IrNode *next = stmt->next;

            if (stmt->tag == irAssign) {
                IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
                TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
                IrNode *rhs = stmt->assign.rhs;

                if (lhsVar && !lhsVar->isTemporary) {
                    if (lhsVar->state == irVarAssigned) {
                        if (lhsVar->needsDropFlag) {
                            IrNode *flagAccess = makeDropFlagAccess(ctx, &stmt->loc, lhsVar);
                            IrNode *dropStmt = makeIrDropIf(ctx->pool, &stmt->loc, stmt->assign.lhs, flagAccess);
                            dropStmt->next = stmt;
                            if (prev)
                                prev->next = dropStmt;
                            else
                                bb->basicBlock.first = dropStmt;
                            prev = dropStmt;
                        }
                        else {
                            IrNode *dropStmt = makeIrDrop(ctx->pool, &stmt->loc, stmt->assign.lhs);
                            dropStmt->next = stmt;
                            if (prev)
                                prev->next = dropStmt;
                            else
                                bb->basicBlock.first = dropStmt;
                            prev = dropStmt;
                        }
                    }

                    lhsVar->state = irVarAssigned;

                    if (lhsVar->needsDropFlag) {
                        IrNode *setFlag = makeDropFlagAssign(ctx, &stmt->loc, lhsVar, true);
                        if (setFlag) {
                            setFlag->next = next;
                            stmt->next = setFlag;
                            next = setFlag;
                        }
                    }
                }
                else if (lhsVar && lhsVar->isTemporary) {
                    lhsVar->state = irVarAssigned;
                    if (rhs && rhs->tag == irCall) {
                        IrNode *dropTemp = makeIrDrop(ctx->pool, &stmt->loc, stmt->assign.lhs);
                        dropTemp->next = next;
                        stmt->next = dropTemp;
                        next = dropTemp;
                        lhsVar->state = irVarUninitialized;
                    }
                }

                if (rhs) {
                    if (rhs->tag == irMove) {
                        IrNode *rhsDecl = findVarDeclInNode(rhs->move.src);
                        TrackedVar *rhsVar = getTrackedVar(ctx, rhsDecl);
                        if (rhsVar) {
                            rhsVar->state = irVarMoved;
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
                    else if (isIrVarDestructible(lhsDecl) && (rhs->tag == irVarRef || rhs->tag == irMember || rhs->tag == irIndex)) {
                        IrNode *lhs = stmt->assign.lhs;
                        stmt->tag = irCopy;
                        stmt->copy.src = rhs;
                        stmt->copy.dst = lhs;
                    }
                }
            }
            else if (stmt->tag == irCall) {
                injectCallArgumentCopies(ctx, bb, &prev, stmt, stmt);

                DynArray usedTemps = newDynArray(sizeof(TrackedVar *));
                collectUsedTemporaries(ctx, stmt, &usedTemps);
                for (u64 i = 0; i < usedTemps.size; i++) {
                    TrackedVar *tempVar = ((TrackedVar **)usedTemps.elems)[i];
                    IrNode *tempRef = makeIrVarRef(ctx->pool, &stmt->loc, tempVar->decl);
                    IrNode *dropTemp = makeIrDrop(ctx->pool, &stmt->loc, tempRef);
                    dropTemp->next = next;
                    stmt->next = dropTemp;
                    next = dropTemp;
                    tempVar->state = irVarUninitialized;
                }
                freeDynArray(&usedTemps);
            }
            else if (stmt->tag == irScopeEnter) {
                ctx->currentScopeId = stmt->scopeEnter.scopeId;
            }
            else if (stmt->tag == irScopeExit) {
                u32 exitingScope = stmt->scopeExit.scopeId;
                ctx->currentScopeId = exitingScope > 0 ? exitingScope - 1 : 0;
                HashtableIt it = newHashTableIt(&ctx->trackedVars, sizeof(TrackedVar));
                while (hashTableItHasNext(&it)) {
                    TrackedVar *var = (TrackedVar *)hashTableItNext(&it);
                    if (var == NULL || var->scopeId != exitingScope || !isIrVarDestructible(var->decl))
                        continue;

                    IrNode *targetRef = makeIrVarRef(ctx->pool, &stmt->loc, var->decl);
                    if (var->needsDropFlag) {
                        IrNode *flagAccess = makeDropFlagAccess(ctx, &stmt->loc, var);
                        IrNode *dropStmt = makeIrDropIf(ctx->pool, &stmt->loc, targetRef, flagAccess);
                        dropStmt->next = stmt;
                        if (prev)
                            prev->next = dropStmt;
                        else
                            bb->basicBlock.first = dropStmt;
                        prev = dropStmt;
                    }
                    else if (var->state == irVarAssigned) {
                        IrNode *dropStmt = makeIrDrop(ctx->pool, &stmt->loc, targetRef);
                        dropStmt->next = stmt;
                        if (prev)
                            prev->next = dropStmt;
                        else
                            bb->basicBlock.first = dropStmt;
                        prev = dropStmt;
                        var->state = irVarUninitialized;
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

    freeHashTable(&ctx->trackedVars);
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
