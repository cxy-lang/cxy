#include "classify.h"
#include "operands.h"

#include "core/hash.h"
#include "lang/middle/ir/visitor.h"

typedef struct TempUse {
    const IrNode *decl;
    const IrNode *defBlock;
    u32 uses;       // references, not counting the assignment that defines it
    u32 ownedUses;  // references in an owned position
    bool escapes;   // stored by an assignment, so it outlives the statement
    bool otherBlock; // referenced outside the block that declares it
} TempUse;

typedef struct UseScan {
    HashTable table;
    const IrNode *block;
    bool storedByAssign;
} UseScan;

static bool compareTempUse(const void *a, const void *b)
{
    return ((const TempUse *)a)->decl == ((const TempUse *)b)->decl;
}

static HashCode hashDecl(const IrNode *decl)
{
    return hashPtr(hashInit(), decl);
}

static TempUse *findUse(UseScan *scan, const IrNode *decl)
{
    TempUse probe = {.decl = decl};
    return findInHashTable(&scan->table, &probe, hashDecl(decl), sizeof(TempUse), compareTempUse);
}

static void declareTemporaries(UseScan *scan, IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            // one the pass created already has its class, a reference owns nothing
            if (stmt->tag != irVarDecl || !irHasInfo(stmt, irInfoSynthetic) ||
                irValueClass(stmt) != irClassNone || !isIrVarDestructible(stmt) ||
                (stmt->type && stmt->type->category == irTypeCatReference))
                continue;
            TempUse use = {.decl = stmt, .defBlock = bb};
            insertInHashTable(&scan->table, &use, hashDecl(stmt), sizeof(TempUse), compareTempUse);
        }
    }
}

static void scanOperand(UseScan *scan, IrNode *node, bool owned, bool stored);

static void scanSlot(IrNode **slot, void *scanPtr)
{
    scanOperand(scanPtr, *slot, false, false);
}

// `owned` and `stored` carry through casts: `<Base>t` is a use of `t` as a whole
static void scanOperand(UseScan *scan, IrNode *node, bool owned, bool stored)
{
    if (node == NULL)
        return;
    owned = owned || irHasInfo(node, irInfoOwnedPosition);

    if (node->tag == irCast) {
        scanOperand(scan, node->cast.expr, owned, stored);
        return;
    }
    if (node->tag == irVarRef) {
        TempUse *use = findUse(scan, node->varRef.target);
        if (use == NULL)
            return;
        use->uses++;
        if (owned)
            use->ownedUses++;
        else if (stored)
            use->escapes = true;
        if (scan->block != use->defBlock)
            use->otherBlock = true;
        return;
    }
    irForEachOperandSlot(node, scanSlot, scan);
}

static void scanStatement(UseScan *scan, IrNode *stmt)
{
    if (stmt->tag == irAssign) {
        // `t = value` defines `t`, only the value is a use. What an assignment
        // stores outlives the statement.
        IrNode *lhs = stmt->assign.lhs;
        bool defines = lhs && lhs->tag == irVarRef && findUse(scan, lhs->varRef.target);
        if (!defines)
            scanOperand(scan, lhs, false, false);
        scanOperand(scan, stmt->assign.rhs, false, true);
        return;
    }
    irForEachOperandSlot(stmt, scanSlot, scan);
}

static IrValueClass classOf(const TempUse *use)
{
    // the tag and payload of a `!T` call are read by the checks
    if (use->decl->type && use->decl->type->category == irTypeCatResult)
        return irClassResult;
    // reads before the single taker are fine, the owner is still handed over once
    if (use->ownedUses == 1)
        return irClassInFlight;
    if (use->ownedUses > 1)
        return irClassNone;
    if (use->uses == 0)
        return irClassStatement;
    if (use->escapes || use->otherBlock)
        return irClassExtended;
    return irClassStatement;
}

void classifyTemporaries(MemMgmtContext *ctx, IrNode *func)
{
    (void)ctx;
    UseScan scan = {.table = newTempHashTable(sizeof(TempUse))};
    declareTemporaries(&scan, func);
    if (scan.table.size == 0) {
        freeHashTable(&scan.table);
        return;
    }

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        scan.block = bb;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next)
            scanStatement(&scan, stmt);
    }

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            TempUse *use = stmt->tag == irVarDecl ? findUse(&scan, stmt) : NULL;
            if (use)
                irSetValueClass(stmt, classOf(use));
        }
    }
    freeHashTable(&scan.table);
}
