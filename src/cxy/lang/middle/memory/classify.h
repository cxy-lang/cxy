#pragma once

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// Gives every owning temporary lowering created (`:synthetic`) the class its uses
// decide: in flight, statement, extended or result (docs/stage/memory-pass.md,
// 4.1), and the flags of the class
void classifyTemporaries(MemMgmtContext *ctx, IrNode *func);

// A statement temporary dies after the last statement that reads it. A branch
// condition that reads one is computed into a value first.
void dropStatementTemporaries(MemMgmtContext *ctx, IrNode *func);

#ifdef __cplusplus
}
#endif
