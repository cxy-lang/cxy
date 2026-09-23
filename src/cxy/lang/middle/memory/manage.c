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
    TrackedVar var = {
        .decl = decl,
        .name = name,
        .scopeId = scopeId,
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



static void scanForConditionalAssignments(MemMgmtContext *ctx, IrNode *func)
{
    IrNode *entryBB = func->function.firstBlock;
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irAssign) {
                IrNode *lhsDecl = findVarDeclInNode(stmt->assign.lhs);
                TrackedVar *lhsVar = getTrackedVar(ctx, lhsDecl);
                if (bb != entryBB && lhsVar && !lhsVar->isTemporary && lhsVar->decl && lhsVar->decl->tag == irVarDecl) {
                    if (!lhsVar->needsDropFlag) {
                        lhsVar->needsDropFlag = true;
                        lhsVar->state = irVarMaybeAssigned;
                        lhsVar->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                        pushOnDynArray(&ctx->dropFlagVars, &lhsVar);
                    }
                }

                IrNode *rhs = stmt->assign.rhs;
                if (rhs && rhs->tag == irMove) {
                    IrNode *rhsDecl = findVarDeclInNode(rhs->move.src);
                    TrackedVar *rhsVar = getTrackedVar(ctx, rhsDecl);
                    if (rhsVar && !rhsVar->isTemporary && rhsVar->decl && rhsVar->decl->tag == irVarDecl) {
                        if (!rhsVar->needsDropFlag) {
                            rhsVar->needsDropFlag = true;
                            rhsVar->state = irVarMaybeAssigned;
                            rhsVar->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                            pushOnDynArray(&ctx->dropFlagVars, &rhsVar);
                        }
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

static void processFunctionMemory(MemMgmtContext *ctx, IrNode *func)
{
    ctx->trackedVars = newHashTable(sizeof(TrackedVar), ctx->pool);
    ctx->trackedVarList = newDynArray(sizeof(TrackedVar *));
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
                else if (lhsVar && lhsVar->isEphemeral) {
                    lhsVar->state = irVarAssigned;
                    if (isIrTypeDestructible(lhsVar->type)) {
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
            else if (stmt->tag == irScopeEnter) {
                ctx->currentScopeId = stmt->scopeEnter.scopeId;
            }
            else if (stmt->tag == irScopeExit) {
                u32 exitingScope = stmt->scopeExit.scopeId;
                ctx->currentScopeId = exitingScope > 0 ? exitingScope - 1 : 0;
                for (i64 i = (i64)ctx->trackedVarList.size - 1; i >= 0; i--) {
                    TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[i];
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
