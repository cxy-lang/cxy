#include "reachability.h"
#include "core/hash.h"
#include "core/htable.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/ir/visitor.h"

typedef struct SymbolEntry {
    cstring name;
    IrNode *node;
} SymbolEntry;

typedef struct ModuleInitEntry {
    const IrNode *module;
    IrNode *initFunc;
} ModuleInitEntry;

typedef struct ReachabilityContext {
    CompilerDriver *driver;
    MemPool pool;
    StrPool *strings;
    HashTable symbolMap; // cstring name -> SymbolEntry
    HashTable moduleInits; // const IrNode* module -> ModuleInitEntry
    DynArray worklist;   // List of IrNode* (functions and globals)
} ReachabilityContext;

static inline bool compareSymbolEntry(const void *a, const void *b)
{
    const SymbolEntry *ea = (const SymbolEntry *)a;
    const SymbolEntry *eb = (const SymbolEntry *)b;
    return ea && eb && ea->name && eb->name && (ea->name == eb->name || strcmp(ea->name, eb->name) == 0);
}

static void addSymbol(ReachabilityContext *ctx, cstring name, IrNode *node)
{
    if (name == NULL || node == NULL)
        return;
    SymbolEntry entry = {.name = name, .node = node};
    insertInHashTable(&ctx->symbolMap,
                      &entry,
                      hashStr(hashInit(), name),
                      sizeof(SymbolEntry),
                      compareSymbolEntry);
}

static IrNode *findSymbol(ReachabilityContext *ctx, cstring name)
{
    if (name == NULL)
        return NULL;
    SymbolEntry query = {.name = name, .node = NULL};
    SymbolEntry *found = findInHashTable(&ctx->symbolMap,
                                         &query,
                                         hashStr(hashInit(), name),
                                         sizeof(SymbolEntry),
                                         compareSymbolEntry);
    return found ? found->node : NULL;
}

static inline bool compareModuleInitEntry(const void *a, const void *b)
{
    const ModuleInitEntry *ea = (const ModuleInitEntry *)a;
    const IrNode *mod = (const IrNode *)b;
    return ea && ea->module == mod;
}

static void addModuleInit(ReachabilityContext *ctx, const IrNode *module, IrNode *initFunc)
{
    if (module == NULL || initFunc == NULL)
        return;
    ModuleInitEntry entry = {.module = module, .initFunc = initFunc};
    insertInHashTable(&ctx->moduleInits,
                      &entry,
                      hashPtr(hashInit(), module),
                      sizeof(ModuleInitEntry),
                      compareModuleInitEntry);
}

static IrNode *findModuleInit(ReachabilityContext *ctx, const IrNode *module)
{
    if (module == NULL)
        return NULL;
    ModuleInitEntry *found = findInHashTable(&ctx->moduleInits,
                                            module,
                                            hashPtr(hashInit(), module),
                                            sizeof(ModuleInitEntry),
                                            compareModuleInitEntry);
    return found ? found->initFunc : NULL;
}

static void markAndEnqueue(ReachabilityContext *ctx, IrNode *node)
{
    if (node == NULL || hasFlag(node, Reachable))
        return;
    node->flags |= flgReachable;
    pushOnDynArray(&ctx->worklist, &node);

    if (node->tag == irGlobalVar && node->parent) {
        IrNode *initFunc = findModuleInit(ctx, node->parent);
        if (initFunc)
            markAndEnqueue(ctx, initFunc);
    }
}

static void markSymbolByName(ReachabilityContext *ctx, cstring name)
{
    if (name == NULL)
        return;
    IrNode *target = findSymbol(ctx, name);
    if (target)
        markAndEnqueue(ctx, target);
}

