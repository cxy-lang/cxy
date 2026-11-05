#include "operands.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/ir/visitor.h"

IrNode *findVarDeclInNode(const IrNode *node)
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

bool isIrVarDestructible(const IrNode *decl)
{
    if (decl == NULL || irIsReturnSlot(decl))
        return false;
    return hasFlag(decl, ReferenceMembers) || isIrTypeDestructible(decl->type);
}

bool isUnwrappedCallResult(const IrNode *src)
{
    IrNode *decl = findVarDeclInNode(src);
    return decl && irValueClass(decl) == irClassResult;
}

bool isResultPayloadPlace(const IrNode *node)
{
    if (node == NULL || node->tag != irMember || node->member.index != 0 ||
        node->member.name != NULL || !isIrTypeDestructible(node->type))
        return false;
    const IrNode *val = node->member.target;
    if (val == NULL || val->tag != irMember || val->member.index != 1 || val->member.name != S_val)
        return false;
    const IrNode *root = val->member.target;
    return root && root->tag == irVarRef && root->varRef.target &&
           irValueClass(root->varRef.target) == irClassResult;
}

bool isOwnerConsumingValue(const IrNode *rhs)
{
    return rhs->tag == irCall || rhs->tag == irUnionValue || rhs->tag == irOptionalValue ||
           rhs->tag == irStructConst || rhs->tag == irArrayConst || rhs->tag == irClosureValue;
}

IrNode *rebuildCastChain(MemPool *pool, const IrNode *castTree, IrNode *newRoot)
{
    if (castTree == NULL || castTree->tag != irCast)
        return newRoot;
    IrNode *inner = rebuildCastChain(pool, castTree->cast.expr, newRoot);
    return makeIrCast(pool, &castTree->loc, inner, castTree->type);
}

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
    IrNode *decl = makeIrVarDecl(
        ctx->pool, &place->loc, flgNone, name, name, place->type, ctx->currentScopeId);
    irSetInfo(decl, irInfoSynthetic);
    irSetValueClass(decl, irClassInFlight);
    return decl;
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
