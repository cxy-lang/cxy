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

bool isPlaceNode(const IrNode *node)
{
    return node && (node->tag == irMember || node->tag == irIndex);
}

bool isPointerLikeType(const IrType *type)
{
    return type && (type->tag == irTypePointer || type->category == irTypeCatClass);
}

bool isMovablePlace(const IrNode *node)
{
    return isPlaceNode(node) && (isPointerLikeType(node->type) || isIrTypeDestructible(node->type));
}

IrNode *makeClearPlace(MemPool *pool, const FileLoc *loc, IrNode *place)
{
    IrNode *clear = NULL;
    if (isPointerLikeType(place->type))
        clear = makeIrAssign(pool, loc, place, makeIrNullConst(pool, loc, place->type));
    else if (isIrTypeDestructible(place->type))
        clear = makeIrZeromem(pool, loc, place);
    if (clear)
        clear->flags |= flgUnmanaged;
    return clear;
}

IrNode *makeOwnedTemp(MemMgmtContext *ctx, cstring prefix, const IrNode *place)
{
    cstring name = makeStringf(ctx->strings, "%s%u", prefix, ctx->tempCounter++);
    return makeIrVarDecl(
        ctx->pool, &place->loc, flgTemporary, name, name, place->type, ctx->currentScopeId);
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
