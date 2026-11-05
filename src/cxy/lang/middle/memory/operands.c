#include "operands.h"

#include "lang/middle/ir/visitor.h"

IrNode *findMovedVarDecl(const IrNode *src)
{
    src = unwrapCastsConst(src);
    if (src && src->tag == irVarRef)
        return src->varRef.target;
    return NULL;
}

IrNode **moveSlot(IrNode **slot)
{
    while (slot && *slot && (*slot)->tag == irCast)
        slot = &(*slot)->cast.expr;
    return (slot && *slot && (*slot)->tag == irMove) ? slot : NULL;
}

IrNode *stripMove(IrNode **moveRef)
{
    IrNode *mv = *moveRef;
    IrNode *src = mv->move.src;
    src->next = mv->next;
    *moveRef = src;
    return src;
}

void insertBeforeStmt(IrNode *bb, IrNode **prevRef, IrNode *stmt, IrNode *node)
{
    node->next = stmt;
    if (*prevRef)
        (*prevRef)->next = node;
    else
        bb->basicBlock.first = node;
    *prevRef = node;
}

void insertAfterStmt(IrNode *stmt, IrNode **nextRef, IrNode *node)
{
    node->next = *nextRef;
    stmt->next = node;
    *nextRef = node;
}

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

void forEachOwnedSlot(IrNode *node, void (*cb)(IrNode **slot, void *ctx), void *cbCtx)
{
    OwnedSlotWalk walk = {.cb = cb, .ctx = cbCtx};
    irForEachOperandSlot(node, walkOperandSlot, &walk);
}
