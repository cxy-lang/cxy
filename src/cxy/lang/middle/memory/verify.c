#include "verify.h"

#include "core/array.h"
#include "core/hash.h"
#include "core/mempool.h"
#include "lang/frontend/flag.h"
#include "lang/middle/ir/visitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Events: what a statement does to the ownership of the tracked variables
// ---------------------------------------------------------------------------

typedef enum {
    oevDecl,       // var declaration, the previous value must be gone
    oevDef,        // the variable now owns a value
    oevClear,      // the variable now holds nothing (null, empty optional)
    oevMove,       // `&&x`
    oevUse,        // any other read
    oevDrop,       // flagIndex: -1 unconditional, -2 unknown flag, else drop-if
    oevFlagSet,    // `__drop_flags.i = v`, flagValue 0, 1 or 2 (unknown)
    oevFlagsClear, // `zeromem __drop_flags`
    oevReturn,
    oevMovePart,   // `&&x.f`, the field must be cleared before x is dropped
    oevZeroPart,   // a field of x is cleared or refilled
    oevEscape,     // decl: a local whose borrow is returned
    oevBind,       // decl: a reference variable, other: the owner it borrows
    oevRefUse,     // decl: a reference variable is read
    oevRefMove     // decl: a reference variable under a `&&` the user wrote
} OwnEventKind;

typedef struct OwnEvent {
    OwnEventKind kind;
    const IrNode *decl;
    const IrNode *other;
    const FileLoc *loc;
    i32 flagIndex;
    i8 flagValue;
} OwnEvent;

typedef struct StmtEntry {
    const IrNode *stmt;
    OwnEvent *events;
    u32 count;
} StmtEntry;

// What a block held when the snapshot was taken, in order. A statement the pass
// removes is not in the block afterwards, its events are still the block's.
typedef struct BlockOrder {
    const IrNode *bb;
    StmtEntry *stmts;
    u32 count;
} BlockOrder;

struct OwnershipSnapshot {
    MemPool pool;
    HashTable stmts;
    HashTable blocks;
};

typedef struct Collector {
    DynArray events;
    const IrNode *flagDecl;
} Collector;

static const IrNode *stripCasts(const IrNode *node)
{
    while (node && node->tag == irCast)
        node = node->cast.expr;
    return node;
}

// A variable or parameter the user declared. A temporary the compiler made, and
// the slot a function returns through, are not variables the user can misuse.
static bool isUserVariable(const IrNode *decl)
{
    if (decl == NULL || (decl->tag != irVarDecl && decl->tag != irFuncParam))
        return false;
    return !irHasInfo(decl, irInfoSynthetic);
}

// An owner the memory pass drops at scope exit, whoever made it: a variable of the
// user, a closure environment on the stack, a temporary that outlives its statement.
// The class tells, the other classes are consumed, dropped by a statement or returned.
static bool isManagedOwner(const IrNode *decl)
{
    if (decl == NULL || (decl->tag != irVarDecl && decl->tag != irFuncParam))
        return false;
    IrValueClass cls = irValueClass(decl);
    return cls == irClassNone || cls == irClassExtended;
}

static bool isTrackedDecl(const IrNode *decl)
{
    return isManagedOwner(decl) &&
           (hasFlag(decl, ReferenceMembers) || isIrTypeDestructible(decl->type));
}

static const IrNode *trackedVarRef(const IrNode *node)
{
    node = stripCasts(node);
    if (node && node->tag == irVarRef && isTrackedDecl(node->varRef.target))
        return node->varRef.target;
    return NULL;
}

static void emit(Collector *c,
                 OwnEventKind kind,
                 const IrNode *decl,
                 const FileLoc *loc,
                 i32 flagIndex,
                 i8 flagValue)
{
    OwnEvent ev = {
        .kind = kind, .decl = decl, .loc = loc, .flagIndex = flagIndex, .flagValue = flagValue};
    pushOnDynArray(&c->events, &ev);
}

static void emitBind(Collector *c, const IrNode *ref, const IrNode *owner, const FileLoc *loc)
{
    OwnEvent ev = {.kind = oevBind, .decl = ref, .other = owner, .loc = loc};
    pushOnDynArray(&c->events, &ev);
}

// The local a place is stored in, looking through fields and array elements.
// Stops at raw pointers and references: what they point to has another owner.
static const IrNode *rootOf(const IrNode *node)
{
    while (node) {
        const IrNode *target = NULL;
        switch (node->tag) {
        case irCast:
            node = node->cast.expr;
            continue;
        case irVarRef:
            return node->varRef.target;
        case irMember:
            target = node->member.target;
            break;
        case irIndex:
            target = node->index.target;
            if (target && target->type && target->type->tag != irTypeArray)
                return NULL;
            break;
        default:
            return NULL;
        }
        const IrType *type = target ? target->type : NULL;
        if (type && type->tag == irTypePointer && type->category != irTypeCatClass)
            return NULL;
        node = target;
    }
    return NULL;
}

// A local that owns its storage: returning a borrow of it dangles
static bool isOwningStorage(const IrNode *decl)
{
    if (decl == NULL || (decl->tag != irVarDecl && decl->tag != irFuncParam))
        return false;
    if (irIsConsumedTemp(decl) || irIsStatementTemp(decl) || irIsReturnSlot(decl))
        return false;
    return decl->type && decl->type->category != irTypeCatReference;
}

// The local `rhs` borrows (`&x`), or NULL. A class `&x` is a reference-typed
// cast of the handle, anything else is the address of the storage.
static const IrNode *borrowedLocal(const IrNode *rhs)
{
    bool viaRefCast = false;
    const IrNode *n = rhs;
    while (n && n->tag == irCast) {
        if (n->type && n->type->category == irTypeCatReference)
            viaRefCast = true;
        n = n->cast.expr;
    }
    if (n == NULL)
        return NULL;

    const IrNode *root = NULL;
    if (n->tag == irAddrOf)
        root = rootOf(n->addrOf.target);
    else if (viaRefCast && isTrackedDecl(rootOf(n)))
        root = rootOf(n);
    return isOwningStorage(root) ? root : NULL;
}

static bool isReferenceVar(const IrNode *decl)
{
    return decl && decl->tag == irVarDecl && isUserVariable(decl) && decl->type &&
           decl->type->category == irTypeCatReference;
}

