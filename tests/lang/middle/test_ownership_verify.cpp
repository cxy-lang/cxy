/**
 * Unit Tests: ownership verifier (docs/stage/memory-verifier.md)
 *
 * Each case builds the IR the lowering would produce, takes the snapshot, then
 * adds the statements the memory pass would insert and verifies. A defect is a
 * drop or flag the pass got wrong, a correct sibling expects no findings.
 */

#include "doctest.h"

#include "core/mempool.h"
#include "core/strpool.h"
#include "lang/middle/ir/node.h"
#include "lang/middle/ir/type.h"
#include "lang/middle/memory/verify.h"

#include <string>
#include <vector>

namespace {

struct Found {
    std::string rule;
    std::string var;
    bool definite;
};

void collect(void *ctx, const OwnershipFinding *f)
{
    static_cast<std::vector<Found> *>(ctx)->push_back({f->rule, f->var, f->definite});
}

struct Builder {
    MemPool pool = newMemPool();
    StrPool strings = newStrPool(&pool);
    IrTypeTable *types = newIrTypeTable(&pool, &strings);
    const IrType *cls = makeIrClassType(types, flgNone, nullptr);
    const IrType *u64Type = getIrPrimitiveType(types, irTypeU64);
    const IrType *refType = makeIrPointerType(types, flgNone, irTypeCatReference, nullptr);
    IrNode *func = nullptr;
    IrNode *module = nullptr;
    IrNode *params = nullptr;
    u32 line = 1;

    Builder()
    {
        FileLoc loc = at();
        module = makeIrModule(&pool, &loc, "m", types);
        func = makeIrFunction(&pool, &loc, flgNone, "f", "f", nullptr, nullptr, 0);
        module->module.functions = func;
    }

    ~Builder()
    {
        freeStrPool(&strings);
        freeMemPool(&pool);
    }

    FileLoc at()
    {
        FileLoc loc = {};
        loc.fileName = "test.cxy";
        loc.begin.row = loc.end.row = line;
        loc.begin.byteOffset = loc.end.byteOffset = line * 10;
        line++;
        return loc;
    }

    IrNode *block(const char *name)
    {
        FileLoc loc = at();
        IrNode *bb = makeIrBasicBlock(&pool, &loc, name);
        irAppendFunctionBlock(func, bb);
        return bb;
    }

    IrNode *var(IrNode *bb, const char *name)
    {
        FileLoc loc = at();
        IrNode *decl = makeIrVarDecl(&pool, &loc, flgNone, name, name, cls, 1);
        irAppendBlockStmt(bb, decl);
        return decl;
    }

    IrNode *refVar(IrNode *bb, const char *name, u64 flags = flgNone)
    {
        FileLoc loc = at();
        IrNode *decl = makeIrVarDecl(&pool, &loc, flags, name, name, refType, 1);
        irAppendBlockStmt(bb, decl);
        return decl;
    }

    // `&x` of a class local: the handle cast to a reference type
    IrNode *borrow(IrNode *decl)
    {
        FileLoc loc = at();
        return makeIrCast(&pool, &loc, ref(decl), refType);
    }

    IrNode *field(IrNode *decl)
    {
        FileLoc loc = at();
        return makeIrMember(&pool, &loc, ref(decl), 0, "f", cls);
    }

    IrNode *param(const char *name)
    {
        FileLoc loc = at();
        IrNode *p = makeIrFuncParam(&pool, &loc, flgNone, name, 0, cls);
        p->next = params;
        params = p;
        func->function.params = p;
        func->function.paramsCount++;
        return p;
    }

    IrNode *ref(IrNode *decl)
    {
        FileLoc loc = at();
        return makeIrVarRef(&pool, &loc, decl);
    }

    IrNode *value()
    {
        FileLoc loc = at();
        return makeIrIntConst(&pool, &loc, 1, false, u64Type);
    }

    IrNode *move(IrNode *decl)
    {
        FileLoc loc = at();
        return makeIrMove(&pool, &loc, ref(decl));
    }

