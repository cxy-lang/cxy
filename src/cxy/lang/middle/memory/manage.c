#include "memory.h"
#include "flow.h"
#include "normalize.h"
#include "core/hash.h"
#include "lang/frontend/flag.h"
#include "lang/middle/ir/visitor.h"


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


// Lowering marks every operand a consumer takes ownership of with
// `:owned-position`. These walk the operands under a node and call `cb` with the
// slot of each marked one, so a move in any position is found, whatever holds it.
typedef struct OwnedSlotWalk {
    void (*cb)(IrNode **slot, void *ctx);
    void *ctx;
} OwnedSlotWalk;

static void walkOperandSlot(IrNode **slot, void *walkPtr)
{
    OwnedSlotWalk *walk = walkPtr;
    if (*slot == NULL)
        return;
    if (irHasInfo(*slot, irInfoOwnedPosition))
        walk->cb(slot, walk->ctx);
    irForEachOperandSlot(*slot, walkOperandSlot, walk);
}

static void forEachOwnedSlot(IrNode *node, void (*cb)(IrNode **slot, void *ctx), void *cbCtx)
{
    OwnedSlotWalk walk = {.cb = cb, .ctx = cbCtx};
    irForEachOperandSlot(node, walkOperandSlot, &walk);
}

typedef struct MovedSourceVisit {
    void (*cb)(IrNode *src, void *ctx);
    void *ctx;
} MovedSourceVisit;

static void onOwnedSlotMovedSource(IrNode **slot, void *ctxPtr)
{
    MovedSourceVisit *visit = ctxPtr;
    IrNode **mv = moveSlot(slot);
    if (mv)
        visit->cb((*mv)->move.src, visit->ctx);
}

// `stmt` is an assignment or a call
static void forEachMovedSource(IrNode *stmt, void (*cb)(IrNode *src, void *ctx), void *cbCtx)
{
    MovedSourceVisit visit = {.cb = cb, .ctx = cbCtx};
    forEachOwnedSlot(stmt, onOwnedSlotMovedSource, &visit);
}

// A store of nothing: `x = null`, or an optional with no value
static bool isEmptyValue(const IrNode *rhs)
{
    return rhs && (rhs->tag == irNullConst ||
                   (rhs->tag == irOptionalValue && !rhs->optionalValue.hasValue));
}

typedef struct GenKillContext {
    MemMgmtContext *ctx;
    BlockEffects *effects;
} GenKillContext;

static void onMoveInGenKill(IrNode *src, void *ctxPtr)
{
    GenKillContext *data = ctxPtr;
    TrackedVar *var = getTrackedVar(data->ctx, findMovedVarDecl(src));
    if (var && !var->isTemporary)
        blockEffectsApply(data->effects, var->varIndex, flowKill);
}

static void computeIntraBlockGenKill(MemMgmtContext *ctx, CfgBlockInfo *block)
{
    BlockEffects effects = makeBlockEffects(ctx->localPool, ctx->trackedVarList.size);
    GenKillContext data = {.ctx = ctx, .effects = &effects};

    for (IrNode *stmt = block->bb->basicBlock.first; stmt; stmt = stmt->next) {
        if (stmt->tag == irAssign) {
            TrackedVar *lhsVar = getTrackedVar(ctx, findVarDeclInNode(stmt->assign.lhs));
            if (lhsVar && !lhsVar->isTemporary) {
                if (!isEmptyValue(stmt->assign.rhs))
                    blockEffectsApply(&effects, lhsVar->varIndex, flowDef);
                else if (stmt->assign.lhs->tag == irVarRef)
                    blockEffectsApply(&effects, lhsVar->varIndex, flowKill);
            }
            forEachMovedSource(stmt, onMoveInGenKill, &data);
        }
        else if (stmt->tag == irCall) {
            forEachMovedSource(stmt, onMoveInGenKill, &data);
        }
        else if (stmt->tag == irCopy) {
            TrackedVar *dstVar = getTrackedVar(ctx, findVarDeclInNode(stmt->copy.dst));
            if (dstVar && !dstVar->isTemporary)
                blockEffectsApply(&effects, dstVar->varIndex, flowDef);
        }
        else if (stmt->tag == irDrop) {
            TrackedVar *var = getTrackedVar(ctx, findVarDeclInNode(stmt->drop.target));
            if (var && !var->isTemporary)
                blockEffectsApply(&effects, var->varIndex, flowKill);
        }
        else if (stmt->tag == irScopeExit) {
            // what the scope declared is gone
            for (u64 v = 0; v < ctx->trackedVarList.size; v++) {
                TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[v];
                if (var && var->scopeId == stmt->scopeExit.scopeId && !var->isTemporary)
                    blockEffectsApply(&effects, var->varIndex, flowKill);
            }
        }
    }
    blockEffectsFinish(&effects, block, ctx->trackedVarList.size);
}