// A variable or a parameter of reference type
static bool isReferenceDecl(const IrNode *decl)
{
    return decl && (decl->tag == irVarDecl || decl->tag == irFuncParam) &&
           decl->type && decl->type->category == irTypeCatReference;
}

static const IrNode *flagMemberDecl(const Collector *c, const IrNode *node)
{
    node = stripCasts(node);
    if (node == NULL || node->tag != irMember || c->flagDecl == NULL)
        return NULL;
    const IrNode *target = stripCasts(node->member.target);
    if (target && target->tag == irVarRef && target->varRef.target == c->flagDecl)
        return target->varRef.target;
    return NULL;
}

// An operand marked `:moved` is the value a `move x` statement before it already
// took, not a read of `x`
static void visitMovedCast(IrVisitor *v, IrNode *node)
{
    if (!irHasInfo(node, irInfoMoved))
        irVisitFallbackVisitAll(v, node);
}

static void visitVarRef(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    if (irHasInfo(node, irInfoMoved))
        return;
    // a `&&` on a reference that normalize turned into a borrow keeps the bit
    if (irHasInfo(node, irInfoUserWritten) && isReferenceDecl(node->varRef.target)) {
        emit(c, oevRefMove, node->varRef.target, &node->loc, 0, 0);
        return;
    }
    if (isTrackedDecl(node->varRef.target))
        emit(c, oevUse, node->varRef.target, &node->loc, 0, 0);
    else if (isReferenceVar(node->varRef.target))
        emit(c, oevRefUse, node->varRef.target, &node->loc, 0, 0);
}

static void visitMove(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    // Only a `&&` the user wrote: a move the compiler inserts is not theirs
    const IrNode *moved = stripCasts(node->move.src);
    if (irHasInfo(node, irInfoUserWritten) && moved && moved->tag == irVarRef &&
        isReferenceDecl(moved->varRef.target)) {
        emit(c, oevRefMove, moved->varRef.target, &node->loc, 0, 0);
        return;
    }
    const IrNode *decl = trackedVarRef(node->move.src);
    if (decl) {
        emit(c, oevMove, decl, &node->loc, 0, 0);
        return;
    }
    // A move out of a field or an element leaves the root alive but holed
    const IrNode *root = rootOf(node->move.src);
    if (isTrackedDecl(root))
        emit(c, oevMovePart, root, &node->loc, 0, 0);
    irVisit(v, node->move.src);
}

static bool isEmptyValue(const IrNode *rhs)
{
    rhs = stripCasts(rhs);
    return rhs && (rhs->tag == irNullConst ||
                   (rhs->tag == irOptionalValue && !rhs->optionalValue.hasValue));
}

static void visitAssign(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    irVisit(v, node->assign.rhs);

    // normalize reads a moved field into a temporary before it clears the field
    if (irHasInfo(node, irInfoImplicit)) {
        const IrNode *captured = rootOf(node->assign.rhs);
        if (isTrackedDecl(captured))
            emit(c, oevMovePart, captured, &node->loc, 0, 0);
    }

    IrNode *lhs = node->assign.lhs;
    const IrNode *lhsRoot = rootOf(lhs);
    if (lhsRoot && irIsReturnSlot(lhsRoot)) {
        const IrNode *borrowed = borrowedLocal(node->assign.rhs);
        if (borrowed)
            emit(c, oevEscape, borrowed, &node->loc, 0, 0);
    }

    const IrNode *decl = trackedVarRef(lhs);
    if (decl) {
        emit(c, isEmptyValue(node->assign.rhs) ? oevClear : oevDef, decl, &node->loc, 0, 0);
        return;
    }

    // `var r = &x` ties r to x. Any other source makes r unknown (no owner).
    const IrNode *bare = stripCasts(lhs);
    if (bare && bare->tag == irVarRef && isReferenceVar(bare->varRef.target)) {
        const IrNode *owner = borrowedLocal(node->assign.rhs);
        emitBind(c, bare->varRef.target, isTrackedDecl(owner) ? owner : NULL, &node->loc);
        return;
    }

    if (flagMemberDecl(c, lhs)) {
        const IrNode *member = stripCasts(lhs);
        const IrNode *rhs = stripCasts(node->assign.rhs);
        i8 value = 2;
        if (rhs && rhs->tag == irIntConst)
            value = rhs->intConst.value ? 1 : 0;
        emit(c, oevFlagSet, NULL, &node->loc, (i32)member->member.index, value);
        return;
    }
    if (isTrackedDecl(lhsRoot))
        emit(c, oevZeroPart, lhsRoot, &node->loc, 0, 0);
    irVisit(v, lhs);
}

static void visitCopy(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    irVisit(v, node->copy.src);
    const IrNode *decl = trackedVarRef(node->copy.dst);
    if (decl) {
        emit(c, oevDef, decl, &node->loc, 0, 0);
        return;
    }
    const IrNode *root = rootOf(node->copy.dst);
    if (isTrackedDecl(root))
        emit(c, oevZeroPart, root, &node->loc, 0, 0);
    irVisit(v, node->copy.dst);
}

static void visitDrop(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    const IrNode *decl = trackedVarRef(node->drop.target);
    if (decl == NULL) {
        irVisit(v, node->drop.target);
        return;
    }

    i32 flagIndex = -1;
    if (node->drop.flag) {
        const IrNode *flag = stripCasts(node->drop.flag);
        flagIndex = (flagMemberDecl(c, flag) != NULL) ? (i32)flag->member.index : -2;
    }
    emit(c, oevDrop, decl, &node->loc, flagIndex, 0);
}

static void visitZeromem(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    const IrNode *target = stripCasts(node->zeromem.target);
    if (c->flagDecl && target && target->tag == irVarRef &&
        target->varRef.target == c->flagDecl) {
        emit(c, oevFlagsClear, NULL, &node->loc, 0, 0);
        return;
    }
    const IrNode *root = rootOf(node->zeromem.target);
    if (isTrackedDecl(root) && trackedVarRef(node->zeromem.target) == NULL)
        emit(c, oevZeroPart, root, &node->loc, 0, 0);
    irVisit(v, node->zeromem.target);
}

