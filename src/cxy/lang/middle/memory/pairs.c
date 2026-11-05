#include "pairs.h"
#include "operands.h"

#include "lang/middle/ir/visitor.h"

// One candidate: `copy` is `Copy a -> b`, both class handle variables
typedef struct Pair {
    IrNode *copy;
    const IrNode *a;
    const IrNode *b;
    DynArray drops;  // every `drop b`, IrNode *
    DynArray seen;   // blocks entered from their start, IrNode *
    DynArray work;   // blocks still to walk
    u32 bDefs;       // statements that give `b` a value
} Pair;

static const IrNode *plainVar(const IrNode *node)
{
    node = unwrapCastsConst(node);
    return node && node->tag == irVarRef ? node->varRef.target : NULL;
}

// A variable that owns its object with a count of its own: `a` has to, or the
// object could die while `b` still reads it
static bool isOwningVariable(const IrNode *decl)
{
    if (decl == NULL || (decl->tag != irVarDecl && decl->tag != irFuncParam))
        return false;
    IrValueClass cls = irValueClass(decl);
    return (cls == irClassNone || cls == irClassExtended) && decl->type &&
           decl->type->category == irTypeCatClass;
}

static bool isAliasTarget(const IrNode *decl)
{
    return decl && decl->tag == irVarDecl && isOwningVariable(decl) && !irIsReturnSlot(decl);
}

static bool isOneOf(const Pair *p, const IrNode *decl)
{
    return decl != NULL && (decl == p->a || decl == p->b);
}

// Does an operand under `node` change who owns `a` or `b`, or let it be changed
// behind our back: a move, a handle whose address is taken, an owned use of `b`
static bool operandHazard(const Pair *p, const IrNode *node);

typedef struct HazardScan {
    const Pair *pair;
    bool found;
} HazardScan;

static void hazardSlot(IrNode **slot, void *scanPtr)
{
    HazardScan *scan = scanPtr;
    if (!scan->found && operandHazard(scan->pair, *slot))
        scan->found = true;
}

static bool operandHazard(const Pair *p, const IrNode *node)
{
    if (node == NULL)
        return false;
    if (node->tag == irMove && isOneOf(p, plainVar(node->move.src)))
        return true;
    if (node->tag == irAddrOf && isOneOf(p, plainVar(node->addrOf.target)))
        return true;
    if (node->tag == irVarRef && isOneOf(p, node->varRef.target) &&
        (irHasInfo(node, irInfoMoved) || irHasInfo(node, irInfoOwnedPosition)))
        return true;

    HazardScan scan = {.pair = p};
    irForEachOperandSlot((IrNode *)node, hazardSlot, &scan);
    return scan.found;
}

// Everything a statement can do to `a` or `b` that an alias could not survive. The
// `drop b` that ends a path is handled by the caller.
static bool stmtHazard(const Pair *p, const IrNode *stmt)
{
    switch (stmt->tag) {
    case irAssign:
        if (isOneOf(p, plainVar(stmt->assign.lhs)))
            return true;
        return operandHazard(p, stmt->assign.rhs);
    case irCopy: {
        if (isOneOf(p, plainVar(stmt->copy.dst)))
            return true;
        // the source of a copy is read, the new owner takes its own count
        const IrNode *src = unwrapCastsConst(stmt->copy.src);
        if (src && src->tag == irVarRef)
            return isOneOf(p, src->varRef.target) && irHasInfo(src, irInfoMoved);
        return operandHazard(p, stmt->copy.src);
    }
    case irDrop:
        return isOneOf(p, plainVar(stmt->drop.target));
    case irZeromem:
        return isOneOf(p, plainVar(stmt->zeromem.target));
    case irMove:
        return isOneOf(p, plainVar(stmt->move.src));
    default:
        return operandHazard(p, stmt);
    }
}

static bool contains(const DynArray *array, const IrNode *node)
{
    for (u64 i = 0; i < array->size; i++) {
        if (dynArrayAt(const IrNode **, array, i) == node)
            return true;
    }
    return false;
}

static void pushOnce(DynArray *array, const IrNode *node)
{
    if (!contains(array, node))
        pushOnDynArray(array, &node);
}

static void pushBlock(Pair *p, const IrNode *bb)
{
    if (bb == NULL || bb->tag != irBasicBlock || contains(&p->seen, bb))
        return;
    pushOnDynArray(&p->seen, &bb);
    pushOnDynArray(&p->work, &bb);
}