static void markTypeLifecycleFunctions(ReachabilityContext *ctx, const IrType *type, bool isCopy)
{
    if (type == NULL)
        return;

    while (type && isIrPointerType(type) && type->category != irTypeCatClass)
        type = type->pointer.pointee;

    if (type == NULL)
        return;

    if (type->category == irTypeCatClass) {
        markSymbolByName(ctx, isCopy ? S_sptr_ref : S_sptr_drop);
        const IrType *st = isIrPointerType(type) ? type->pointer.pointee : type;
        if (st) {
            IrNode *func = isCopy ? st->strct.copy : st->strct.dctor;
            if (func)
                markAndEnqueue(ctx, func);
            if (!isCopy && st->strct.dctorFwd)
                markAndEnqueue(ctx, st->strct.dctorFwd);
        }
    }
    else if (type->category == irTypeCatOptional) {
        if (type->strct.count > 1 && type->strct.fields[1].type)
            markTypeLifecycleFunctions(ctx, type->strct.fields[1].type, isCopy);
    }
    else if (type->category == irTypeCatStruct || type->category == irTypeCatTuple || type->category == irTypeCatUnion) {
        IrNode *func = isCopy ? type->strct.copy : type->strct.dctor;
        if (func)
            markAndEnqueue(ctx, func);
    }
}

static void visitGlobalRef(ConstIrVisitor *visitor, const IrNode *node)
{
    ReachabilityContext *ctx = getConstIrVisitorContext(visitor);
    if (node->globalRef.target)
        markAndEnqueue(ctx, node->globalRef.target);
    else if (node->globalRef.name)
        markSymbolByName(ctx, node->globalRef.name);
}

static void visitVarRef(ConstIrVisitor *visitor, const IrNode *node)
{
    ReachabilityContext *ctx = getConstIrVisitorContext(visitor);
    if (node->varRef.target && node->varRef.target->tag == irGlobalVar)
        markAndEnqueue(ctx, node->varRef.target);
}

static void visitAddrOf(ConstIrVisitor *visitor, const IrNode *node)
{
    ReachabilityContext *ctx = getConstIrVisitorContext(visitor);
    if (node->addrOf.target) {
        if (node->addrOf.target->tag == irGlobalVar)
            markAndEnqueue(ctx, node->addrOf.target);
        else if (node->addrOf.target->tag == irGlobalRef) {
            if (node->addrOf.target->globalRef.target)
                markAndEnqueue(ctx, node->addrOf.target->globalRef.target);
            else if (node->addrOf.target->globalRef.name)
                markSymbolByName(ctx, node->addrOf.target->globalRef.name);
        }
    }
    irConstVisitFallbackVisitAll(visitor, node);
}

static void visitAlloc(ConstIrVisitor *visitor, const IrNode *node)
{
    ReachabilityContext *ctx = getConstIrVisitorContext(visitor);
    if (node->alloc.type) {
        const IrType *st = isIrPointerType(node->alloc.type) ? node->alloc.type->pointer.pointee : node->alloc.type;
        if (st && st->category == irTypeCatClass) {
            markSymbolByName(ctx, ctx->driver->options.withMemoryTrace ? S_sptr_alloc_trace : S_sptr_alloc);
            if (st->strct.dctorFwd)
                markAndEnqueue(ctx, st->strct.dctorFwd);
            else if (st->strct.dctor)
                markAndEnqueue(ctx, st->strct.dctor);
        }
    }
    irConstVisitFallbackVisitAll(visitor, node);
}

static void visitDrop(ConstIrVisitor *visitor, const IrNode *node)
{
    ReachabilityContext *ctx = getConstIrVisitorContext(visitor);
    if (node->drop.target && node->drop.target->type)
        markTypeLifecycleFunctions(ctx, node->drop.target->type, false);
    irConstVisitFallbackVisitAll(visitor, node);
}

static void visitCopy(ConstIrVisitor *visitor, const IrNode *node)
{
    ReachabilityContext *ctx = getConstIrVisitorContext(visitor);
    if (node->copy.src && node->copy.src->type)
        markTypeLifecycleFunctions(ctx, node->copy.src->type, true);
    irConstVisitFallbackVisitAll(visitor, node);
}

