#pragma once

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// Rewrites a lowered function so every owned operand has one explicit shape,
// before the ownership analysis runs (docs/stage/memory-pass.md, section 5)
void normalizeFunction(MemMgmtContext *ctx, IrNode *func);

// Normalizes every function of the module. It runs before the ownership verifier
// takes its snapshot, so the verifier and the memory pass read the same form.
void runNormalization(CompilerDriver *driver, IrModule *module);

#ifdef __cplusplus
}
#endif