static void visitReturn(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    irVisit(v, node->returnStmt.value);
    const IrNode *borrowed = borrowedLocal(node->returnStmt.value);
    if (borrowed)
        emit(c, oevEscape, borrowed, &node->loc, 0, 0);
    emit(c, oevReturn, NULL, &node->loc, 0, 0);
}

static void visitVarDecl(IrVisitor *v, IrNode *node)
{
    Collector *c = getIrVisitorContext(v);
    if (isTrackedDecl(node))
        emit(c, oevDecl, node, &node->loc, 0, 0);
}

// Every statement goes through the generic walk, so a move nested at any depth
// is seen, whatever the shape around it.
static void collectEvents(Collector *c, IrNode *stmt)
{
    IrVisitor visitor = makeIrVisitor(c,
                                      {
                                          [irVarRef] = visitVarRef,
                                          [irCast] = visitMovedCast,
                                          [irMove] = visitMove,
                                          [irAssign] = visitAssign,
                                          [irCopy] = visitCopy,
                                          [irDrop] = visitDrop,
                                          [irZeromem] = visitZeromem,
                                          [irReturn] = visitReturn,
                                          [irVarDecl] = visitVarDecl,
                                      });
    visitor.fallback = irVisitFallbackVisitAll;
    clearDynArray(&c->events);
    irVisit(&visitor, stmt);
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

static bool compareStmtEntryKey(const void *a, const void *b)
{
    return ((const StmtEntry *)a)->stmt == b;
}

static bool compareStmtEntries(const void *a, const void *b)
{
    return ((const StmtEntry *)a)->stmt == ((const StmtEntry *)b)->stmt;
}

static StmtEntry *findStmtEntry(const HashTable *table, const IrNode *stmt)
{
    return findInHashTable(
        table, stmt, hashPtr(hashInit(), stmt), sizeof(StmtEntry), compareStmtEntryKey);
}

static bool compareBlockOrderKey(const void *a, const void *b)
{
    return ((const BlockOrder *)a)->bb == b;
}

static bool compareBlockOrders(const void *a, const void *b)
{
    return ((const BlockOrder *)a)->bb == ((const BlockOrder *)b)->bb;
}

static const BlockOrder *findBlockOrder(const HashTable *table, const IrNode *bb)
{
    return findInHashTable(
        table, bb, hashPtr(hashInit(), bb), sizeof(BlockOrder), compareBlockOrderKey);
}

OwnershipSnapshot *ownershipSnapshot(IrNode *module)
{
    if (module == NULL)
        return NULL;

    OwnershipSnapshot *snap = calloc(1, sizeof(OwnershipSnapshot));
    snap->pool = newMemPool();
    snap->stmts = newHashTable(sizeof(StmtEntry), &snap->pool);
    snap->blocks = newHashTable(sizeof(BlockOrder), &snap->pool);

    Collector c = {.events = newDynArray(sizeof(OwnEvent)), .flagDecl = NULL};
    for (IrNode *fn = module->module.functions; fn; fn = fn->next) {
        if (fn->tag != irFunction)
            continue;
        for (IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
            if (bb->tag != irBasicBlock)
                continue;
            BlockOrder order = {.bb = bb};
            for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next)
                order.count++;
            order.stmts = allocFromMemPool(
                &snap->pool, sizeof(StmtEntry) * (order.count ? order.count : 1));

            u32 at = 0;
            for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
                collectEvents(&c, stmt);
                StmtEntry entry = {.stmt = stmt, .count = (u32)c.events.size};
                if (entry.count > 0) {
                    entry.events = allocFromMemPool(&snap->pool, sizeof(OwnEvent) * entry.count);
                    memcpy(entry.events, c.events.elems, sizeof(OwnEvent) * entry.count);
                }
                order.stmts[at++] = entry;
                insertInHashTable(&snap->stmts,
                                  &entry,
                                  hashPtr(hashInit(), stmt),
                                  sizeof(StmtEntry),
                                  compareStmtEntries);
            }
            insertInHashTable(&snap->blocks,
                              &order,
                              hashPtr(hashInit(), bb),
                              sizeof(BlockOrder),
                              compareBlockOrders);
        }
    }
    freeDynArray(&c.events);
    return snap;
}

void freeOwnershipSnapshot(OwnershipSnapshot *snap)
{
    if (snap == NULL)
        return;
    freeMemPool(&snap->pool);
    free(snap);
}

// ---------------------------------------------------------------------------
// Dataflow state
// ---------------------------------------------------------------------------

// pairs: bit (own << 1 | flag) is set when some path reaches here in that
// (owned, drop flag) combination. Keeping combinations, not two separate sets,
// is what lets `drop-if x, flag` be checked exactly.
typedef struct VarState {
    u8 pairs;
    bool staleAny; // some path moved or dropped the value
    bool staleAll; // every path did
    bool partialAny; // a field was moved out and not cleared yet
    bool partialAll;
    char originKind;
    const FileLoc *origin;
    const FileLoc *partialOrigin;
} VarState;

#define PAIR_BIT(own, flag) (1u << (((own) << 1) | (flag)))
#define PAIRS_OWNED 0xC
#define PAIRS_NOT_OWNED 0x3

static u8 pairsSetOwn(u8 pairs, u8 own)
{
    u8 out = 0;
    for (u8 i = 0; i < 4; i++) {
        if (pairs & (1u << i))
            out |= (u8)(1u << ((i & 1) | (own << 1)));
    }
    return out;
}

static u8 pairsSetFlag(u8 pairs, u8 flag)
{
    u8 out = 0;
    for (u8 i = 0; i < 4; i++) {
        if (pairs & (1u << i))
            out |= (u8)(1u << ((i & 2) | flag));
    }
    return out;
}

static bool anyOwned(const VarState *s) { return (s->pairs & PAIRS_OWNED) != 0; }

static bool allOwned(const VarState *s)
{
    return s->pairs != 0 && (s->pairs & PAIRS_NOT_OWNED) == 0;
}

typedef struct PtrIndex {
    const void *key;
    u32 value;
} PtrIndex;

static bool comparePtrIndexKey(const void *a, const void *b)
{
    return ((const PtrIndex *)a)->key == b;
}

