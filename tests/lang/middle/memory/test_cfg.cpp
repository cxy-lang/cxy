/**
 * The control flow graph of a function and the ownership dataflow over it:
 * which variables are owned on some path (`may`) and on every path (`must`)
 * at the start and the end of each block.
 */

#include "doctest.h"

#include "core/mempool.h"
#include "lang/middle/ir/node.h"
#include "lang/middle/memory/cfg.h"

namespace {

struct Graph {
    MemPool pool = newMemPool();
    IrNode *func = nullptr;

    Graph() { func = makeIrFunction(&pool, nullptr, flgNone, "f", "f", nullptr, nullptr, 0); }
    ~Graph() { freeMemPool(&pool); }

    IrNode *block(const char *name)
    {
        IrNode *bb = makeIrBasicBlock(&pool, nullptr, name);
        irAppendFunctionBlock(func, bb);
        return bb;
    }

    void jump(IrNode *from, IrNode *to) { irAppendBlockStmt(from, makeIrBranch(&pool, nullptr, to)); }

    void branch(IrNode *from, IrNode *yes, IrNode *no)
    {
        IrNode *cond = makeIrBoolConst(&pool, nullptr, true, nullptr);
        irAppendBlockStmt(from, makeIrBranchIf(&pool, nullptr, cond, yes, no));
    }
};

bool hasEdge(const CfgBlockInfo *from, const CfgBlockInfo *to)
{
    for (size_t i = 0; i < from->succs.size; i++) {
        if (dynArrayAt(CfgBlockInfo **, &from->succs, i) == to)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("Bit set: set, reset, union and intersection")
{
    MemPool pool = newMemPool();
    BitSet a = makeBitSet(&pool, 70), b = makeBitSet(&pool, 70);
    bitSetSet(&a, 1);
    bitSetSet(&a, 65);
    bitSetSet(&b, 65);
    bitSetSet(&b, 2);
    CHECK(bitSetTest(&a, 65));
    CHECK_FALSE(bitSetTest(&a, 2));
    CHECK_FALSE(bitSetTest(&a, 500));

    BitSet u = makeBitSet(&pool, 70);
    bitSetCopy(&u, &a);
    CHECK(bitSetUnion(&u, &b));
    CHECK(bitSetTest(&u, 1));
    CHECK(bitSetTest(&u, 2));

    CHECK(bitSetIntersect(&u, &b));
    CHECK_FALSE(bitSetTest(&u, 1));
    CHECK(bitSetTest(&u, 65));

    bitSetSetAll(&u);
    CHECK(bitSetTest(&u, 69));
    CHECK_FALSE(bitSetTest(&u, 70));
    bitSetReset(&u, 3);
    CHECK_FALSE(bitSetEquals(&u, &a));
    freeMemPool(&pool);
}

TEST_CASE("CFG: the edges follow the branches")
{
    Graph g;
    IrNode *entry = g.block("entry"), *then = g.block("then"),
           *other = g.block("else"), *merge = g.block("merge");
    g.branch(entry, then, other);
    g.jump(then, merge);
    g.jump(other, merge);

    u64 count = 0;
    CfgBlockInfo **blocks = buildCfg(&g.pool, g.func, 1, &count);
    REQUIRE(count == 4);
    CHECK(hasEdge(blocks[0], blocks[1]));
    CHECK(hasEdge(blocks[0], blocks[2]));
    CHECK(hasEdge(blocks[1], blocks[3]));
    CHECK(hasEdge(blocks[2], blocks[3]));
    CHECK_FALSE(hasEdge(blocks[0], blocks[3]));
    CHECK(blocks[3]->preds.size == 2);
}

TEST_CASE("CFG: a function with no blocks has no graph")
{
    Graph g;
    u64 count = 7;
    CHECK(buildCfg(&g.pool, g.func, 1, &count) == nullptr);
    CHECK(count == 0);
}

TEST_CASE("Ownership dataflow: a value assigned on one branch is owned on some path")
{
    Graph g;
    IrNode *entry = g.block("entry"), *then = g.block("then"),
           *other = g.block("else"), *merge = g.block("merge");
    g.branch(entry, then, other);
    g.jump(then, merge);
    g.jump(other, merge);

    u64 count = 0;
    CfgBlockInfo **blocks = buildCfg(&g.pool, g.func, 1, &count);
    bitSetSet(&blocks[1]->gen, 0); // `then` assigns variable 0

    BitSet entryOwned = makeBitSet(&g.pool, 1);
    solveOwnership(&g.pool, blocks, count, 1, &entryOwned);

    CHECK_FALSE(bitSetTest(&blocks[0]->mayOut, 0));
    CHECK(bitSetTest(&blocks[1]->mustOut, 0));
    CHECK(bitSetTest(&blocks[3]->mayIn, 0));
    CHECK_FALSE(bitSetTest(&blocks[3]->mustIn, 0));
}

TEST_CASE("Ownership dataflow: assigned on both branches is owned on every path")
{
    Graph g;
    IrNode *entry = g.block("entry"), *then = g.block("then"),
           *other = g.block("else"), *merge = g.block("merge");
    g.branch(entry, then, other);
    g.jump(then, merge);
    g.jump(other, merge);

    u64 count = 0;
    CfgBlockInfo **blocks = buildCfg(&g.pool, g.func, 1, &count);
    bitSetSet(&blocks[1]->gen, 0);
    bitSetSet(&blocks[2]->gen, 0);

    BitSet entryOwned = makeBitSet(&g.pool, 1);
    solveOwnership(&g.pool, blocks, count, 1, &entryOwned);

    CHECK(bitSetTest(&blocks[3]->mayIn, 0));
    CHECK(bitSetTest(&blocks[3]->mustIn, 0));
}

TEST_CASE("Ownership dataflow: a parameter is owned at entry, a kill ends it")
{
    Graph g;
    IrNode *entry = g.block("entry"), *then = g.block("then"),
           *other = g.block("else"), *merge = g.block("merge");
    g.branch(entry, then, other);
    g.jump(then, merge);
    g.jump(other, merge);

    u64 count = 0;
    CfgBlockInfo **blocks = buildCfg(&g.pool, g.func, 1, &count);
    bitSetSet(&blocks[1]->kill, 0); // `then` moves the parameter

    BitSet entryOwned = makeBitSet(&g.pool, 1);
    bitSetSet(&entryOwned, 0);
    solveOwnership(&g.pool, blocks, count, 1, &entryOwned);

    CHECK(bitSetTest(&blocks[0]->mustIn, 0));
    CHECK_FALSE(bitSetTest(&blocks[1]->mayOut, 0));
    CHECK(bitSetTest(&blocks[2]->mustOut, 0));
    CHECK(bitSetTest(&blocks[3]->mayIn, 0));
    CHECK_FALSE(bitSetTest(&blocks[3]->mustIn, 0));
}

TEST_CASE("Ownership dataflow: a loop carries ownership back to its head")
{
    Graph g;
    IrNode *entry = g.block("entry"), *head = g.block("head"),
           *body = g.block("body"), *exit = g.block("exit");
    g.jump(entry, head);
    g.branch(head, body, exit);
    g.jump(body, head);

    u64 count = 0;
    CfgBlockInfo **blocks = buildCfg(&g.pool, g.func, 1, &count);
    bitSetSet(&blocks[2]->gen, 0); // the body assigns variable 0

    BitSet entryOwned = makeBitSet(&g.pool, 1);
    solveOwnership(&g.pool, blocks, count, 1, &entryOwned);

    // not owned on the first pass, owned after one iteration
    CHECK(bitSetTest(&blocks[1]->mayIn, 0));
    CHECK_FALSE(bitSetTest(&blocks[1]->mustIn, 0));
    CHECK(bitSetTest(&blocks[3]->mayIn, 0));
}
