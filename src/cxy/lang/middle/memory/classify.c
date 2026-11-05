#include "classify.h"
#include "operands.h"

#include "core/hash.h"
#include "lang/frontend/flag.h"
#include "lang/middle/ir/visitor.h"

typedef struct TempUse {
    const IrNode *decl;
    IrNode *defBlock;   // the block of the assignment that defines it
    IrNode *defStmt;
    IrNode *useBlock;   // the block of the uses, when they share one
    IrNode *lastUse;    // the last statement that refers to it, `defStmt` if none
    u32 defs;
    u32 uses;           // references, not counting the assignment that defines it
    u32 ownedUses;      // references in an owned position
    bool escapes;       // stored by an assignment, so it outlives the statement
    bool otherBlock;    // referenced from more than one block
} TempUse;

typedef struct UseScan {
    MemMgmtContext *ctx;
    HashTable table;
    IrNode *block;
    IrNode *stmt;
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

// The temporaries that have the class `wanted`. None are the ones lowering made, a
// pass created the others and gave them a class already.
static void declareTemporaries(UseScan *scan, IrNode *func, IrValueClass wanted)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            // a reference owns nothing
            if (stmt->tag != irVarDecl || !irHasInfo(stmt, irInfoSynthetic) ||
                irValueClass(stmt) != wanted || !isIrVarDestructible(stmt) ||
                (stmt->type && stmt->type->category == irTypeCatReference))
                continue;
            TempUse use = {.decl = stmt};
            insertInHashTable(&scan->table, &use, hashDecl(stmt), sizeof(TempUse), compareTempUse);
        }
    }
}

static void scanOperand(UseScan *scan, IrNode *node, bool owned, bool stored);

static void scanSlot(IrNode **slot, void *scanPtr)
{
    scanOperand(scanPtr, *slot, false, false);
}

static void recordUse(UseScan *scan, TempUse *use, bool owned, bool stored)
{
    use->uses++;
    if (owned)
        use->ownedUses++;
    else if (stored)
        use->escapes = true;
    if (use->useBlock == NULL)
        use->useBlock = scan->block;
    else if (use->useBlock != scan->block)
        use->otherBlock = true;
    use->lastUse = scan->stmt;
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
        if (use)
            recordUse(scan, use, owned, stored);
        return;
    }
    irForEachOperandSlot(node, scanSlot, scan);
}

static void scanStatement(UseScan *scan, IrNode *stmt)
{
    scan->stmt = stmt;
    if (stmt->tag == irAssign) {
        // `t = value` defines `t`, only the value is a use. What an assignment
        // stores outlives the statement.
        IrNode *lhs = stmt->assign.lhs;
        TempUse *defined = lhs && lhs->tag == irVarRef ? findUse(scan, lhs->varRef.target) : NULL;
        if (defined) {
            if (defined->defs++ == 0) {
                defined->defBlock = scan->block;
                defined->defStmt = stmt;
                defined->lastUse = stmt;
            }
        }
        else {
            scanOperand(scan, lhs, false, false);
        }
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
    // it must be made and used in one block to end with its statement
    if (use->defs != 1 || use->escapes || use->otherBlock ||
        (use->uses > 0 && use->useBlock != use->defBlock))
        return irClassExtended;
    return irClassStatement;
}

static bool isTerminatorWithOperand(const IrNode *stmt)
{
    return stmt->tag == irBranchIf || stmt->tag == irSwitch || stmt->tag == irReturn;
}

static IrNode **terminatorOperand(IrNode *stmt)
{
    if (stmt->tag == irBranchIf)
        return &stmt->branchIf.cond;
    if (stmt->tag == irSwitch)
        return &stmt->switchStmt.cond;
    return &stmt->returnStmt.value;
}

typedef struct TerminatorReads {
    UseScan *scan;
    IrNode *terminator;
    u32 count;
} TerminatorReads;

static void markStatementReads(IrNode **slot, void *readsPtr);

static void markStatementOperand(TerminatorReads *reads, IrNode *node)
{
    if (node == NULL)
        return;
    if (node->tag == irVarRef) {
        TempUse *use = findUse(reads->scan, node->varRef.target);
        if (use && irValueClass(use->decl) == irClassStatement && use->lastUse == reads->terminator)
            reads->count++;
        return;
    }
    irForEachOperandSlot(node, markStatementReads, reads);
}

static void markStatementReads(IrNode **slot, void *readsPtr)
{
    markStatementOperand(readsPtr, *slot);
}

typedef struct HoistedReads {
    UseScan *scan;
    IrNode *terminator;
    IrNode *assign;
} HoistedReads;

static void moveLastUse(IrNode **slot, void *hoistPtr);

static void moveLastUseOf(HoistedReads *hoist, IrNode *node)
{
    if (node == NULL)
        return;
    if (node->tag == irVarRef) {
        TempUse *use = findUse(hoist->scan, node->varRef.target);
        if (use && use->lastUse == hoist->terminator)
            use->lastUse = hoist->assign;
        return;
    }
    irForEachOperandSlot(node, moveLastUse, hoist);
}

static void moveLastUse(IrNode **slot, void *hoistPtr)
{
    moveLastUseOf(hoistPtr, *slot);
}

// A branch reads its condition after the statement it ends, so the condition is
// computed into a value first: `__v = cond; branch __v`. False when the
// condition has no type to declare the value with.
static bool hoistTerminatorOperand(UseScan *scan, IrNode *bb, IrNode *prev, IrNode *terminator)
{
    IrNode **slot = terminatorOperand(terminator);
    IrNode *operand = *slot;
    if (operand == NULL || operand->type == NULL)
        return false;

    MemMgmtContext *ctx = scan->ctx;
    cstring name = makeStringf(ctx->strings, "__v%u", ctx->tempCounter++);
    IrNode *value = makeIrVarDecl(
        ctx->pool, &operand->loc, flgNone, name, name, operand->type, ctx->currentScopeId);
    irSetInfo(value, irInfoSynthetic);
    IrNode *store = makeIrAssign(
        ctx->pool, &operand->loc, makeIrVarRef(ctx->pool, &operand->loc, value), operand);

    HoistedReads hoist = {.scan = scan, .terminator = terminator, .assign = store};
    moveLastUseOf(&hoist, operand);
    *slot = makeIrVarRef(ctx->pool, &operand->loc, value);
    (*slot)->next = operand->next;
    operand->next = NULL;

    insertBeforeStmt(bb, &prev, terminator, value);
    insertBeforeStmt(bb, &prev, terminator, store);
    return true;
}

// Statement temporaries read by a terminator: the terminator is not where they die
static void hoistTerminators(UseScan *scan, IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        IrNode *prev = NULL;
        for (IrNode *stmt = bb->basicBlock.first; stmt; prev = stmt, stmt = stmt->next) {
            if (!isTerminatorWithOperand(stmt))
                continue;
            TerminatorReads reads = {.scan = scan, .terminator = stmt};
            markStatementOperand(&reads, *terminatorOperand(stmt));
            if (reads.count > 0)
                hoistTerminatorOperand(scan, bb, prev, stmt);
        }
    }
}