static bool comparePtrIndexes(const void *a, const void *b)
{
    return ((const PtrIndex *)a)->key == ((const PtrIndex *)b)->key;
}

static void putPtrIndex(HashTable *table, const void *key, u32 value)
{
    PtrIndex entry = {.key = key, .value = value};
    insertInHashTable(
        table, &entry, hashPtr(hashInit(), key), sizeof(PtrIndex), comparePtrIndexes);
}

static i64 getPtrIndex(const HashTable *table, const void *key)
{
    PtrIndex *found = findInHashTable(
        table, key, hashPtr(hashInit(), key), sizeof(PtrIndex), comparePtrIndexKey);
    return found ? (i64)found->value : -1;
}

typedef struct BlockInfo {
    const IrNode *bb;
    StmtEntry *stmts;
    u32 stmtCount;
    u32 *succs;
    u32 succCount;
    VarState *in;
    bool reached;
} BlockInfo;

typedef enum {
    ruleO1,
    ruleO2,
    ruleO3,
    ruleO5,
    ruleO6,
    ruleO7,
    ruleO8,
    ruleO10,
    ruleO13,
    ruleB3,
    ruleB4
} Rule;

static const char *const ruleNames[] = {[ruleO1] = "O1",
                                        [ruleO2] = "O2",
                                        [ruleO3] = "O3",
                                        [ruleO5] = "O5",
                                        [ruleO6] = "O6",
                                        [ruleO7] = "O7",
                                        [ruleO8] = "O8",
                                        [ruleO10] = "O10",
                                        [ruleO13] = "O13",
                                        [ruleB3] = "B3",
                                        [ruleB4] = "B4"};

// The compiler warning each rule is reported under
static const u8 ruleWarnings[] = {[ruleO1] = wrnOwnerLeak,
                                  [ruleO2] = wrnUseAfterMove,
                                  [ruleO3] = wrnDoubleDrop,
                                  [ruleO5] = wrnUnclearedMove,
                                  [ruleO6] = wrnMoveOfReference,
                                  [ruleO7] = wrnOverwrittenOwner,
                                  [ruleO8] = wrnDropFlagMismatch,
                                  [ruleO10] = wrnTemporaryOwnership,
                                  [ruleO13] = wrnRedeclaredOwner,
                                  [ruleB3] = wrnReturnedBorrow,
                                  [ruleB4] = wrnDanglingBorrow};

typedef struct Finding {
    Rule rule;
    const IrNode *decl;
    u32 offset;
} Finding;

typedef struct FuncVerify {
    MemPool pool;
    const IrNode *func;
    const IrNode *flagDecl;
    HashTable varIndex;
    const IrNode **decls;
    u32 varCount;
    i32 *flagVar; // flag index to variable index, -1 for none
    u32 flagCount;
    BlockInfo *blocks;
    u32 blockCount;
    HashTable bindIndex; // reference variable to the owner variable it borrows
    DynArray reported;   // Finding
    OwnershipReporter reporter;
    void *reporterCtx;
    u64 findings;
} FuncVerify;

static cstring declName(const IrNode *decl)
{
    if (decl->tag == irFuncParam)
        return decl->funcParam.name;
    return decl->varDecl.display ? decl->varDecl.display : decl->varDecl.name;
}

static void reportFinding(FuncVerify *fv,
                          Rule rule,
                          bool definite,
                          const IrNode *decl,
                          const FileLoc *loc,
                          const VarState *state,
                          const char *what)
{
    u32 offset = loc ? (u32)loc->begin.byteOffset : 0;
    for (u64 i = 0; i < fv->reported.size; i++) {
        Finding *f = &((Finding *)fv->reported.elems)[i];
        if (f->rule == rule && f->decl == decl && f->offset == offset)
            return;
    }
    Finding key = {.rule = rule, .decl = decl, .offset = offset};
    pushOnDynArray(&fv->reported, &key);

    OwnershipFinding finding = {
        .rule = ruleNames[rule],
        .warning = ruleWarnings[rule],
        .definite = definite,
        .func = fv->func->function.display ? fv->func->function.display
                                           : fv->func->function.name,
        .var = declName(decl),
    };
    if (loc)
        finding.loc = *loc;

    const FileLoc *origin = NULL;
    if (state && state->origin) {
        origin = state->origin;
        finding.originNote = state->originKind == 'd' ? "dropped here" : "moved here";
    }
    else if (rule == ruleO1 || rule == ruleO13) {
        origin = &decl->loc;
        finding.originNote = "declared here";
    }
    if (origin) {
        finding.hasOrigin = true;
        finding.origin = *origin;
    }

    snprintf(finding.message,
             sizeof(finding.message),
             "`%s` %s%s",
             finding.var,
             what,
             definite ? "" : " on some path");
    fv->findings++;
    fv->reporter(fv->reporterCtx, &finding);
}

