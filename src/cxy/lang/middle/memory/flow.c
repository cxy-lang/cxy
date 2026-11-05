#include "flow.h"

void applyEvent(BitSet *may, BitSet *must, u64 var, FlowEvent event)
{
    if (event == flowDef) {
        bitSetSet(may, var);
        bitSetSet(must, var);
    }
    else {
        bitSetReset(may, var);
        bitSetReset(must, var);
    }
}

BlockEffects makeBlockEffects(MemPool *pool, u64 varCount)
{
    BlockEffects effects = {
        .fromNone = makeBitSet(pool, varCount),
        .fromAll = makeBitSet(pool, varCount),
        .unusedMust = makeBitSet(pool, varCount),
    };
    bitSetSetAll(&effects.fromAll);
    return effects;
}

// The transfer of a block is `out = (in - kill) + gen`. Run from an empty and
// from a full state, the two runs give `gen` and, by what is missing, `kill`.
void blockEffectsApply(BlockEffects *effects, u64 var, FlowEvent event)
{
    applyEvent(&effects->fromNone, &effects->unusedMust, var, event);
    applyEvent(&effects->fromAll, &effects->unusedMust, var, event);
}

void blockEffectsFinish(const BlockEffects *effects, CfgBlockInfo *block, u64 varCount)
{
    bitSetCopy(&block->gen, &effects->fromNone);
    bitSetClearAll(&block->kill);
    for (u64 v = 0; v < varCount; v++) {
        if (!bitSetTest(&effects->fromAll, v))
            bitSetSet(&block->kill, v);
    }
}