static void collectRoots(ReachabilityContext *ctx)
{
    DynArray *modules = &ctx->driver->finalizedModules;
    for (size_t i = 0; i < modules->size; i++) {
        const IrNode *mod = dynArrayAt(const IrNode **, modules, i);
        if (mod == NULL)
            continue;

        for (IrNode *func = mod->module.functions; func; func = func->next) {
            bool isRoot = false;
            if (hasFlag(func, Main) || func->function.display == S_main || func->function.name == S_main ||
                func->function.name == S_CXY__main || func->function.name == S__main) {
                isRoot = true;
            }
            else if (ctx->driver->options.buildPlugin && hasFlag(func, Public)) {
                isRoot = true;
            }
            else if (ctx->driver->options.cmd == cmdTest || ctx->driver->hasTestCases) {
                if (hasFlag(func, TestContext) || func->function.name == S_allTestCases)
                    isRoot = true;
            }

            if (isRoot)
                markAndEnqueue(ctx, func);
        }

        for (IrNode *glob = mod->module.globals; glob; glob = glob->next) {
            bool isRoot = false;
            if (ctx->driver->options.buildPlugin && hasFlag(glob, Public))
                isRoot = true;
            else if (ctx->driver->options.cmd == cmdTest || ctx->driver->hasTestCases) {
                if (hasFlag(glob, TestContext) || glob->globalVar.name == S_allTestCases)
                    isRoot = true;
            }

            if (isRoot)
                markAndEnqueue(ctx, glob);
        }
    }
}

void runReachabilityAnalysis(CompilerDriver *driver)
{
    if (driver->finalizedModules.size == 0)
        return;

    ReachabilityContext ctx = {
        .driver = driver,
        .pool = newMemPool(),
        .strings = driver->strings,
        .symbolMap = newHashTable(sizeof(SymbolEntry), &ctx.pool),
        .moduleInits = newHashTable(sizeof(ModuleInitEntry), &ctx.pool),
        .worklist = newDynArray(sizeof(IrNode *))
    };

    DynArray *modules = &driver->finalizedModules;
    for (size_t i = 0; i < modules->size; i++) {
        IrNode *mod = dynArrayAt(IrNode **, modules, i);
        if (mod == NULL)
            continue;

        IrNode *initFunc = NULL;
        for (IrNode *func = mod->module.functions; func; func = func->next) {
            func->flags &= ~flgReachable;
            func->parent = mod;
            addSymbol(&ctx, func->function.name, func);
            if (hasFlag(func, ModuleInit) || func->function.name == S___init ||
                (func->function.name && strstr(func->function.name, "___init"))) {
                initFunc = func;
            }
        }
        for (IrNode *glob = mod->module.globals; glob; glob = glob->next) {
            glob->flags &= ~flgReachable;
            glob->parent = mod;
            addSymbol(&ctx, glob->globalVar.name, glob);
        }

        if (initFunc)
            addModuleInit(&ctx, mod, initFunc);
    }

    collectRoots(&ctx);

    // clang-format off
    ConstIrVisitor visitor = makeConstIrVisitor(&ctx, {
        [irGlobalRef] = visitGlobalRef,
        [irVarRef] = visitVarRef,
        [irAddrOf] = visitAddrOf,
        [irAlloc] = visitAlloc,
        [irDrop] = visitDrop,
        [irCopy] = visitCopy
    }, .fallback = irConstVisitFallbackVisitAll);
    // clang-format on

    while (ctx.worklist.size > 0) {
        IrNode *item = *(IrNode **)popDynArray(&ctx.worklist);
        if (item == NULL)
            continue;

        if (item->tag == irFunction) {
            for (IrNode *bb = item->function.firstBlock; bb; bb = bb->next) {
                irConstVisit(&visitor, bb);
            }
        }
        else if (item->tag == irGlobalVar && item->globalVar.init) {
            irConstVisit(&visitor, item->globalVar.init);
        }
    }

    freeDynArray(&ctx.worklist);
    freeHashTable(&ctx.symbolMap);
    freeHashTable(&ctx.moduleInits);
    freeMemPool(&ctx.pool);
}
