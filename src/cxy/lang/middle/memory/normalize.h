#pragma once

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// Rewrites a lowered function so every owned operand has one explicit shape,
// before the ownership analysis runs (docs/stage/memory-pass.md, section 5)
void normalizeFunction(MemMgmtContext *ctx, IrNode *func);

#ifdef __cplusplus
}
#endif