    IrNode *assign(IrNode *decl, IrNode *rhs)
    {
        FileLoc loc = at();
        return makeIrAssign(&pool, &loc, ref(decl), rhs);
    }

    IrNode *call(IrNode *arg)
    {
        FileLoc loc = at();
        IrNode *callee = makeIrGlobalRef(&pool, &loc, "sink", nullptr, nullptr);
        return makeIrCall(&pool, &loc, callee, arg, arg ? 1 : 0, nullptr);
    }

    IrNode *drop(IrNode *decl)
    {
        FileLoc loc = at();
        return makeIrDrop(&pool, &loc, ref(decl));
    }

    IrNode *flagMember(IrNode *flags, u32 index)
    {
        FileLoc loc = at();
        return makeIrMember(&pool, &loc, ref(flags), index, "flag", u64Type);
    }

    IrNode *dropIf(IrNode *decl, IrNode *flags, u32 index)
    {
        FileLoc loc = at();
        return makeIrDropIf(&pool, &loc, ref(decl), flagMember(flags, index));
    }

    IrNode *setFlag(IrNode *flags, u32 index, u64 v)
    {
        FileLoc loc = at();
        FileLoc l2 = at();
        return makeIrAssign(&pool, &loc, flagMember(flags, index), makeIrIntConst(&pool, &l2, v, false, u64Type));
    }

    IrNode *ret()
    {
        FileLoc loc = at();
        return makeIrReturn(&pool, &loc, nullptr);
    }

    IrNode *branch(IrNode *target)
    {
        FileLoc loc = at();
        return makeIrBranch(&pool, &loc, target);
    }

    IrNode *branchIf(IrNode *t, IrNode *f)
    {
        FileLoc loc = at();
        return makeIrBranchIf(&pool, &loc, value(), t, f);
    }

    IrNode *flagsDecl(IrNode *bb)
    {
        FileLoc loc = at();
        IrNode *d = makeIrVarDecl(&pool, &loc, flgNone, "__drop_flags", "__drop_flags", u64Type, 0);
        irAppendBlockStmt(bb, d);
        return d;
    }

    // Splices `stmt` in front of `anchor`, the way the pass inserts drops
    static void insertBefore(IrNode *bb, IrNode *anchor, IrNode *stmt)
    {
        IrNode *prev = nullptr;
        for (IrNode *s = bb->basicBlock.first; s; prev = s, s = s->next) {
            if (s == anchor)
                break;
        }
        stmt->next = anchor;
        if (prev)
            prev->next = stmt;
        else
            bb->basicBlock.first = stmt;
    }

    std::vector<Found> verify(OwnershipSnapshot *snap)
    {
        std::vector<Found> found;
        ownershipVerify(snap, module, collect, &found);
        return found;
    }
};

} // namespace

TEST_CASE("Ownership verifier: clean functions")
{
    SUBCASE("owned local dropped before return")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(x));
        CHECK(b.verify(snap).empty());
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("moved local needs no drop")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        irAppendBlockStmt(bb, b.call(b.move(x)));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        CHECK(b.verify(snap).empty());
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("parameter dropped by the callee")
    {
        Builder b;
        IrNode *p = b.param("p");
        IrNode *bb = b.block("entry");
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(p));
        CHECK(b.verify(snap).empty());
        freeOwnershipSnapshot(snap);
    }
}

// What the verifier follows is told by the class: a variable the pass drops at
// scope exit is an owner whoever made it, one it never drops is not
TEST_CASE("Ownership verifier: what is tracked depends on the class, not on who made it")
{
    auto leak = [](IrValueClass cls) {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "env");
        irSetInfo(x, irInfoSynthetic);
        irSetValueClass(x, cls);
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        auto found = b.verify(snap);
        freeOwnershipSnapshot(snap);
        return found;
    };

    SUBCASE("a compiler variable with an ordinary lifetime is an owner")
    {
        auto found = leak(irClassNone);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O1");
        CHECK(found[0].var == "env");
    }

    SUBCASE("a temporary that outlives its statement is an owner")
    {
        CHECK(leak(irClassExtended).size() == 1);
    }

    SUBCASE("a transient, in flight, statement, result or return slot variable is not")
    {
        CHECK(leak(irClassTransient).empty());
        CHECK(leak(irClassInFlight).empty());
        CHECK(leak(irClassStatement).empty());
        CHECK(leak(irClassResult).empty());
        CHECK(leak(irClassReturnSlot).empty());
    }
}

