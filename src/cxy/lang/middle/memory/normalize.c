#include "normalize.h"
#include "operands.h"

#include "lang/frontend/flag.h"

typedef struct NormalizeCursor {
    MemMgmtContext *ctx;
    IrNode *bb;
    IrNode *prev;
    IrNode *stmt;
} NormalizeCursor;

// N4: a `&&x` for a parameter that only borrows is not a move, `x` stays owned,
// so the move goes and the operand is the borrow itself
static void dropBorrowedMoves(IrNode *call)
{
    for (IrNode **arg = &call->call.args; *arg; arg = &(*arg)->next) {
        if (irHasInfo(*arg, irInfoOwnedPosition))
            continue;
        IrNode **move = moveSlot(arg);
        if (move)
            stripMove(move);
    }
}

// N1: `&&x` for a variable becomes the statement `move x` before the statement
// that uses it, and the operand is `x`
static void hoistVariableMove(NormalizeCursor *c, IrNode **move, IrNode *decl)
{
    MemPool *pool = c->ctx->pool;
    const FileLoc *loc = &(*move)->loc;
    IrNode *moved = makeIrMove(pool, loc, makeIrVarRef(pool, loc, decl));
    stripMove(move);
    insertBeforeStmt(c->bb, &c->prev, c->stmt, moved);
}

// N2: `&&p.f` becomes `__mv = p.f; p.f = null` before the statement, and the
// operand is `__mv`. The place cannot be killed as a variable, it is emptied after
// its value is read.
static void captureMovedPlace(NormalizeCursor *c, IrNode **move, IrNode *src)
{
    MemPool *pool = c->ctx->pool;
    IrNode *temp = makeOwnedTemp(c->ctx, "__mv", src);
    IrNode *capture = makeIrAssign(pool, &src->loc, makeIrVarRef(pool, &src->loc, temp), src);
    capture->flags |= flgUnmanaged;
    IrNode *clear = makeClearPlace(pool, &c->stmt->loc, cloneIrNode(pool, src));

    insertBeforeStmt(c->bb, &c->prev, c->stmt, temp);
    insertBeforeStmt(c->bb, &c->prev, c->stmt, capture);
    insertBeforeStmt(c->bb, &c->prev, c->stmt, clear);

    IrNode *ref = makeIrVarRef(pool, &src->loc, temp);
    ref->next = (*move)->next;
    *move = ref;
}

// What is left of a `&&` is the value itself, the operand is marked `:moved` so
// it is not copied
static void normalizeMovedOperand(IrNode **slot, void *cursor)
{
    NormalizeCursor *c = cursor;
    IrNode **move = moveSlot(slot);
    if (move == NULL)
        return;

    IrNode *src = (*move)->move.src;
    IrNode *decl = findMovedVarDecl(src);
    if (decl)
        hoistVariableMove(c, move, decl);
    else if (isMovablePlace(src))
        captureMovedPlace(c, move, src);
    else
        stripMove(move);
    irSetInfo(*slot, irInfoOwnedPosition | irInfoMoved);
}

static void normalizeStmt(NormalizeCursor *c)
{
    IrNode *stmt = c->stmt;
    if (stmt->tag == irCall) {
        dropBorrowedMoves(stmt);
        forEachOwnedSlot(stmt, normalizeMovedOperand, c);
    }
    else if (stmt->tag == irAssign && !hasFlag(stmt, Unmanaged)) {
        IrNode *rhs = stmt->assign.rhs;
        if (rhs && rhs->tag == irCall)
            dropBorrowedMoves(rhs);
        forEachOwnedSlot(stmt, normalizeMovedOperand, c);
    }
}

void normalizeFunction(MemMgmtContext *ctx, IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;

        NormalizeCursor c = {.ctx = ctx, .bb = bb};
        for (IrNode *stmt = bb->basicBlock.first; stmt;) {
            IrNode *next = stmt->next;
            c.stmt = stmt;
            normalizeStmt(&c);
            c.prev = stmt;
            stmt = next;
        }
    }
}
