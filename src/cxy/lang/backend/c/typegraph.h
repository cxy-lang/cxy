#pragma once

#include "core/array.h"
#include "core/htable.h"
#include "core/mempool.h"
#include "lang/middle/ir/node.h"
#include "lang/middle/ir/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IrTypeGraphNode {
    const IrType *type;
    DynArray deps;        // By-value dependencies (must be fully defined before this type)
    DynArray pointerDeps; // Pointer dependencies (can be satisfied with forward declaration)
    bool visited;
    bool inStack;
    bool emitted;
} IrTypeGraphNode;

typedef struct IrTypeGraph {
    MemPool *pool;
    HashTable nodes;      // const IrType* -> IrTypeGraphNode*
    DynArray sorted;      // Topologically sorted const IrType*
    DynArray forwardDecls;// Types requiring forward declaration
} IrTypeGraph;

IrTypeGraph *newIrTypeGraph(MemPool *pool);
void freeIrTypeGraph(IrTypeGraph *g);

void addIrTypeToGraph(IrTypeGraph *g, const IrType *type);
void buildIrTypeGraph(IrTypeGraph *g, const IrModule *module);

#ifdef __cplusplus
}
#endif
