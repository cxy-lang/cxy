#pragma once

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// Gives every owning temporary lowering created (`:synthetic`) the class its uses
// decide: in flight, statement or extended (docs/stage/memory-pass.md, 4.1)
void classifyTemporaries(MemMgmtContext *ctx, IrNode *func);

#ifdef __cplusplus
}
#endif