// Walks from `first` to the end of every path. A path ends at a `drop b`, which is
// recorded. Any hazard before it, or a return that `b` was never dropped for, makes
// the pair unsafe.
static bool walkPath(Pair *p, const IrNode *first)
{
    for (const IrNode *s = first; s; s = s->next) {
        if (s->tag == irDrop && plainVar(s->drop.target) == p->b) {
            if (s->drop.flag != NULL)
                return false;
            pushOnce(&p->drops, s);
            return true;
        }
        if (stmtHazard(p, s))
            return false;

        switch (s->tag) {
        case irReturn:
            return false;
        case irUnreachable:
            return true;
        case irBranch:
            pushBlock(p, s->branch.target);
            return true;
        case irBranchIf:
            pushBlock(p, s->branchIf.trueTarget);
            pushBlock(p, s->branchIf.falseTarget);
            return true;
        case irSwitch:
            for (u64 i = 0; i < s->switchStmt.casesCount; i++)
                pushBlock(p, s->switchStmt.cases[i].target);
            pushBlock(p, s->switchStmt.defaultTarget);
            return true;
        default:
            break;
        }
    }
    return false;
}

static bool safeToAlias(Pair *p)
{
    bool ok = walkPath(p, p->copy->next);
    while (ok && p->work.size > 0) {
        const IrNode *bb = *(const IrNode **)popDynArray(&p->work);
        ok = walkPath(p, bb->basicBlock.first);
    }
    return ok && p->drops.size > 0;
}

// `b` has no other value than the one the copy gives it
static u32 countDefs(const IrNode *func, const IrNode *b)
{
    u32 defs = 0;
    for (const IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            if (s->tag == irAssign && plainVar(s->assign.lhs) == b)
                defs++;
            else if (s->tag == irCopy && plainVar(s->copy.dst) == b)
                defs++;
        }
    }
    return defs;
}

static void unlinkStmt(IrNode *func, const IrNode *stmt)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        IrNode *prev = NULL;
        for (IrNode *s = bb->basicBlock.first; s; prev = s, s = s->next) {
            if (s != stmt)
                continue;
            if (prev)
                prev->next = s->next;
            else
                bb->basicBlock.first = s->next;
            if (bb->basicBlock.last == s)
                bb->basicBlock.last = prev;
            return;
        }
    }
}

// `Copy a -> b` becomes `b = a`, nothing is retained
static void makeAlias(IrNode *func, Pair *p)
{
    IrNode *src = p->copy->copy.src;
    IrNode *dst = p->copy->copy.dst;
    irClearInfo(src, irInfoOwnedPosition);
    p->copy->tag = irAssign;
    p->copy->assign.lhs = dst;
    p->copy->assign.rhs = src;
    p->copy->flags |= flgUnmanaged;
    irSetInfo(p->copy, irInfoImplicit | irInfoSynthetic);

    for (u64 i = 0; i < p->drops.size; i++)
        unlinkStmt(func, dynArrayAt(const IrNode **, &p->drops, i));

    // `b` is a view of `a` now, the creator vouches for it
    irSetValueClass((IrNode *)p->b, irClassTransient);
}

static bool tryPair(IrNode *func, IrNode *copy)
{
    // `@copy` is the user asking for an owner, only copies the compiler made go
    if (irHasInfo(copy, irInfoUserWritten))
        return false;
    const IrNode *a = plainVar(copy->copy.src);
    const IrNode *b = plainVar(copy->copy.dst);
    if (!isOwningVariable(a) || !isAliasTarget(b) || a == b)
        return false;
    // the source must be the handle itself, a cast to a reference is a borrow
    const IrNode *srcNode = unwrapCastsConst(copy->copy.src);
    if (srcNode->type == NULL || srcNode->type->category != irTypeCatClass)
        return false;
    if (countDefs(func, b) != 1)
        return false;

    Pair pair = {.copy = copy,
                 .a = a,
                 .b = b,
                 .drops = newDynArray(sizeof(const IrNode *)),
                 .seen = newDynArray(sizeof(const IrNode *)),
                 .work = newDynArray(sizeof(const IrNode *))};
    bool ok = safeToAlias(&pair);
    if (ok)
        makeAlias(func, &pair);
    freeDynArray(&pair.drops);
    freeDynArray(&pair.seen);
    freeDynArray(&pair.work);
    return ok;
}

void eliminateRetainReleasePairs(MemMgmtContext *ctx, IrNode *func)
{
    (void)ctx;
    DynArray copies = newDynArray(sizeof(IrNode *));
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *s = bb->basicBlock.first; s; s = s->next) {
            if (irIsRetain(s))
                pushOnDynArray(&copies, &s);
        }
    }
    // The last copy first: in `var c = b` after `var b = a`, `c` becomes an alias of
    // `b` while `b` still owns, then `b` an alias of `a`
    for (u64 i = copies.size; i > 0; i--)
        tryPair(func, dynArrayAt(IrNode **, &copies, i - 1));
    freeDynArray(&copies);
}