static void applyEvent(FuncVerify *fv, VarState *vars, const OwnEvent *ev, bool report)
{

    switch (ev->kind) {
    case oevFlagSet: {
        if (ev->flagIndex < 0 || (u32)ev->flagIndex >= fv->flagCount)
            return;
        i32 vi = fv->flagVar[ev->flagIndex];
        if (vi < 0)
            return;
        VarState *s = &vars[vi];
        if (ev->flagValue == 2)
            s->pairs = (u8)(pairsSetFlag(s->pairs, 0) | pairsSetFlag(s->pairs, 1));
        else
            s->pairs = pairsSetFlag(s->pairs, (u8)ev->flagValue);
        return;
    }
    case oevFlagsClear:
        for (u32 i = 0; i < fv->varCount; i++)
            vars[i].pairs = pairsSetFlag(vars[i].pairs, 0);
        return;
    case oevEscape:
        if (report)
            reportFinding(fv,
                          ruleB3,
                          true,
                          ev->decl,
                          ev->loc,
                          NULL,
                          "is borrowed and the borrow is returned, but it dies at function exit");
        return;
    case oevBind:
        return;
    case oevRefMove: // reported by reportReferenceMoves, it needs no state
        return;
    case oevRefUse: {
        i64 owner = getPtrIndex(&fv->bindIndex, ev->decl);
        if (!report || owner < 0 || owner >= (i64)fv->varCount)
            return;
        VarState *o = &vars[owner];
        if (o->staleAny) {
            char what[160];
            snprintf(what,
                     sizeof(what),
                     "borrows `%s`, which was moved or dropped",
                     declName(fv->decls[owner]));
            reportFinding(fv, ruleB4, o->staleAll, ev->decl, ev->loc, o, what);
        }
        return;
    }
    case oevReturn:
        if (!report)
            return;
        for (u32 i = 0; i < fv->varCount; i++) {
            VarState *s = &vars[i];
            if (anyOwned(s))
                reportFinding(fv,
                              ruleO1,
                              allOwned(s),
                              fv->decls[i],
                              ev->loc,
                              s,
                              "is still owned at function exit, it leaks");
        }
        return;
    default:
        break;
    }

    i64 vi = getPtrIndex(&fv->varIndex, ev->decl);
    if (vi < 0)
        return;
    VarState *s = &vars[vi];
    const IrNode *decl = ev->decl;

    switch (ev->kind) {
    case oevDecl:
        if (report && anyOwned(s))
            reportFinding(fv,
                          ruleO13,
                          allOwned(s),
                          decl,
                          ev->loc,
                          s,
                          "is declared again while its previous value is still owned");
        s->pairs = pairsSetOwn(s->pairs, 0);
        s->staleAny = s->staleAll = false;
        s->partialAny = s->partialAll = false;
        s->origin = NULL;
        break;
    case oevDef:
    case oevClear:
        if (report && anyOwned(s))
            reportFinding(fv,
                          ruleO7,
                          allOwned(s),
                          decl,
                          ev->loc,
                          s,
                          "is assigned while its old value is still owned, the old value leaks");
        s->pairs = pairsSetOwn(s->pairs, ev->kind == oevDef ? 1 : 0);
        s->staleAny = s->staleAll = false;
        s->partialAny = s->partialAll = false;
        s->origin = NULL;
        break;
    case oevMovePart:
        s->partialAny = s->partialAll = true;
        s->partialOrigin = ev->loc;
        break;
    case oevZeroPart:
        s->partialAny = s->partialAll = false;
        break;
    case oevUse:
        if (report && s->staleAny)
            reportFinding(fv,
                          ruleO2,
                          s->staleAll,
                          decl,
                          ev->loc,
                          s,
                          "is used after it was moved or dropped");
        break;
    case oevMove:
        if (report && s->staleAny)
            reportFinding(fv,
                          ruleO2,
                          s->staleAll,
                          decl,
                          ev->loc,
                          s,
                          "is moved after it was moved or dropped");
        s->pairs = pairsSetOwn(s->pairs, 0);
        s->staleAny = s->staleAll = true;
        s->originKind = 'm';
        s->origin = ev->loc;
        break;
    case oevDrop:
        if (report && s->partialAny) {
            VarState hole = *s;
            hole.origin = s->partialOrigin;
            hole.originKind = 'm';
            reportFinding(fv,
                          ruleO5,
                          s->partialAll,
                          decl,
                          ev->loc,
                          &hole,
                          "is dropped while a field moved out of it was not cleared");
        }
        s->partialAny = s->partialAll = false;
        if (ev->flagIndex == -1) {
            if (report && s->staleAny)
                reportFinding(fv,
                              ruleO3,
                              s->staleAll,
                              decl,
                              ev->loc,
                              s,
                              "is dropped but it was already moved or dropped");
            s->pairs = pairsSetOwn(s->pairs, 0);
            s->staleAny = s->staleAll = true;
        }
        else if (ev->flagIndex == -2) {
            s->pairs = pairsSetOwn(s->pairs, 0);
            s->staleAny = true;
        }
        else {
            // Owned must equal the flag wherever the drop runs
            u8 setNotOwned = (u8)PAIR_BIT(0, 1);
            u8 clearOwned = (u8)PAIR_BIT(1, 0);
            bool onlySetNotOwned = s->pairs == setNotOwned;
            bool onlyClearOwned = s->pairs == clearOwned;
            if (report && (s->pairs & setNotOwned))
                reportFinding(fv,
                              ruleO8,
                              onlySetNotOwned,
                              decl,
                              ev->loc,
                              s,
                              "has its drop flag set but the value is not owned");
            if (report && (s->pairs & clearOwned))
                reportFinding(fv,
                              ruleO8,
                              onlyClearOwned,
                              decl,
                              ev->loc,
                              s,
                              "has its drop flag clear while the value is owned, it leaks");
            bool allSet = (s->pairs & (PAIR_BIT(0, 0) | PAIR_BIT(1, 0))) == 0;
            s->pairs = pairsSetOwn(s->pairs, 0);
            s->staleAll = s->staleAll || allSet;
            s->staleAny = true;
        }
        s->originKind = 'd';
        s->origin = ev->loc;
        break;
    default:
        break;
    }
}

static void runBlock(FuncVerify *fv, BlockInfo *b, VarState *cur, bool report)
{
    for (u32 i = 0; i < b->stmtCount; i++) {
        StmtEntry *e = &b->stmts[i];
        for (u32 k = 0; k < e->count; k++)
            applyEvent(fv, cur, &e->events[k], report);
    }
}

static bool joinInto(BlockInfo *dst, const VarState *src, u32 n)
{
    if (!dst->reached) {
        memcpy(dst->in, src, sizeof(VarState) * n);
        dst->reached = true;
        return true;
    }

    bool changed = false;
    for (u32 i = 0; i < n; i++) {
        VarState *d = &dst->in[i];
        const VarState *s = &src[i];
        u8 pairs = d->pairs | s->pairs;
        bool staleAny = d->staleAny || s->staleAny;
        bool staleAll = d->staleAll && s->staleAll;
        bool partialAny = d->partialAny || s->partialAny;
        bool partialAll = d->partialAll && s->partialAll;
        if (s->partialOrigin && (d->partialOrigin == NULL || (s->partialAny && !d->partialAny)))
            d->partialOrigin = s->partialOrigin;
        if (s->origin && (d->origin == NULL || (s->staleAny && !d->staleAny))) {
            d->origin = s->origin;
            d->originKind = s->originKind;
        }
        if (pairs != d->pairs || staleAny != d->staleAny || staleAll != d->staleAll ||
            partialAny != d->partialAny || partialAll != d->partialAll)
            changed = true;
        d->pairs = pairs;
        d->staleAny = staleAny;
        d->staleAll = staleAll;
        d->partialAny = partialAny;
        d->partialAll = partialAll;
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Per function setup
// ---------------------------------------------------------------------------

static u32 countSuccessors(const IrNode *bb)
{
    u32 n = 0;
    for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
        if (s->tag == irBranch)
            n += 1;
        else if (s->tag == irBranchIf)
            n += 2;
        else if (s->tag == irSwitch)
            n += (u32)s->switchStmt.casesCount + 1;
    }
    return n;
}

static bool collectTrackedVars(DynArray *decls, const IrNode *fn)
{
    for (const IrNode *p = fn->function.params; p; p = p->next) {
        if (isTrackedDecl(p))
            pushOnDynArray(decls, &p);
    }
    for (const IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            if (s->tag == irVarDecl && isTrackedDecl(s))
                pushOnDynArray(decls, &s);
        }
    }
    return decls->size > 0;
}

