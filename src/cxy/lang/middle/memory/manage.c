#include "memory.h"
#include "flow.h"

#include "operands.h"
#include "core/hash.h"
#include "lang/frontend/flag.h"
#include "lang/middle/ir/visitor.h"


bool isIrTypeCopyable(const IrType *type)
{
    return isIrTypeDestructible(type);
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

// A variable the analysis follows. A temporary is consumed by its statement and an
// ephemeral is dropped right after it is made, neither is live across statements.
static bool isOwnedVar(const TrackedVar *var)
{
    return var && !var->isTemporary && !var->isEphemeral;
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
        .isTemporary = (decl && irIsConsumedTemp(decl)),
        .isEphemeral = (decl && irIsStatementTemp(decl)),
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
    if (isOwnedVar(var))
        blockEffectsApply(data->effects, var->varIndex, flowKill);
}

static void computeIntraBlockGenKill(MemMgmtContext *ctx, CfgBlockInfo *block)
{
    BlockEffects effects = makeBlockEffects(ctx->localPool, ctx->trackedVarList.size);
    GenKillContext data = {.ctx = ctx, .effects = &effects};

    for (IrNode *stmt = block->bb->basicBlock.first; stmt; stmt = stmt->next) {
        if (stmt->tag == irAssign) {
            TrackedVar *lhsVar = getTrackedVar(ctx, findVarDeclInNode(stmt->assign.lhs));
            if (isOwnedVar(lhsVar)) {
                if (!isEmptyValue(stmt->assign.rhs))
                    blockEffectsApply(&effects, lhsVar->varIndex, flowDef);
                else if (stmt->assign.lhs->tag == irVarRef)
                    blockEffectsApply(&effects, lhsVar->varIndex, flowKill);
            }
        }
        else if (stmt->tag == irMove) {
            onMoveInGenKill(stmt->move.src, &data);
        }
        else if (stmt->tag == irCopy) {
            TrackedVar *dstVar = getTrackedVar(ctx, findVarDeclInNode(stmt->copy.dst));
            if (isOwnedVar(dstVar))
                blockEffectsApply(&effects, dstVar->varIndex, flowDef);
        }
        else if (stmt->tag == irDrop) {
            TrackedVar *var = getTrackedVar(ctx, findVarDeclInNode(stmt->drop.target));
            if (isOwnedVar(var))
                blockEffectsApply(&effects, var->varIndex, flowKill);
        }
        else if (stmt->tag == irScopeExit) {
            // what the scope declared is gone
            for (u64 v = 0; v < ctx->trackedVarList.size; v++) {
                TrackedVar *var = ((TrackedVar **)ctx->trackedVarList.elems)[v];
                if (isOwnedVar(var) && var->scopeId == stmt->scopeExit.scopeId)
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
            if (isOwnedVar(var) && !var->needsDropFlag) {
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
    bool removed; // the statement was taken out of the block
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


// The place an owner was moved out of is cleared, so its destructor finds nothing
static void nullifyMovedPlace(ElabCursor *c, IrNode *src)
{
    IrNode *clear = makeClearPlace(c->ctx->pool, &c->stmt->loc, src);
    if (clear)
        insertAfter(c, clear);
}


// `x` is no longer owned. Its drop flag is cleared where the move is, the
// statement that uses `x` still reads it, only ownership has moved.
static void killMovedVar(ElabCursor *c, IrNode *decl)
{
    TrackedVar *var = getTrackedVar(c->ctx, decl);
    if (!isOwnedVar(var))
        return;
    applyEvent(c->may, c->must, var->varIndex, flowKill);
    if (!var->needsDropFlag)
        return;

    IrNode *clearFlag = makeDropFlagAssign(c->ctx, &c->stmt->loc, var, false);
    if (clearFlag)
        insertBefore(c, clearFlag);
}

// A `move x` statement is only a marker for the analysis, it is gone afterwards
static void elaborateMoveStmt(ElabCursor *c)
{
    killMovedVar(c, findMovedVarDecl(c->stmt->move.src));
    if (c->prev)
        c->prev->next = c->next;
    else
        c->bb->basicBlock.first = c->next;
    c->removed = true;
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

    // a temporary or an ephemeral is not live across statements, normalize drops it
    if (!isOwnedVar(var))
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
        if (!isOwnedVar(var) || var->scopeId != exitingScope || !isIrVarDestructible(var->decl))
            continue;
        dropAtScopeExit(c, var);
    }
}

static void elaborateStmt(ElabCursor *c)
{
    IrNode *stmt = c->stmt;
    bool managed = !hasFlag(stmt, Unmanaged);
    if (stmt->tag == irAssign && managed)
        elaborateStoreTarget(c, stmt->assign.lhs, isEmptyValue(stmt->assign.rhs));
    else if (stmt->tag == irCopy && managed)
        elaborateStoreTarget(c, stmt->copy.dst, false);
    else if (stmt->tag == irDrop && managed)
        elaborateDrop(c);
    else if (stmt->tag == irScopeExit)
        elaborateScopeExit(c);
    else if (stmt->tag == irMove)
        elaborateMoveStmt(c);
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
        if (!c.removed)
            c.prev = stmt;
        c.removed = false;
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
