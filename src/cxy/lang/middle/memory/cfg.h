#pragma once

#include "bitset.h"
#include "core/array.h"
#include "lang/middle/ir/node.h"

#ifdef __cplusplus
extern "C" {
#endif

// A basic block with the sets of the forward dataflow over a function. A bit is
// a tracked variable. `may` is owned on some path, `must` on every path.
typedef struct CfgBlockInfo {
    IrNode *bb;
    BitSet gen;
    BitSet kill;
    BitSet mayIn;
    BitSet mayOut;
    BitSet mustIn;
    BitSet mustOut;
    DynArray preds; // List of CfgBlockInfo*
    DynArray succs; // List of CfgBlockInfo*
} CfgBlockInfo;

// The blocks of `func` in order and the edges between them. The sets have
// `varCount` bits and are allocated in `pool`. `*outCount` is the block count.
CfgBlockInfo **buildCfg(MemPool *pool, IrNode *func, u64 varCount, u64 *outCount);

// Fills in the in and out sets from `gen` and `kill` of every block, to a fixed
// point. `entryOwned` is what is owned when the function is entered (its
// parameters), it also holds on a jump back to the entry block.
void solveOwnership(MemPool *pool,
                    CfgBlockInfo **blocks,
                    u64 count,
                    u64 varCount,
                    const BitSet *entryOwned);

#ifdef __cplusplus
}
#endif
