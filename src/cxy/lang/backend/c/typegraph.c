#include "typegraph.h"
#include "core/hash.h"
#include "lang/middle/ir/visitor.h"

#include <string.h>

static IrTypeGraphNode *findOrAddGraphNode(IrTypeGraph *g, const IrType *type)
{
    if (type == NULL)
        return NULL;

    IrTypeGraphNode *found = findInHashTable(
        &g->nodes, &(IrTypeGraphNode){.type = type}, hashPtr(hashInit(), type),
        sizeof(IrTypeGraphNode), comparePointers);
    if (found)
        return found;

    IrTypeGraphNode node = {
        .type = type,
        .deps = newDynArray(sizeof(const IrType *)),
        .pointerDeps = newDynArray(sizeof(const IrType *)),
        .visited = false,
        .inStack = false,
        .emitted = false};

    insertInHashTable(&g->nodes, &node, hashPtr(hashInit(), type),
                      sizeof(IrTypeGraphNode), comparePointers);
    return findInHashTable(&g->nodes, &(IrTypeGraphNode){.type = type},
                           hashPtr(hashInit(), type), sizeof(IrTypeGraphNode),
                           comparePointers);
}

void addIrTypeToGraph(IrTypeGraph *g, const IrType *type)
{
    if (type == NULL)
        return;

    IrTypeGraphNode *node = findOrAddGraphNode(g, type);
    if (node == NULL || node->visited)
        return;
    node->visited = true;

    switch (type->tag) {
    case irTypePointer:
        if (type->pointer.pointee) {
            addIrTypeToGraph(g, type->pointer.pointee);
            pushOnDynArray(&node->pointerDeps, &type->pointer.pointee);
            pushOnDynArray(&g->forwardDecls, &type->pointer.pointee);
        }
        break;
    case irTypeArray:
        if (type->array.elem) {
            addIrTypeToGraph(g, type->array.elem);
            pushOnDynArray(&node->deps, &type->array.elem);
        }
        break;
    case irTypeStruct:
    case irTypeUntaggedUnion:
        for (u64 i = 0; i < type->strct.count; i++) {
            const IrType *fldType = type->strct.fields[i].type;
            if (fldType == NULL)
                continue;
            addIrTypeToGraph(g, fldType);
            if (isIrPointerType(fldType)) {
                if (fldType->pointer.pointee) {
                    pushOnDynArray(&node->pointerDeps, &fldType->pointer.pointee);
                    pushOnDynArray(&g->forwardDecls, &fldType->pointer.pointee);
                }
            }
            else {
                pushOnDynArray(&node->deps, &fldType);
            }
        }
        break;
    case irTypeFunc:
        for (u64 i = 0; i < type->func.count; i++) {
            addIrTypeToGraph(g, type->func.params[i]);
        }
        if (type->func.ret)
            addIrTypeToGraph(g, type->func.ret);
        break;
    default:
        break;
    }
}

static void collectTypesFromNode(ConstIrVisitor *visitor, const IrNode *node)
{
    IrTypeGraph *g = getConstIrVisitorContext(visitor);
    if (node->type)
        addIrTypeToGraph(g, node->type);

    switch (node->tag) {
    case irCast:
        if (node->cast.expr && node->cast.expr->type)
            addIrTypeToGraph(g, node->cast.expr->type);
        break;
    case irSizeof:
        if (node->sizeofVal.targetType)
            addIrTypeToGraph(g, node->sizeofVal.targetType);
        break;
    case irAlloca:
        if (node->alloca.elemType)
            addIrTypeToGraph(g, node->alloca.elemType);
        break;
    case irMalloc:
        if (node->malloc.elemType)
            addIrTypeToGraph(g, node->malloc.elemType);
        break;
    default:
        break;
    }

    irConstVisitFallbackVisitAll(visitor, node);
}

static void topoSortDfs(IrTypeGraph *g, const IrType *type)
{
    IrTypeGraphNode *node = findInHashTable(
        &g->nodes, &(IrTypeGraphNode){.type = type}, hashPtr(hashInit(), type),
        sizeof(IrTypeGraphNode), comparePointers);
    if (node == NULL || node->emitted)
        return;

    if (node->inStack)
        return; // Cycle detected: broken via forward pointer declarations

    node->inStack = true;

    dynArrayFor(dep, const IrType *, &node->deps) {
        topoSortDfs(g, *dep);
    }

    node->inStack = false;
    node->emitted = true;
    pushOnDynArray(&g->sorted, &type);
}

IrTypeGraph *newIrTypeGraph(MemPool *pool)
{
    IrTypeGraph *g = allocFromMemPool(pool, sizeof(IrTypeGraph));
    g->pool = pool;
    g->nodes = newHashTable(sizeof(IrTypeGraphNode), pool);
    g->sorted = newDynArray(sizeof(const IrType *));
    g->forwardDecls = newDynArray(sizeof(const IrType *));
    return g;
}

void freeIrTypeGraph(IrTypeGraph *g)
{
    if (g) {
        HashtableIt it = newHashTableIt(&g->nodes, sizeof(IrTypeGraphNode));
        while (hashTableItHasNext(&it)) {
            IrTypeGraphNode *node = hashTableItNext(&it);
            freeDynArray(&node->deps);
            freeDynArray(&node->pointerDeps);
        }
        freeHashTable(&g->nodes);
        freeDynArray(&g->sorted);
        freeDynArray(&g->forwardDecls);
    }
}

void buildIrTypeGraph(IrTypeGraph *g, const IrModule *module)
{
    if (module == NULL)
        return;

    // Pass 1: Walk globals and functions to collect all referenced types
    ConstIrVisitor collector = makeConstIrVisitor(g, {
        [irModule] = collectTypesFromNode,
        [irFunction] = collectTypesFromNode,
        [irFuncParam] = collectTypesFromNode,
        [irBasicBlock] = collectTypesFromNode,
        [irGlobalVar] = collectTypesFromNode,
        [irVarDecl] = collectTypesFromNode,
        [irAssign] = collectTypesFromNode,
        [irBranch] = collectTypesFromNode,
        [irBranchIf] = collectTypesFromNode,
        [irSwitch] = collectTypesFromNode,
        [irReturn] = collectTypesFromNode,
        [irCall] = collectTypesFromNode,
        [irCast] = collectTypesFromNode,
        [irMember] = collectTypesFromNode,
        [irIndex] = collectTypesFromNode,
        [irUnary] = collectTypesFromNode,
        [irBinary] = collectTypesFromNode,
        [irAddrOf] = collectTypesFromNode,
        [irDeref] = collectTypesFromNode,
        [irSizeof] = collectTypesFromNode
    }, .fallback = irConstVisitFallbackVisitAll);

    irConstVisit(&collector, module);

    // Pass 2: Topologically sort all collected types
    HashtableIt it = newHashTableIt(&g->nodes, sizeof(IrTypeGraphNode));
    while (hashTableItHasNext(&it)) {
        IrTypeGraphNode *node = hashTableItNext(&it);
        if (!node->emitted) {
            topoSortDfs(g, node->type);
        }
    }
}