// What a temporary is used for fixes how long it lives. A statement temporary
// is dropped after the last statement that reads it, and is never live across
// statements for the analysis.
static bool dropAfterLastUse(TempUse *use, MemMgmtContext *ctx)
{
    IrNode *anchor = use->lastUse;
    IrNode *bb = use->defBlock;
    if (anchor == NULL || bb == NULL || isTerminatorWithOperand(anchor))
        return false;

    IrNode *decl = (IrNode *)use->decl;
    decl->flags |= flgEphemeral;
    IrNode *drop = makeIrDrop(ctx->pool, &anchor->loc, makeIrVarRef(ctx->pool, &anchor->loc, decl));
    drop->flags |= flgUnmanaged;
    drop->next = anchor->next;
    anchor->next = drop;
    if (bb->basicBlock.last == anchor)
        bb->basicBlock.last = drop;
    return true;
}

// The uses of the temporaries of class `wanted`. False when there are none.
static bool scanFunction(UseScan *scan, IrNode *func, IrValueClass wanted)
{
    declareTemporaries(scan, func, wanted);
    if (scan->table.size == 0)
        return false;

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        scan->block = bb;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next)
            scanStatement(scan, stmt);
    }
    return true;
}

// What a temporary is used for fixes its class, and the class its flags: an
// in-flight owner or a result is `Temporary`, nobody drops it but its consumer
void classifyTemporaries(MemMgmtContext *ctx, IrNode *func)
{
    UseScan scan = {.ctx = ctx, .table = newTempHashTable(sizeof(TempUse))};
    if (scanFunction(&scan, func, irClassNone)) {
        for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
            if (bb->tag != irBasicBlock)
                continue;
            for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
                TempUse *use = stmt->tag == irVarDecl ? findUse(&scan, stmt) : NULL;
                if (use == NULL)
                    continue;
                IrValueClass cls = classOf(use);
                irSetValueClass(stmt, cls);
                if (cls == irClassInFlight || cls == irClassResult)
                    stmt->flags |= flgTemporary;
            }
        }
    }
    freeHashTable(&scan.table);
}

void dropStatementTemporaries(MemMgmtContext *ctx, IrNode *func)
{
    UseScan scan = {.ctx = ctx, .table = newTempHashTable(sizeof(TempUse))};
    if (scanFunction(&scan, func, irClassStatement)) {
        hoistTerminators(&scan, func);

        for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
            if (bb->tag != irBasicBlock)
                continue;
            for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
                TempUse *use = stmt->tag == irVarDecl ? findUse(&scan, stmt) : NULL;
                // one that cannot end with its statement lives to the end of its scope
                if (use && !dropAfterLastUse(use, ctx))
                    irSetValueClass(stmt, irClassExtended);
            }
        }
    }
    freeHashTable(&scan.table);
}