TEST_CASE("Ownership verifier: O1 leak at exit")
{
    SUBCASE("local never dropped")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O1");
        CHECK(found[0].var == "x");
        CHECK(found[0].definite);
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("parameter never dropped")
    {
        Builder b;
        b.param("p");
        IrNode *bb = b.block("entry");
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O1");
        freeOwnershipSnapshot(snap);
    }
}

TEST_CASE("Ownership verifier: O3 drop of a moved value")
{
    SUBCASE("move in a call argument")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        irAppendBlockStmt(bb, b.call(b.move(x)));
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(x));
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O3");
        CHECK(found[0].definite);
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("move nested in a call inside a struct literal")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        IrNode *y = b.var(bb, "y");
        irAppendBlockStmt(bb, b.assign(x, b.value()));

        FileLoc loc = b.at();
        IrNode *field = makeIrFieldConst(&b.pool, &loc, "page", 0, b.call(b.move(x)), nullptr);
        IrNode *literal = makeIrStructConst(&b.pool, &loc, field, 1, nullptr);
        irAppendBlockStmt(bb, b.assign(y, literal));
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(y));
        Builder::insertBefore(bb, r, b.drop(x));
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O3");
        CHECK(found[0].var == "x");
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("move wrapped by an optional")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        IrNode *y = b.var(bb, "y");
        irAppendBlockStmt(bb, b.assign(x, b.value()));

        FileLoc loc = b.at();
        IrNode *opt = makeIrOptionalValue(&b.pool, &loc, b.move(x), nullptr);
        irAppendBlockStmt(bb, b.assign(y, opt));
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(y));
        Builder::insertBefore(bb, r, b.drop(x));
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O3");
        CHECK(found[0].var == "x");
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("dropped twice")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(x));
        Builder::insertBefore(bb, r, b.drop(x));
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O3");
        freeOwnershipSnapshot(snap);
    }
}

TEST_CASE("Ownership verifier: O2 use after move")
{
    Builder b;
    IrNode *bb = b.block("entry");
    IrNode *x = b.var(bb, "x");
    irAppendBlockStmt(bb, b.assign(x, b.value()));
    irAppendBlockStmt(bb, b.call(b.move(x)));
    irAppendBlockStmt(bb, b.call(b.ref(x)));
    irAppendBlockStmt(bb, b.ret());

    OwnershipSnapshot *snap = ownershipSnapshot(b.module);
    auto found = b.verify(snap);
    REQUIRE(found.size() == 1);
    CHECK(found[0].rule == "O2");
    CHECK(found[0].definite);
    freeOwnershipSnapshot(snap);
}

TEST_CASE("Ownership verifier: O7 assignment over an owned value")
{
    Builder b;
    IrNode *bb = b.block("entry");
    IrNode *x = b.var(bb, "x");
    irAppendBlockStmt(bb, b.assign(x, b.value()));
    irAppendBlockStmt(bb, b.assign(x, b.value()));
    IrNode *r = b.ret();
    irAppendBlockStmt(bb, r);

    OwnershipSnapshot *snap = ownershipSnapshot(b.module);
    Builder::insertBefore(bb, r, b.drop(x));
    auto found = b.verify(snap);
    REQUIRE(found.size() == 1);
    CHECK(found[0].rule == "O7");
    freeOwnershipSnapshot(snap);
}