// The flag variable is whichever one `drop-if` reads. It maps each flag index
// to the variable it guards.
static void findFlags(FuncVerify *fv, const IrNode *fn)
{
    i32 maxIndex = -1;
    for (const IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            if (s->tag != irDrop || s->drop.flag == NULL)
                continue;
            const IrNode *flag = stripCasts(s->drop.flag);
            if (flag == NULL || flag->tag != irMember)
                continue;
            const IrNode *base = stripCasts(flag->member.target);
            if (base && base->tag == irVarRef) {
                fv->flagDecl = base->varRef.target;
                if ((i32)flag->member.index > maxIndex)
                    maxIndex = (i32)flag->member.index;
            }
        }
    }
    if (maxIndex < 0)
        return;

    fv->flagCount = (u32)maxIndex + 1;
    fv->flagVar = allocFromMemPool(&fv->pool, sizeof(i32) * fv->flagCount);
    for (u32 i = 0; i < fv->flagCount; i++)
        fv->flagVar[i] = -1;

    for (const IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            if (s->tag != irDrop || s->drop.flag == NULL)
                continue;
            const IrNode *flag = stripCasts(s->drop.flag);
            const IrNode *decl = trackedVarRef(s->drop.target);
            if (flag && flag->tag == irMember && decl) {
                i64 vi = getPtrIndex(&fv->varIndex, decl);
                if (vi >= 0)
                    fv->flagVar[flag->member.index] = (i32)vi;
            }
        }
    }
}

// O10. A temporary the compiler made has its lifetime fixed by its class, and it
// lives inside one block, so the check needs no flow. An in flight one is handed to
// exactly one consumer. A statement one is only read, and dropped once after its
// last read.
typedef struct TempFacts {
    const IrNode *decl;
    IrValueClass cls;
    const IrNode *defBlock;
    bool defined;
    u32 owned;       // uses in an owned position
    u32 drops;
    const IrNode *dropBlock;
    u32 dropIndex;
    const IrNode *lastReadBlock;
    u32 lastRead;    // index of the last statement that reads or hands it over
    bool read;
} TempFacts;

typedef struct TempScan {
    FuncVerify *fv;
    HashTable index; // temporary to its facts
    TempFacts *facts;
    const IrNode *block;
    u32 stmtIndex;
} TempScan;

static TempFacts *factsOf(TempScan *scan, const IrNode *decl)
{
    i64 at = getPtrIndex(&scan->index, decl);
    return at < 0 ? NULL : &scan->facts[at];
}

static void scanTempOperand(TempScan *scan, const IrNode *node, bool owned);

static void scanTempSlot(IrNode **slot, void *scanPtr)
{
    scanTempOperand(scanPtr, *slot, false);
}

// `owned` carries through casts: `<Base>t` hands `t` over as a whole
static void scanTempOperand(TempScan *scan, const IrNode *node, bool owned)
{
    if (node == NULL)
        return;
    owned = owned || irHasInfo(node, irInfoOwnedPosition);
    if (node->tag == irCast) {
        scanTempOperand(scan, node->cast.expr, owned);
        return;
    }
    if (node->tag == irVarRef) {
        TempFacts *f = factsOf(scan, node->varRef.target);
        if (f == NULL)
            return;
        if (owned)
            f->owned++;
        f->read = true;
        f->lastReadBlock = scan->block;
        f->lastRead = scan->stmtIndex;
        return;
    }
    irForEachOperandSlot((IrNode *)node, scanTempSlot, scan);
}

static void scanTempStatement(TempScan *scan, const IrNode *stmt)
{
    const IrNode *defined = NULL;
    if (stmt->tag == irAssign)
        defined = stripCasts(stmt->assign.lhs);
    else if (stmt->tag == irCopy)
        defined = stripCasts(stmt->copy.dst);
    if (defined && defined->tag == irVarRef) {
        TempFacts *f = factsOf(scan, defined->varRef.target);
        if (f) {
            if (!f->defined) {
                f->defined = true;
                f->defBlock = scan->block;
            }
            scanTempOperand(scan, stmt->tag == irAssign ? stmt->assign.rhs : stmt->copy.src, false);
            return;
        }
    }
    if (stmt->tag == irDrop) {
        const IrNode *target = stripCasts(stmt->drop.target);
        TempFacts *f = target && target->tag == irVarRef ? factsOf(scan, target->varRef.target) : NULL;
        if (f) {
            if (f->drops++ == 0) {
                f->dropBlock = scan->block;
                f->dropIndex = scan->stmtIndex;
            }
            return;
        }
    }
    irForEachOperandSlot((IrNode *)stmt, scanTempSlot, scan);
}

static void reportTemporary(FuncVerify *fv, const TempFacts *f, const char *what)
{
    reportFinding(fv, ruleO10, true, f->decl, &f->decl->loc, NULL, what);
}

