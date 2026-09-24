#pragma once

#include <stddef.h>
#include "core/array.h"
#include "core/htable.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "driver/driver.h"
#include "lang/middle/ir/node.h"
#include "lang/middle/ir/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IrVariableState {
    irVarUninitialized = 0, // Declared, no value stored
    irVarAssigned,          // Valid initialized value, must be dropped on exit
    irVarMoved,             // Ownership transferred (&&x), must not be dropped
    irVarMaybeAssigned      // Conditionally assigned/moved across branches (needs drop flag)
} IrVariableState;

typedef struct TrackedVar {
    IrNode *decl;           // The irVarDecl or irFuncParam node
    cstring name;           // Variable name
    u32 scopeId;            // Scope ID where declared
    u32 varIndex;           // Index in trackedVarList / BitSet
    u32 dropFlagIndex;      // Bit index in __drop_flags struct
    bool needsDropFlag;     // True if variable state ever becomes MaybeAssigned
    bool isTemporary;       // True if compiler temporary
    bool isEphemeral;       // True if ephemeral discarded call result (dropped eagerly)
    const IrType *type;     // IR type of the variable
    IrVariableState state;  // Current state during traversal
} TrackedVar;

typedef struct BitSet {
    u64 *words;
    u64 count;
} BitSet;

static inline BitSet makeBitSet(MemPool *pool, u64 count)
{
    u64 numWords = (count + 63) / 64;
    if (numWords == 0)
        numWords = 1;
    BitSet bs = {
        .words = allocFromMemPool(pool, sizeof(u64) * numWords),
        .count = count
    };
    memset(bs.words, 0, sizeof(u64) * numWords);
    return bs;
}

static inline void bitSetSet(BitSet *bs, u64 index)
{
    if (index < bs->count) {
        bs->words[index / 64] |= (1ULL << (index % 64));
    }
}

static inline void bitSetReset(BitSet *bs, u64 index)
{
    if (index < bs->count) {
        bs->words[index / 64] &= ~(1ULL << (index % 64));
    }
}

static inline bool bitSetTest(const BitSet *bs, u64 index)
{
    if (index >= bs->count)
        return false;
    return (bs->words[index / 64] & (1ULL << (index % 64))) != 0;
}

static inline void bitSetCopy(BitSet *dst, const BitSet *src)
{
    u64 words = (dst->count + 63) / 64;
    if (words == 0) words = 1;
    memcpy(dst->words, src->words, sizeof(u64) * words);
}

static inline void bitSetSetAll(BitSet *bs)
{
    u64 words = (bs->count + 63) / 64;
    if (words == 0) words = 1;
    memset(bs->words, 0xFF, sizeof(u64) * words);
    if (bs->count % 64 != 0) {
        u64 lastMask = (1ULL << (bs->count % 64)) - 1;
        bs->words[words - 1] &= lastMask;
    }
}

static inline void bitSetClearAll(BitSet *bs)
{
    u64 words = (bs->count + 63) / 64;
    if (words == 0) words = 1;
    memset(bs->words, 0, sizeof(u64) * words);
}

static inline bool bitSetUnion(BitSet *dst, const BitSet *src)
{
    u64 words = (dst->count + 63) / 64;
    if (words == 0) words = 1;
    bool changed = false;
    for (u64 i = 0; i < words; i++) {
        u64 prev = dst->words[i];
        dst->words[i] |= src->words[i];
        if (dst->words[i] != prev)
            changed = true;
    }
    return changed;
}

static inline bool bitSetIntersect(BitSet *dst, const BitSet *src)
{
    u64 words = (dst->count + 63) / 64;
    if (words == 0) words = 1;
    bool changed = false;
    for (u64 i = 0; i < words; i++) {
        u64 prev = dst->words[i];
        dst->words[i] &= src->words[i];
        if (dst->words[i] != prev)
            changed = true;
    }
    return changed;
}

static inline bool bitSetEquals(const BitSet *a, const BitSet *b)
{
    u64 words = (a->count + 63) / 64;
    if (words == 0) words = 1;
    for (u64 i = 0; i < words; i++) {
        if (a->words[i] != b->words[i])
            return false;
    }
    return true;
}

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

typedef struct MemScope {
    u32 scopeId;            // Scope ID
    u32 parentScopeId;      // Parent scope ID
    DynArray vars;          // DynArray of (TrackedVar *) declared in this scope
    IrNode *cleanupBlock;   // Landing pad basic block for this scope
    bool isLoop;            // True if this scope represents a loop iteration
} MemScope;

typedef struct BlockVarState {
    IrNode *block;          // The BasicBlock node
    HashTable inStates;     // IrNode* (decl) -> VariableState
    HashTable outStates;    // IrNode* (decl) -> VariableState
    bool visited;
} BlockVarState;

typedef struct MemMgmtContext {
    CompilerDriver *driver;
    MemPool *pool;
    MemPool *localPool;
    StrPool *strings;
    IrTypeTable *types;
    IrModule *module;
    IrFunction *currentFunc;

    HashTable trackedVars;  // IrNode* (decl) -> TrackedVar*
    DynArray trackedVarList; // Ordered list of TrackedVar*
    DynArray scopes;        // Stack of active MemScope
    HashTable blockStates;  // IrNode* (basicBlock) -> BlockVarState*
    DynArray dropFlagVars;  // TrackedVar* needing drop flags in current function
    IrNode *dropFlagsDecl;  // The __drop_flags varDecl in current function

    u32 currentScopeId;
    u32 tempCounter;
} MemMgmtContext;

bool isIrTypeDestructible(const IrType *type);
bool isIrTypeCopyable(const IrType *type);

IrModule *runMemoryManagement(CompilerDriver *driver, IrModule *module);
IrModule *runFinalize(CompilerDriver *driver, IrModule *module);

#ifdef __cplusplus
}
#endif