// entry: x = v; flag = 1; if c goto thenBlock else merge
// thenBlock: sink(&&x); [flag = 0]; goto merge
// merge: drop-if x, flag; return
static std::vector<Found> conditionalMove(bool clearFlag)
{
    Builder b;
    IrNode *entry = b.block("entry");
    IrNode *thenBlock = b.block("then");
    IrNode *merge = b.block("merge");

    IrNode *flags = b.flagsDecl(entry);
    IrNode *x = b.var(entry, "x");
    irAppendBlockStmt(entry, b.assign(x, b.value()));
    irAppendBlockStmt(entry, b.branchIf(thenBlock, merge));

    irAppendBlockStmt(thenBlock, b.call(b.move(x)));
    irAppendBlockStmt(thenBlock, b.branch(merge));
    IrNode *r = b.ret();
    irAppendBlockStmt(merge, r);

    OwnershipSnapshot *snap = ownershipSnapshot(b.module);

    // What the pass adds
    FileLoc loc = b.at();
    Builder::insertBefore(entry, entry->basicBlock.first, makeIrZeromem(&b.pool, &loc, b.ref(flags)));
    IrNode *branchStmt = entry->basicBlock.first;
    while (branchStmt->next)
        branchStmt = branchStmt->next;
    Builder::insertBefore(entry, branchStmt, b.setFlag(flags, 0, 1));
    if (clearFlag)
        Builder::insertBefore(thenBlock, thenBlock->basicBlock.last, b.setFlag(flags, 0, 0));
    Builder::insertBefore(merge, r, b.dropIf(x, flags, 0));

    auto found = b.verify(snap);
    freeOwnershipSnapshot(snap);
    return found;
}

TEST_CASE("Ownership verifier: drop flags")
{
    SUBCASE("conditional move with the flag cleared")
    {
        CHECK(conditionalMove(true).empty());
    }

    SUBCASE("conditional move, flag left set")
    {
        auto found = conditionalMove(false);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O8");
        CHECK_FALSE(found[0].definite);
    }
}

TEST_CASE("Ownership verifier: O13 redeclared while owned")
{
    Builder b;
    IrNode *entry = b.block("entry");
    IrNode *loop = b.block("loop");
    IrNode *exit = b.block("exit");
    irAppendBlockStmt(entry, b.branch(loop));

    IrNode *x = b.var(loop, "x");
    irAppendBlockStmt(loop, b.assign(x, b.value()));
    irAppendBlockStmt(loop, b.branchIf(loop, exit));
    IrNode *r = b.ret();
    irAppendBlockStmt(exit, r);

    // The pass forgot the drop at the end of the iteration
    OwnershipSnapshot *snap = ownershipSnapshot(b.module);
    Builder::insertBefore(exit, r, b.drop(x));
    auto found = b.verify(snap);
    REQUIRE(found.size() >= 1);
    CHECK(found[0].rule == "O13");
    freeOwnershipSnapshot(snap);
}

TEST_CASE("Ownership verifier: B3 returned borrow of a local")
{
    Builder b;
    IrNode *bb = b.block("entry");
    IrNode *retVar = b.refVar(bb, "__ret");
    irSetInfo(retVar, irInfoSynthetic);
    irSetValueClass(retVar, irClassReturnSlot);
    IrNode *x = b.var(bb, "x");
    irAppendBlockStmt(bb, b.assign(x, b.value()));
    irAppendBlockStmt(bb, b.assign(retVar, b.borrow(x)));
    IrNode *r = b.ret();
    irAppendBlockStmt(bb, r);

    OwnershipSnapshot *snap = ownershipSnapshot(b.module);
    Builder::insertBefore(bb, r, b.drop(x));
    auto found = b.verify(snap);
    REQUIRE(found.size() == 1);
    CHECK(found[0].rule == "B3");
    CHECK(found[0].var == "x");
    freeOwnershipSnapshot(snap);
}

TEST_CASE("Ownership verifier: B4 borrow outlives its owner")
{
    SUBCASE("read after the owner was moved")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        IrNode *r = b.refVar(bb, "r");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        irAppendBlockStmt(bb, b.assign(r, b.borrow(x)));
        irAppendBlockStmt(bb, b.call(b.move(x)));
        irAppendBlockStmt(bb, b.call(b.ref(r)));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "B4");
        CHECK(found[0].var == "r");
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("last read before the owner is moved")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        IrNode *r = b.refVar(bb, "r");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        irAppendBlockStmt(bb, b.assign(r, b.borrow(x)));
        irAppendBlockStmt(bb, b.call(b.ref(r)));
        irAppendBlockStmt(bb, b.call(b.move(x)));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        CHECK(b.verify(snap).empty());
        freeOwnershipSnapshot(snap);
    }
}