static void checkTemporary(FuncVerify *fv, const TempFacts *f)
{
    if (!f->defined)
        return;
    if (f->cls == irClassInFlight) {
        if (f->owned == 0)
            reportTemporary(fv, f, "is made but nobody takes it, the value leaks");
        else if (f->owned > 1)
            reportTemporary(fv, f, "is handed to more than one consumer");
        return;
    }
    // a statement temporary
    if (f->owned > 0)
        reportTemporary(fv, f, "is dropped after the statement and also handed to a consumer");
    else if (f->drops == 0)
        reportTemporary(fv, f, "is made but never dropped, the value leaks");
    else if (f->drops > 1)
        reportTemporary(fv, f, "is dropped more than once");
    else if (f->dropBlock != f->defBlock)
        reportTemporary(fv, f, "is dropped outside the block that makes it");
    else if (f->read && (f->lastReadBlock != f->dropBlock || f->lastRead > f->dropIndex))
        reportTemporary(fv, f, "is read after it was dropped");
}

static void verifyTemporaries(FuncVerify *fv)
{
    u32 count = 0;
    for (const IrNode *bb = fv->func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            IrValueClass cls = s->tag == irVarDecl ? irValueClass(s) : irClassNone;
            if (cls == irClassInFlight || cls == irClassStatement)
                count++;
        }
    }
    if (count == 0)
        return;

    TempScan scan = {.fv = fv, .index = newHashTable(sizeof(PtrIndex), &fv->pool)};
    scan.facts = allocFromMemPool(&fv->pool, sizeof(TempFacts) * count);
    memset(scan.facts, 0, sizeof(TempFacts) * count);
    u32 at = 0;
    for (const IrNode *bb = fv->func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            IrValueClass cls = s->tag == irVarDecl ? irValueClass(s) : irClassNone;
            if (cls != irClassInFlight && cls != irClassStatement)
                continue;
            scan.facts[at] = (TempFacts){.decl = s, .cls = cls};
            putPtrIndex(&scan.index, s, at++);
        }
    }

    for (const IrNode *bb = fv->func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        scan.block = bb;
        scan.stmtIndex = 0;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next, scan.stmtIndex++)
            scanTempStatement(&scan, s);
    }
    for (u32 i = 0; i < count; i++)
        checkTemporary(fv, &scan.facts[i]);
}

// O6 needs no flow: a `&&` the user wrote on a reference is wrong wherever it is,
// also in a function that owns nothing
static void reportReferenceMoves(FuncVerify *fv, const OwnershipSnapshot *snap)
{
    for (const IrNode *bb = fv->func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        const BlockOrder *order = findBlockOrder(&snap->blocks, bb);
        for (u32 i = 0; order && i < order->count; i++) {
            const StmtEntry *known = &order->stmts[i];
            for (u32 k = 0; k < known->count; k++) {
                const OwnEvent *ev = &known->events[k];
                if (ev->kind == oevRefMove)
                    reportFinding(fv,
                                  ruleO6,
                                  true,
                                  ev->decl,
                                  ev->loc,
                                  NULL,
                                  "is a reference, it cannot be moved out of: write "
                                  "`@copy` to take an owner");
            }
        }
    }
}

// `move x` stands before the statement that takes `x`, so the drop flag is cleared
// before a call that may suspend. That statement still reads `x` first, so the move
// takes effect after the first statement that is not synthetic.
static void deferMoves(StmtEntry *stmts, u32 count, MemPool *pool)
{
    u32 room = count ? count : 1;
    StmtEntry *held = allocFromMemPool(pool, sizeof(StmtEntry) * room);
    StmtEntry *ordered = allocFromMemPool(pool, sizeof(StmtEntry) * room);
    u32 heldCount = 0;
    u32 out = 0;
    for (u32 i = 0; i < count; i++) {
        const IrNode *s = stmts[i].stmt;
        if (s->tag == irMove) {
            held[heldCount++] = stmts[i];
            continue;
        }
        ordered[out++] = stmts[i];
        if (irHasInfo(s, irInfoSynthetic))
            continue;
        for (u32 k = 0; k < heldCount; k++)
            ordered[out++] = held[k];
        heldCount = 0;
    }
    for (u32 k = 0; k < heldCount; k++)
        ordered[out++] = held[k];
    memcpy(stmts, ordered, sizeof(StmtEntry) * count);
}

