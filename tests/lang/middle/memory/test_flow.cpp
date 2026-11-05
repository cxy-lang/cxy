/**
 * The ownership events: what a store and a kill do to the `may` and `must`
 * sets, and how the gen and kill sets of a block follow from its events.
 */

#include "doctest.h"

#include "core/mempool.h"
#include "lang/middle/memory/flow.h"

namespace {

struct State {
    MemPool pool = newMemPool();
    BitSet may = makeBitSet(&pool, 4);
    BitSet must = makeBitSet(&pool, 4);
    ~State() { freeMemPool(&pool); }
};

} // namespace

TEST_CASE("Flow: a store owns the variable on every path, a kill on none")
{
    State s;
    applyEvent(&s.may, &s.must, 2, flowDef);
    CHECK(bitSetTest(&s.may, 2));
    CHECK(bitSetTest(&s.must, 2));
    CHECK_FALSE(bitSetTest(&s.may, 1));

    applyEvent(&s.may, &s.must, 2, flowKill);
    CHECK_FALSE(bitSetTest(&s.may, 2));
    CHECK_FALSE(bitSetTest(&s.must, 2));
}

TEST_CASE("Flow: a kill of a variable that is not owned changes nothing")
{
    State s;
    applyEvent(&s.may, &s.must, 0, flowDef);
    applyEvent(&s.may, &s.must, 1, flowKill);
    CHECK(bitSetTest(&s.may, 0));
    CHECK_FALSE(bitSetTest(&s.may, 1));
}

TEST_CASE("Flow: the last event of a variable decides gen and kill")
{
    MemPool pool = newMemPool();
    BlockEffects effects = makeBlockEffects(&pool, 4);
    blockEffectsApply(&effects, 0, flowDef);
    blockEffectsApply(&effects, 1, flowKill);
    blockEffectsApply(&effects, 2, flowDef);
    blockEffectsApply(&effects, 2, flowKill); // stored, then moved
    blockEffectsApply(&effects, 3, flowKill);
    blockEffectsApply(&effects, 3, flowDef); // moved, then stored again

    CfgBlockInfo block = {};
    block.gen = makeBitSet(&pool, 4);
    block.kill = makeBitSet(&pool, 4);
    blockEffectsFinish(&effects, &block, 4);

    CHECK(bitSetTest(&block.gen, 0));
    CHECK_FALSE(bitSetTest(&block.kill, 0));
    CHECK_FALSE(bitSetTest(&block.gen, 1));
    CHECK(bitSetTest(&block.kill, 1));
    CHECK_FALSE(bitSetTest(&block.gen, 2));
    CHECK(bitSetTest(&block.kill, 2));
    CHECK(bitSetTest(&block.gen, 3));
    CHECK_FALSE(bitSetTest(&block.kill, 3));
    freeMemPool(&pool);
}

TEST_CASE("Flow: a variable the block does not touch is in neither set")
{
    MemPool pool = newMemPool();
    BlockEffects effects = makeBlockEffects(&pool, 4);
    blockEffectsApply(&effects, 1, flowDef);

    CfgBlockInfo block = {};
    block.gen = makeBitSet(&pool, 4);
    block.kill = makeBitSet(&pool, 4);
    blockEffectsFinish(&effects, &block, 4);

    CHECK_FALSE(bitSetTest(&block.gen, 0));
    CHECK_FALSE(bitSetTest(&block.kill, 0));
    CHECK_FALSE(bitSetTest(&block.gen, 3));
    CHECK_FALSE(bitSetTest(&block.kill, 3));
    freeMemPool(&pool);
}