TEST_CASE("Ownership verifier: O5 field moved out and not cleared")
{
    auto build = [](bool clearField) {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        IrNode *y = b.var(bb, "y");
        irAppendBlockStmt(bb, b.assign(x, b.value()));

        FileLoc loc = b.at();
        irAppendBlockStmt(bb, b.assign(y, makeIrMove(&b.pool, &loc, b.field(x))));
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        if (clearField) {
            FileLoc l2 = b.at();
            IrNode *zero = makeIrAssign(&b.pool, &l2, b.field(x), makeIrNullConst(&b.pool, &l2, b.cls));
            Builder::insertBefore(bb, r, zero);
        }
        Builder::insertBefore(bb, r, b.drop(y));
        Builder::insertBefore(bb, r, b.drop(x));
        auto found = b.verify(snap);
        freeOwnershipSnapshot(snap);
        return found;
    };

    SUBCASE("field cleared after the move")
    {
        CHECK(build(true).empty());
    }

    SUBCASE("field left in place")
    {
        auto found = build(false);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O5");
        CHECK(found[0].var == "x");
    }
}

TEST_CASE("Ownership verifier: O5 on the normalized form, a captured field must be cleared")
{
    auto build = [](bool clearField) {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        IrNode *held = b.var(bb, "held");
        irSetInfo(held, irInfoSynthetic);
        irSetValueClass(held, irClassInFlight);
        irAppendBlockStmt(bb, b.assign(x, b.value()));

        // `held = x.f` then `x.f = null`, what normalize writes for `&&x.f`
        IrNode *capture = b.assign(held, b.field(x));
        irSetInfo(capture, irInfoImplicit);
        capture->flags |= flgUnmanaged;
        irAppendBlockStmt(bb, capture);
        if (clearField) {
            FileLoc loc = b.at();
            IrNode *zero = makeIrAssign(&b.pool, &loc, b.field(x), makeIrNullConst(&b.pool, &loc, b.cls));
            zero->flags |= flgUnmanaged;
            irAppendBlockStmt(bb, zero);
        }
        IrNode *r = b.ret();
        irAppendBlockStmt(bb, r);

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        Builder::insertBefore(bb, r, b.drop(x));
        auto found = b.verify(snap);
        freeOwnershipSnapshot(snap);
        return found;
    };

    SUBCASE("field cleared after it was read")
    {
        CHECK(build(true).empty());
    }

    SUBCASE("field left in place")
    {
        auto found = build(false);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O5");
        CHECK(found[0].var == "x");
    }
}

TEST_CASE("Ownership verifier: O6 a reference cannot be moved out of")
{
    SUBCASE("a `&&r` the user wrote")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *r = b.refVar(bb, "r");
        IrNode *mv = b.move(r);
        irSetInfo(mv, irInfoUserWritten);
        irAppendBlockStmt(bb, b.call(mv));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        auto found = b.verify(snap);
        REQUIRE(found.size() == 1);
        CHECK(found[0].rule == "O6");
        CHECK(found[0].var == "r");
        CHECK(found[0].definite);
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("the same move inserted by the compiler is not the user's")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *r = b.refVar(bb, "r");
        irAppendBlockStmt(bb, b.call(b.move(r)));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        CHECK(b.verify(snap).empty());
        freeOwnershipSnapshot(snap);
    }

    SUBCASE("a `&&x` of an owner is fine")
    {
        Builder b;
        IrNode *bb = b.block("entry");
        IrNode *x = b.var(bb, "x");
        irAppendBlockStmt(bb, b.assign(x, b.value()));
        IrNode *mv = b.move(x);
        irSetInfo(mv, irInfoUserWritten);
        irAppendBlockStmt(bb, b.call(mv));
        irAppendBlockStmt(bb, b.ret());

        OwnershipSnapshot *snap = ownershipSnapshot(b.module);
        CHECK(b.verify(snap).empty());
        freeOwnershipSnapshot(snap);
    }
}
