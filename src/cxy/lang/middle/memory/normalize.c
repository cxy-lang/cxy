#include "normalize.h"

// One call per rule goes here, in the order of the table in section 5
static void normalizeStmt(MemMgmtContext *ctx, IrNode *bb, IrNode **prevRef, IrNode *stmt)
{
    (void)ctx;
    (void)bb;
    (void)prevRef;
    (void)stmt;
}

void normalizeFunction(MemMgmtContext *ctx, IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;

        IrNode *prev = NULL;
        for (IrNode *stmt = bb->basicBlock.first; stmt;) {
            IrNode *next = stmt->next;
            normalizeStmt(ctx, bb, &prev, stmt);
            prev = stmt;
            stmt = next;
        }
    }
}