static void verifyFunction(const OwnershipSnapshot *snap,
                           IrNode *fn,
                           OwnershipReporter reporter,
                           void *ctx,
                           u64 *findings)
{
    DynArray decls = newDynArray(sizeof(const IrNode *));
    FuncVerify fv = {
        .pool = newMemPool(),
        .func = fn,
        .reporter = reporter,
        .reporterCtx = ctx,
        .reported = newDynArray(sizeof(Finding)),
    };
    fv.varIndex = newHashTable(sizeof(PtrIndex), &fv.pool);
    reportReferenceMoves(&fv, snap);
    verifyTemporaries(&fv);

    if (!collectTrackedVars(&decls, fn))
        goto done;

    fv.varCount = (u32)decls.size;
    fv.decls = allocFromMemPool(&fv.pool, sizeof(const IrNode *) * fv.varCount);
    memcpy(fv.decls, decls.elems, sizeof(const IrNode *) * fv.varCount);
    for (u32 i = 0; i < fv.varCount; i++)
        putPtrIndex(&fv.varIndex, fv.decls[i], i);

    findFlags(&fv, fn);

    // Blocks and their statements' events
    HashTable blockIndex = newHashTable(sizeof(PtrIndex), &fv.pool);
    for (const IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag == irBasicBlock)
            fv.blockCount++;
    }
    if (fv.blockCount == 0)
        goto done;

    fv.blocks = allocFromMemPool(&fv.pool, sizeof(BlockInfo) * fv.blockCount);
    memset(fv.blocks, 0, sizeof(BlockInfo) * fv.blockCount);
    u32 bi = 0;
    for (const IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        BlockInfo *b = &fv.blocks[bi];
        b->bb = bb;
        putPtrIndex(&blockIndex, bb, bi);
        b->in = allocFromMemPool(&fv.pool, sizeof(VarState) * fv.varCount);
        memset(b->in, 0, sizeof(VarState) * fv.varCount);

        const BlockOrder *order = findBlockOrder(&snap->blocks, bb);
        u32 capacity = order ? order->count : 0;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next)
            capacity++;
        b->stmts = allocFromMemPool(&fv.pool, sizeof(StmtEntry) * (capacity ? capacity : 1));
        u32 si = 0;
        u32 removed = 0; // the next statement of the snapshot that is not accounted for
        Collector c = {.events = newDynArray(sizeof(OwnEvent)), .flagDecl = fv.flagDecl};
        for (IrNode *s = bb->basicBlock.first; s; s = s->next) {
            StmtEntry *known = findStmtEntry(&snap->stmts, s);
            if (known) {
                // what the snapshot held before `s` and is gone now was removed by the pass
                while (order && removed < order->count && order->stmts[removed].stmt != s)
                    b->stmts[si++] = order->stmts[removed++];
                removed++;
                b->stmts[si++] = *known;
                continue;
            }
            // Added by the memory pass
            collectEvents(&c, s);
            StmtEntry entry = {.stmt = s, .count = (u32)c.events.size};
            if (entry.count > 0) {
                entry.events = allocFromMemPool(&fv.pool, sizeof(OwnEvent) * entry.count);
                memcpy(entry.events, c.events.elems, sizeof(OwnEvent) * entry.count);
            }
            b->stmts[si++] = entry;
        }
        while (order && removed < order->count)
            b->stmts[si++] = order->stmts[removed++];
        b->stmtCount = si;
        deferMoves(b->stmts, b->stmtCount, &fv.pool);
        freeDynArray(&c.events);
        bi++;
    }

    // A reference variable tied to one owner is checked against that owner's
    // state at every read
    fv.bindIndex = newHashTable(sizeof(PtrIndex), &fv.pool);
    for (u32 i = 0; i < fv.blockCount; i++) {
        BlockInfo *b = &fv.blocks[i];
        for (u32 si = 0; si < b->stmtCount; si++) {
            for (u32 k = 0; k < b->stmts[si].count; k++) {
                const OwnEvent *ev = &b->stmts[si].events[k];
                if (ev->kind != oevBind)
                    continue;
                i64 owner = ev->other ? getPtrIndex(&fv.varIndex, ev->other) : -1;
                u32 value = owner >= 0 ? (u32)owner : UINT32_MAX;
                PtrIndex *known = findInHashTable(&fv.bindIndex,
                                                  ev->decl,
                                                  hashPtr(hashInit(), ev->decl),
                                                  sizeof(PtrIndex),
                                                  comparePtrIndexKey);
                if (known == NULL)
                    putPtrIndex(&fv.bindIndex, ev->decl, value);
                else if (known->value != value)
                    known->value = UINT32_MAX;
            }
        }
    }

    for (u32 i = 0; i < fv.blockCount; i++) {
        BlockInfo *b = &fv.blocks[i];
        u32 cap = countSuccessors(b->bb);
        b->succs = allocFromMemPool(&fv.pool, sizeof(u32) * (cap ? cap : 1));
        for (const IrNode *s = b->bb->basicBlock.first; s; s = s->next) {
            const IrNode *targets[3] = {NULL, NULL, NULL};
            if (s->tag == irBranch) {
                targets[0] = s->branch.target;
            }
            else if (s->tag == irBranchIf) {
                targets[0] = s->branchIf.trueTarget;
                targets[1] = s->branchIf.falseTarget;
            }
            else if (s->tag == irSwitch) {
                for (u64 k = 0; k < s->switchStmt.casesCount; k++) {
                    i64 t = getPtrIndex(&blockIndex, s->switchStmt.cases[k].target);
                    if (t >= 0)
                        b->succs[b->succCount++] = (u32)t;
                }
                targets[0] = s->switchStmt.defaultTarget;
            }
            for (u32 k = 0; k < 3; k++) {
                if (targets[k] == NULL)
                    continue;
                i64 t = getPtrIndex(&blockIndex, targets[k]);
                if (t >= 0)
                    b->succs[b->succCount++] = (u32)t;
            }
        }
    }

    // Entry state: parameters are owned, locals hold nothing
    BlockInfo *entry = &fv.blocks[0];
    for (u32 i = 0; i < fv.varCount; i++) {
        bool isParam = fv.decls[i]->tag == irFuncParam;
        entry->in[i].pairs = (u8)(isParam ? PAIR_BIT(1, 0) : PAIR_BIT(0, 0));
    }
    entry->reached = true;

    VarState *cur = allocFromMemPool(&fv.pool, sizeof(VarState) * fv.varCount);

    bool changed = true;
    for (u32 iter = 0; changed && iter < 64 + fv.blockCount * 4; iter++) {
        changed = false;
        for (u32 i = 0; i < fv.blockCount; i++) {
            BlockInfo *b = &fv.blocks[i];
            if (!b->reached)
                continue;
            memcpy(cur, b->in, sizeof(VarState) * fv.varCount);
            runBlock(&fv, b, cur, false);
            for (u32 k = 0; k < b->succCount; k++) {
                if (joinInto(&fv.blocks[b->succs[k]], cur, fv.varCount))
                    changed = true;
            }
        }
    }

    for (u32 i = 0; i < fv.blockCount; i++) {
        BlockInfo *b = &fv.blocks[i];
        if (!b->reached)
            continue;
        memcpy(cur, b->in, sizeof(VarState) * fv.varCount);
        runBlock(&fv, b, cur, true);
    }

done:
    *findings += fv.findings;
    freeDynArray(&decls);
    freeDynArray(&fv.reported);
    freeMemPool(&fv.pool);
}

u64 ownershipVerify(const OwnershipSnapshot *snap,
                    IrNode *module,
                    OwnershipReporter reporter,
                    void *ctx)
{
    if (snap == NULL || module == NULL || reporter == NULL)
        return 0;

    u64 findings = 0;
    for (IrNode *fn = module->module.functions; fn; fn = fn->next) {
        if (fn->tag == irFunction && fn->function.firstBlock)
            verifyFunction(snap, fn, reporter, ctx, &findings);
    }
    return findings;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

void ownershipReportToLog(void *ctx, const OwnershipFinding *f)
{
    Log *L = ctx;
    if (!(L->enabledWarnings.num & BIT(f->warning)))
        return;

    char text[320];
    snprintf(text, sizeof(text), "%s (in %s)", f->message, f->func);
    FormatArg args[] = {{.s = text}};
    logWarningWithId(L, f->warning, &f->loc, "{s}", args);
    if (f->hasOrigin) {
        FormatArg note[] = {{.s = f->originNote}};
        logNote(L, &f->origin, "{s}", note);
    }
}
