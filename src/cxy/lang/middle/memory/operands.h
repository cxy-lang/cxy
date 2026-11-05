#pragma once

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// The variable behind a moved operand (`x` in `<Base>x`), or NULL for a place
IrNode *findMovedVarDecl(const IrNode *src);

// The single definition of "this operand is a move": returns the slot holding an
// irMove, looking through casts (`<Base>(&&x)`), or NULL.
IrNode **moveSlot(IrNode **slot);

// Lowering marks every operand a consumer takes ownership of with
// `:owned-position`. This walks the operands under a node and calls `cb` with the
// slot of each marked one, so a move in any position is found, whatever holds it.
void forEachOwnedSlot(IrNode *node, void (*cb)(IrNode **slot, void *ctx), void *cbCtx);

// A member or an element
bool isPlaceNode(const IrNode *node);

bool isPointerLikeType(const IrType *type);

// A place whose owner can be moved out: it is cleared afterwards
bool isMovablePlace(const IrNode *node);

// The statement that empties `place`: null for a pointer or class, zeroed memory
// for a struct, NULL for anything else. Unmanaged, so the pass does not
// elaborate it again.
IrNode *makeClearPlace(MemPool *pool, const FileLoc *loc, IrNode *place);

// A temporary declaration for an owner the pass creates, named `<prefix><n>` and
// typed like `place`. It is not tracked until the analysis collects it.
IrNode *makeOwnedTemp(MemMgmtContext *ctx, cstring prefix, const IrNode *place);

// Replaces a `move src` operand by `src` itself and returns it
IrNode *stripMove(IrNode **moveRef);

// Puts `node` before `stmt` in `bb`. `prevRef` is the statement before `stmt`,
// or NULL, and becomes `node`.
void insertBeforeStmt(IrNode *bb, IrNode **prevRef, IrNode *stmt, IrNode *node);

// Puts `node` after `stmt`. `nextRef` is the statement after `stmt` and becomes `node`.
void insertAfterStmt(IrNode *stmt, IrNode **nextRef, IrNode *node);

#ifdef __cplusplus
}
#endif