static void runDataflowAnalysis(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count, u64 varCount)
{
    if (count == 0 || varCount == 0)
        return;

    for (u64 i = 0; i < count; i++)
        computeIntraBlockGenKill(ctx, blocks[i]);

    // A parameter is owned when the function is entered
    BitSet entryOwned = makeBitSet(ctx->localPool, varCount);
    for (u64 i = 0; i < ctx->trackedVarList.size; i++) {
        TrackedVar *v = ((TrackedVar **)ctx->trackedVarList.elems)[i];
        if (v && v->decl && v->decl->tag == irFuncParam)
            bitSetSet(&entryOwned, v->varIndex);
    }
    solveOwnership(ctx->localPool, blocks, count, varCount, &entryOwned);
}

// A variable that is owned on some of the paths into a block and not on the
// others needs a drop flag. Inside a block every event stores or kills on all
// paths at once, so a block start is the only place a variable can be partly owned.
static void identifyDropFlagVariables(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count)
{
    u64 varCount = ctx->trackedVarList.size;
    for (u64 i = 0; i < count; i++) {
        for (u64 v = 0; v < varCount; v++) {
            if (!bitSetTest(&blocks[i]->mayIn, v) || bitSetTest(&blocks[i]->mustIn, v))
                continue;
            TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[v];
            if (var && !var->isTemporary && !var->needsDropFlag) {
                var->needsDropFlag = true;
                var->dropFlagIndex = (u32)ctx->dropFlagVars.size;
                pushOnDynArray(&ctx->dropFlagVars, &var);
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

static void insertBeforeStmt(IrNode *bb, IrNode **prevRef, IrNode *stmt, IrNode *node)
{
    node->next = stmt;
    if (*prevRef)
        (*prevRef)->next = node;
    else
        bb->basicBlock.first = node;
    *prevRef = node;
}

static void insertAfterStmt(IrNode *stmt, IrNode **nextRef, IrNode *node)
{
    node->next = *nextRef;
    stmt->next = node;
    *nextRef = node;
}

// The statement being elaborated, the new statements go before it (`prev` is the
// last one before it) or after it (`next` is the first one after it)
typedef struct ElabCursor {
    MemMgmtContext *ctx;
    IrNode *bb;
    IrNode *prev;
    IrNode *stmt;
    IrNode *next;
    BitSet *may;
    BitSet *must;
} ElabCursor;

static void insertBefore(ElabCursor *c, IrNode *node)
{
    insertBeforeStmt(c->bb, &c->prev, c->stmt, node);
}

static void insertAfter(ElabCursor *c, IrNode *node)
{
    insertAfterStmt(c->stmt, &c->next, node);
}

static void insertFlagAfter(ElabCursor *c, TrackedVar *var, bool value)
{
    IrNode *flag = makeDropFlagAssign(c->ctx, &c->stmt->loc, var, value);
    if (flag)
        insertAfter(c, flag);
}

static void insertDropBefore(ElabCursor *c, IrNode *target)
{
    insertBefore(c, makeIrDrop(c->ctx->pool, &c->stmt->loc, target));
}

// Replaces a `move src` operand by `src` itself and returns it
static IrNode *stripMove(IrNode **moveRef)
{
    IrNode *mv = *moveRef;
    IrNode *src = mv->move.src;
    src->next = mv->next;
    *moveRef = src;
    return src;
}

static bool isPlaceNode(const IrNode *node)
{
    return node && (node->tag == irMember || node->tag == irIndex);
}

static bool isPointerLikeType(const IrType *type)
{
    return type && (type->tag == irTypePointer || type->category == irTypeCatClass);
}

// A place whose owner can be moved out: it is cleared afterwards
static bool isMovablePlace(const IrNode *node)
{
    return isPlaceNode(node) && (isPointerLikeType(node->type) || isIrTypeDestructible(node->type));
}

// The statement that empties `place`: null for a pointer or class, zeroed memory
// for a struct. Unmanaged, so the pass does not elaborate it again.
static IrNode *makeClearPlace(ElabCursor *c, IrNode *place)
{
    MemPool *pool = c->ctx->pool;
    const FileLoc *loc = &c->stmt->loc;
    IrNode *clear = NULL;
    if (isPointerLikeType(place->type))
        clear = makeIrAssign(pool, loc, place, makeIrNullConst(pool, loc, place->type));
    else if (isIrTypeDestructible(place->type))
        clear = makeIrZeromem(pool, loc, place);
    if (clear)
        clear->flags |= flgUnmanaged;
    return clear;
}

// The place an owner was moved out of is cleared, so its destructor finds nothing
static void nullifyMovedPlace(ElabCursor *c, IrNode *src)
{
    IrNode *clear = makeClearPlace(c, src);
    if (clear)
        insertAfter(c, clear);
}

// A temporary the pass creates to hold an owner. It starts as moved-out, so it is
// owned only once something is stored into it.
static IrNode *declareOwnedTemp(ElabCursor *c, cstring prefix, const IrNode *place)
{
    MemMgmtContext *ctx = c->ctx;
    cstring name = makeStringf(ctx->strings, "%s%u", prefix, ctx->tempCounter++);
    IrNode *decl = makeIrVarDecl(
        ctx->pool, &place->loc, flgTemporary, name, name, place->type, ctx->currentScopeId);
    TrackedVar *tracked = addTrackedVar(ctx, decl, name, ctx->currentScopeId, place->type);
    if (tracked)
        tracked->state = irVarMoved;
    return decl;
}

// `x` is no longer owned. Its drop flag is cleared before the statement (an operand
// still reads the variable, only ownership moves) or after it (an assignment).
static void killMovedVar(ElabCursor *c, IrNode *decl, bool flagBefore)
{
    TrackedVar *var = getTrackedVar(c->ctx, decl);
    if (var == NULL || var->isTemporary)
        return;
    applyEvent(c->may, c->must, var->varIndex, flowKill);
    if (!var->needsDropFlag)
        return;

    IrNode *clearFlag = makeDropFlagAssign(c->ctx, &c->stmt->loc, var, false);
    if (clearFlag == NULL)
        return;
    if (flagBefore)
        insertBefore(c, clearFlag);
    else
        insertAfter(c, clearFlag);
}

// `__mv = src; src = null;` then the operand is `__mv`
static void captureMovedPlace(ElabCursor *c, IrNode **moveRef, IrNode *src)
{
    MemPool *pool = c->ctx->pool;
    IrNode *mv = *moveRef;
    IrNode *tempVar = declareOwnedTemp(c, "__mv", src);
    IrNode *capture = makeIrAssign(pool, &src->loc, makeIrVarRef(pool, &src->loc, tempVar), src);
    capture->flags |= flgUnmanaged;
    IrNode *clear = makeClearPlace(c, cloneIrNode(pool, src));

    insertBefore(c, tempVar);
    insertBefore(c, capture);
    insertBefore(c, clear);

    IrNode *tempRef = makeIrVarRef(pool, &src->loc, tempVar);
    tempRef->next = mv->next;
    *moveRef = tempRef;
}

// Moves take effect when the operand is evaluated, i.e. before the statement.
// Clearing after it is wrong once the parent can resume mid-call (async).
static void elaborateMovedOperand(ElabCursor *c, IrNode **moveRef)
{
    IrNode *src = (*moveRef)->move.src;
    IrNode *srcDecl = findMovedVarDecl(src);
    if (srcDecl == NULL && isMovablePlace(src)) {
        captureMovedPlace(c, moveRef, src);
        return;
    }
    stripMove(moveRef);
    if (srcDecl)
        killMovedVar(c, srcDecl, true);
}

// A place that is not a temporary or a call result is still owned by someone else
static bool needsOwnedCopy(const IrNode *inner)
{
    if (inner == NULL)
        return false;
    if (inner->tag == irVarRef) {
        const IrNode *target = inner->varRef.target;
        return target && !hasFlag(target, Temporary) && !hasFlag(target, Returned);
    }
    return isPlaceNode(inner) && !isUnwrappedCallResult(inner);
}

// `__copy = @copy inner`, the operand is `__copy` under the same casts
static void copyIntoTemp(ElabCursor *c, IrNode **slot, IrNode *val, IrNode *inner)
{
    MemPool *pool = c->ctx->pool;
    IrNode *tempVar = declareOwnedTemp(c, "__copy", inner);
    IrNode *dstRef = makeIrVarRef(pool, &inner->loc, tempVar);
    insertBefore(c, tempVar);
    insertBefore(c, makeIrCopy(pool, &inner->loc, inner, dstRef));

    IrNode *finalArg = rebuildCastChain(pool, val, dstRef);
    finalArg->next = val->next;
    *slot = finalArg;
}

static void elaborateOwnedOperand(IrNode **slot, void *cursor)
{
    ElabCursor *c = cursor;
    IrNode *val = *slot;
    if (val == NULL)
        return;

    IrNode **moveRef = moveSlot(slot);
    if (moveRef) {
        elaborateMovedOperand(c, moveRef);
        return;
    }
    if (!isIrTypeDestructible(val->type))
        return;
    IrNode *inner = unwrapCasts(val);
    if (needsOwnedCopy(inner))
        copyIntoTemp(c, slot, val, inner);
}

// A `&&x` for a parameter that only borrows is not a move: `x` stays owned, so
// the move is dropped and the operand is the borrow itself
static void dropBorrowedMoves(IrNode *call)
{
    for (IrNode **arg = &call->call.args; *arg; arg = &(*arg)->next) {
        if (irHasInfo(*arg, irInfoOwnedPosition))
            continue;
        IrNode **mv = moveSlot(arg);
        if (mv)
            stripMove(mv);
    }
}

// Moves and copies for every operand of a call or a literal that a consumer takes
// ownership of, found by the `:owned-position` bit, in evaluation order
static void injectOperandCopies(ElabCursor *c, IrNode *value)
{
    if (value == NULL)
        return;
    if (value->tag == irCall)
        dropBorrowedMoves(value);
    forEachOwnedSlot(value, elaborateOwnedOperand, c);
}

static bool shouldPromoteAssignToCopy(const IrNode *lhsDecl, const IrNode *lhs, const IrNode *rhs)
{
    if (!isIrVarDestructible(lhsDecl) && !isIrTypeDestructible(lhs->type))
        return false;
    // `<Base>x` (a class up/down cast) still refers to `x`, so it is copied
    const IrNode *inner = unwrapCastsConst(rhs);
    IrNode *rhsDecl = findVarDeclInNode(inner);
    if (rhsDecl && hasFlag(rhsDecl, Temporary))
        return false;
    if (inner->tag == irVarRef) {
        return inner->varRef.target && !hasFlag(inner->varRef.target, Temporary);
    }
    return inner->tag == irMember || inner->tag == irIndex;
}

// What storing into `target` does: the old value of an element or field is
// dropped, so is the old value of an owned variable (guarded by its flag when only
// some paths own it). The variable is owned afterwards, or not when nothing is
// stored. Shared by an assignment and a user `@copy`.
static void elaborateStoreTarget(ElabCursor *c, IrNode *target, bool storesNothing)
{
    MemMgmtContext *ctx = c->ctx;
    if (target->tag == irIndex && isIrTypeDestructible(target->type)) {
        IrNode *base = target->index.target;
        // Do not auto-drop if indexing into a raw pointer buffer (^T)
        if (base && base->type && !isIrPointerType(base->type))
            insertDropBefore(c, target);
    }
    else if (target->tag == irMember && isIrTypeDestructible(target->type)) {
        IrNode *baseDecl = findVarDeclInNode(target);
        bool shouldDrop = true;
        if (baseDecl && baseDecl->tag == irVarDecl) {
            TrackedVar *baseVar = getTrackedVar(ctx, baseDecl);
            shouldDrop = baseVar && !baseVar->isTemporary && bitSetTest(c->must, baseVar->varIndex);
        }
        if (shouldDrop)
            insertDropBefore(c, target);
    }

    TrackedVar *var = getTrackedVar(ctx, findVarDeclInNode(target));
    if (var == NULL)
        return;

    // ephemerals are dropped right after the store, so they are never live across statements
    if (var->isEphemeral) {
        if (isIrTypeDestructible(var->type))
            insertAfter(c, makeIrDrop(ctx->pool, &c->stmt->loc, target));
        return;
    }
    if (var->isTemporary)
        return;

    if (target->tag == irVarRef && bitSetTest(c->may, var->varIndex)) {
        if (var->needsDropFlag && !bitSetTest(c->must, var->varIndex)) {
            IrNode *flagAccess = makeDropFlagAccess(ctx, &c->stmt->loc, var);
            insertBefore(c, makeIrDropIf(ctx->pool, &c->stmt->loc, target, flagAccess));
        }
        else if (bitSetTest(c->must, var->varIndex)) {
            insertDropBefore(c, target);
        }
    }

    if (storesNothing && target->tag != irVarRef)
        return;
    applyEvent(c->may, c->must, var->varIndex, storesNothing ? flowKill : flowDef);
    if (target->tag == irVarRef && var->needsDropFlag)
        insertFlagAfter(c, var, !storesNothing);
}

// `x = &&y`: the move is stripped, `y` is no longer owned, or the field it was
// taken from is cleared
static void elaborateAssignMove(ElabCursor *c, IrNode **moveRef)
{
    IrNode *src = stripMove(moveRef);
    IrNode *srcDecl = findMovedVarDecl(src);
    if (srcDecl)
        killMovedVar(c, srcDecl, false);
    else if (isPlaceNode(src))
        nullifyMovedPlace(c, src);
}

static bool isOwnerConsumingValue(const IrNode *rhs)
{
    return rhs->tag == irCall || rhs->tag == irUnionValue || rhs->tag == irOptionalValue ||
           rhs->tag == irStructConst || rhs->tag == irArrayConst || rhs->tag == irClosureValue;
}

static void elaborateAssign(ElabCursor *c)
{
    IrNode *stmt = c->stmt;
    IrNode *rhs = stmt->assign.rhs;
    elaborateStoreTarget(c, stmt->assign.lhs, isEmptyValue(rhs));
    if (rhs == NULL)
        return;

    IrNode **rhsMove = moveSlot(&stmt->assign.rhs);
    if (rhsMove) {
        elaborateAssignMove(c, rhsMove);
    }
    else if (isOwnerConsumingValue(rhs)) {
        injectOperandCopies(c, rhs);
    }
    else {
        IrNode *lhs = stmt->assign.lhs;
        if (shouldPromoteAssignToCopy(findVarDeclInNode(lhs), lhs, rhs)) {
            stmt->tag = irCopy;
            stmt->copy.src = rhs;
            stmt->copy.dst = lhs;
        }
    }
}

// A dropped field is cleared, so the owner's destructor does not drop it again
static void elaborateDrop(ElabCursor *c)
{
    IrNode *target = c->stmt->drop.target;
    if (target && target->tag == irMember)
        nullifyMovedPlace(c, target);
}

// A variable that goes out of scope is dropped when it is owned, or when its flag
// says so if only some paths own it
static void dropAtScopeExit(ElabCursor *c, TrackedVar *var)
{
    MemMgmtContext *ctx = c->ctx;
    bool owned = bitSetTest(c->must, var->varIndex);
    bool guarded = var->needsDropFlag && bitSetTest(c->may, var->varIndex) && !owned;
    if (!owned && !guarded)
        return;

    IrNode *targetRef = makeIrVarRef(ctx->pool, &c->stmt->loc, var->decl);
    if (guarded) {
        IrNode *flagAccess = makeDropFlagAccess(ctx, &c->stmt->loc, var);
        insertBefore(c, makeIrDropIf(ctx->pool, &c->stmt->loc, targetRef, flagAccess));
    }
    else {
        insertDropBefore(c, targetRef);
    }

    if (var->needsDropFlag) {
        IrNode *clearFlag = makeDropFlagAssign(ctx, &c->stmt->loc, var, false);
        if (clearFlag)
            insertBefore(c, clearFlag);
    }
    applyEvent(c->may, c->must, var->varIndex, flowKill);
}

// Variables are dropped in reverse order of declaration
static void elaborateScopeExit(ElabCursor *c)
{
    MemMgmtContext *ctx = c->ctx;
    u32 exitingScope = c->stmt->scopeExit.scopeId;
    for (i64 i = (i64)ctx->trackedVarList.size - 1; i >= 0; i--) {
        TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[i];
        if (var == NULL || var->scopeId != exitingScope || !isIrVarDestructible(var->decl) || var->isTemporary)
            continue;
        dropAtScopeExit(c, var);
    }
}

static void elaborateStmt(ElabCursor *c)
{
    IrNode *stmt = c->stmt;
    bool managed = !hasFlag(stmt, Unmanaged);
    if (stmt->tag == irAssign && managed)
        elaborateAssign(c);
    else if (stmt->tag == irCopy && managed)
        elaborateStoreTarget(c, stmt->copy.dst, false);
    else if (stmt->tag == irDrop && managed)
        elaborateDrop(c);
    else if (stmt->tag == irCall)
        injectOperandCopies(c, stmt);
    else if (stmt->tag == irScopeExit)
        elaborateScopeExit(c);
}

static void elaborateBlock(MemMgmtContext *ctx, CfgBlockInfo *block, BitSet *may, BitSet *must)
{
    IrNode *bb = block->bb;
    bitSetCopy(may, &block->mayIn);
    bitSetCopy(must, &block->mustIn);

    ElabCursor c = {.ctx = ctx, .bb = bb, .may = may, .must = must};
    for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = c.next) {
        c.stmt = stmt;
        c.next = stmt->next;
        elaborateStmt(&c);
        c.prev = stmt;
    }

    IrNode *lastStmt = NULL;
    for (IrNode *s = bb->basicBlock.first; s; s = s->next)
        lastStmt = s;
    bb->basicBlock.last = lastStmt;
}

static void elaborateDropsAndReassignments(MemMgmtContext *ctx, CfgBlockInfo **blocks, u64 count)
{
    u64 varCount = ctx->trackedVarList.size;
    BitSet may = makeBitSet(ctx->localPool, varCount);
    BitSet must = makeBitSet(ctx->localPool, varCount);
    for (u64 b = 0; b < count; b++)
        elaborateBlock(ctx, blocks[b], &may, &must);
}

static void visitNoCopyCopy(IrVisitor *visitor, IrNode *node)
{
    MemMgmtContext *ctx = getIrVisitorContext(visitor);
    irVisitFallbackVisitAll(visitor, node);
    const IrType *type = node->copy.src ? node->copy.src->type : NULL;
    if (!isIrTypeNoCopy(type))
        return;

    // Report the type behind a reference, and a class by its readable name
    if (type->tag == irTypePointer && type->category == irTypeCatReference)
        type = type->pointer.pointee;
    cstring name = type->typeName ?: type->display ?: type->name;
    logError(ctx->driver->L,
             &node->loc,
             "`{s}` cannot be copied, it is `@nocopy` or holds a `@nocopy` "
             "value. Move it with `&&x` or borrow it with `&x`",
             (FormatArg[]){{.s = name}});
}

// Runs after every copy exists, so it sees user `@copy` and the copies the pass
// inserted alike.
static void rejectNoCopyCopies(MemMgmtContext *ctx, IrNode *func)
{
    // Generated copy and destructor functions are not user code
    if (irHasInfo(func, irInfoGlue))
        return;

    IrVisitor visitor = makeIrVisitor(ctx, {[irCopy] = visitNoCopyCopy});
    visitor.fallback = irVisitFallbackVisitAll;
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag == irBasicBlock)
            irVisitManyNodes(&visitor, bb->basicBlock.first);
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

    normalizeFunction(ctx, func);
    collectScopeVariables(ctx, func);

    u64 blockCount = 0;
    CfgBlockInfo **blocks = buildCfg(ctx->localPool, func, ctx->trackedVarList.size, &blockCount);

    if (blockCount > 0) {
        if (ctx->trackedVarList.size > 0) {
            runDataflowAnalysis(ctx, blocks, blockCount, ctx->trackedVarList.size);
            identifyDropFlagVariables(ctx, blocks, blockCount);
            synthesizeDropFlags(ctx, func);
        }
        elaborateDropsAndReassignments(ctx, blocks, blockCount);
        rejectNoCopyCopies(ctx, func);
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
