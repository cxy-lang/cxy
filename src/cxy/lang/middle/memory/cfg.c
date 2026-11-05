#include "cfg.h"

static CfgBlockInfo *getCfgBlock(CfgBlockInfo **blocks, u64 count, const IrNode *bb)
{
    if (bb == NULL)
        return NULL;
    for (u64 i = 0; i < count; i++) {
        if (blocks[i]->bb == bb)
            return blocks[i];
    }
    return NULL;
}

static void addCfgEdge(CfgBlockInfo *src, CfgBlockInfo *dst)
{
    if (src == NULL || dst == NULL)
        return;
    for (size_t i = 0; i < src->succs.size; i++) {
        if (dynArrayAt(CfgBlockInfo **, &src->succs, i) == dst)
            return;
    }
    pushOnDynArray(&src->succs, &dst);
    pushOnDynArray(&dst->preds, &src);
}

CfgBlockInfo **buildCfg(MemPool *pool, IrNode *func, u64 numVars, u64 *outCount)
{
    u64 count = 0;
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag == irBasicBlock)
            count++;
    }

    *outCount = count;
    if (count == 0)
        return NULL;

    CfgBlockInfo **blocks = allocFromMemPool(pool, sizeof(CfgBlockInfo *) * count);
    u64 idx = 0;

    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        CfgBlockInfo *info = allocFromMemPool(pool, sizeof(CfgBlockInfo));
        info->bb = bb;
        info->gen = makeBitSet(pool, numVars);
        info->kill = makeBitSet(pool, numVars);
        info->mayIn = makeBitSet(pool, numVars);
        info->mayOut = makeBitSet(pool, numVars);
        info->mustIn = makeBitSet(pool, numVars);
        info->mustOut = makeBitSet(pool, numVars);
        info->preds = newDynArray(sizeof(CfgBlockInfo *));
        info->succs = newDynArray(sizeof(CfgBlockInfo *));
        blocks[idx++] = info;
    }

    for (u64 i = 0; i < count; i++) {
        CfgBlockInfo *src = blocks[i];
        for (IrNode *stmt = src->bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irBranch) {
                addCfgEdge(src, getCfgBlock(blocks, count, stmt->branch.target));
            }
            else if (stmt->tag == irBranchIf) {
                addCfgEdge(src, getCfgBlock(blocks, count, stmt->branchIf.trueTarget));
                addCfgEdge(src, getCfgBlock(blocks, count, stmt->branchIf.falseTarget));
            }
            else if (stmt->tag == irSwitch) {
                for (u64 c = 0; c < stmt->switchStmt.casesCount; c++) {
                    addCfgEdge(src, getCfgBlock(blocks, count, stmt->switchStmt.cases[c].target));
                }
                if (stmt->switchStmt.defaultTarget)
                    addCfgEdge(src, getCfgBlock(blocks, count, stmt->switchStmt.defaultTarget));
            }
        }
    }

    return blocks;
}

// out = (in without kill) with gen
static void applyTransfer(const CfgBlockInfo *block,
                          const BitSet *in,
                          BitSet *out,
                          u64 varCount)
{
    bitSetCopy(out, in);
    for (u64 v = 0; v < varCount; v++) {
        if (bitSetTest(&block->kill, v))
            bitSetReset(out, v);
    }
    bitSetUnion(out, &block->gen);
}

void solveOwnership(MemPool *pool,
                    CfgBlockInfo **blocks,
                    u64 count,
                    u64 varCount,
                    const BitSet *entryOwned)
{
    if (count == 0 || varCount == 0)
        return;

    CfgBlockInfo *entry = blocks[0];
    bitSetUnion(&entry->mayIn, entryOwned);
    bitSetUnion(&entry->mustIn, entryOwned);
    applyTransfer(entry, &entry->mayIn, &entry->mayOut, varCount);
    applyTransfer(entry, &entry->mustIn, &entry->mustOut, varCount);

    // Optimistic start: nothing is owned on some path, everything on all paths
    for (u64 i = 1; i < count; i++) {
        bitSetClearAll(&blocks[i]->mayIn);
        bitSetCopy(&blocks[i]->mayOut, &blocks[i]->gen);

        bitSetSetAll(&blocks[i]->mustIn);
        applyTransfer(blocks[i], &blocks[i]->mustIn, &blocks[i]->mustOut, varCount);
    }

    BitSet tempMay = makeBitSet(pool, varCount);
    BitSet tempMust = makeBitSet(pool, varCount);
    bool changed = true;

    while (changed) {
        changed = false;
        for (u64 i = 0; i < count; i++) {
            CfgBlockInfo *block = blocks[i];
            if (block->preds.size > 0) {
                bitSetClearAll(&tempMay);
                bitSetSetAll(&tempMust);

                for (size_t p = 0; p < block->preds.size; p++) {
                    CfgBlockInfo *pred = dynArrayAt(CfgBlockInfo **, &block->preds, p);
                    bitSetUnion(&tempMay, &pred->mayOut);
                    bitSetIntersect(&tempMust, &pred->mustOut);
                }

                if (i == 0) {
                    bitSetUnion(&tempMay, entryOwned);
                    bitSetUnion(&tempMust, entryOwned);
                }

                if (!bitSetEquals(&block->mayIn, &tempMay)) {
                    bitSetCopy(&block->mayIn, &tempMay);
                    changed = true;
                }
                if (!bitSetEquals(&block->mustIn, &tempMust)) {
                    bitSetCopy(&block->mustIn, &tempMust);
                    changed = true;
                }
            }

            applyTransfer(block, &block->mayIn, &tempMay, varCount);
            applyTransfer(block, &block->mustIn, &tempMust, varCount);

            if (!bitSetEquals(&block->mayOut, &tempMay)) {
                bitSetCopy(&block->mayOut, &tempMay);
                changed = true;
            }
            if (!bitSetEquals(&block->mustOut, &tempMust)) {
                bitSetCopy(&block->mustOut, &tempMust);
                changed = true;
            }
        }
    }
}
