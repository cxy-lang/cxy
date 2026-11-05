#pragma once

#include "core/mempool.h"
#include "core/utils.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// A fixed size set of small integers, one bit each
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
        .words = (u64 *)allocFromMemPool(pool, sizeof(u64) * numWords),
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

#ifdef __cplusplus
}
#endif
