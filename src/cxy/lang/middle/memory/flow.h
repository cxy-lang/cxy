#pragma once

#include "bitset.h"
#include "cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

// What a statement does to the ownership of one variable. This is the only
// definition of it: the dataflow, the drop flag analysis and the elaboration of
// drops all change their state through `applyEvent`.
typedef enum FlowEvent {
    flowDef,  // a value is stored: owned from here on, on every path
    flowKill  // moved, dropped or set empty: owned on no path
} FlowEvent;

// `may` holds the variables owned on some path, `must` on every path
void applyEvent(BitSet *may, BitSet *must, u64 var, FlowEvent event);

// The gen and kill sets of a block, built from its events in order. A variable
// is in `gen` when its last event is a store, in `kill` when it is a kill, and
// in neither when the block does not touch it.
typedef struct BlockEffects {
    BitSet fromNone; // the state at the end of the block when nothing was owned
    BitSet fromAll;  // the same when everything was
    BitSet unusedMust;
} BlockEffects;

BlockEffects makeBlockEffects(MemPool *pool, u64 varCount);
void blockEffectsApply(BlockEffects *effects, u64 var, FlowEvent event);
// Sets `block->gen` and `block->kill`
void blockEffectsFinish(const BlockEffects *effects, CfgBlockInfo *block, u64 varCount);

#ifdef __cplusplus
}
#endif
