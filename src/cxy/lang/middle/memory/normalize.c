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
// that uses it, and the operand is `x`, marked `:moved` so it is not copied.
static void hoistVariableMove(IrNode **slot, void *cursor)
{
    NormalizeCursor *c = cursor;
    IrNode **move = moveSlot(slot);
    if (move == NULL)
        return;
    IrNode *decl = findMovedVarDecl((*move)->move.src);
    if (decl == NULL)
        return;

    MemPool *pool = c->ctx->pool;
    const FileLoc *loc = &(*move)->loc;
    IrNode *moved = makeIrMove(pool, loc, makeIrVarRef(pool, loc, decl));
    stripMove(move);
    irSetInfo(*slot, irInfoOwnedPosition | irInfoMoved);
    insertBeforeStmt(c->bb, &c->prev, c->stmt, moved);
}

static void normalizeStmt(NormalizeCursor *c)
{
    IrNode *stmt = c->stmt;
    if (stmt->tag == irCall) {
        dropBorrowedMoves(stmt);
        forEachOwnedSlot(stmt, hoistVariableMove, c);
    }
    else if (stmt->tag == irAssign && !hasFlag(stmt, Unmanaged)) {
        IrNode *rhs = stmt->assign.rhs;
        if (rhs && rhs->tag == irCall)
            dropBorrowedMoves(rhs);
        forEachOwnedSlot(stmt, hoistVariableMove, c);
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
