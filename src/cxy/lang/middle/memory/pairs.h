#pragma once

#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// Retain and release pairs of class handles (design 8, plan phase 7b).
//
// `var b = a` retains the object and the scope exit releases it again. When `b` is
// only read, and `a` keeps its own count for as long as `b` lives, `b` is an alias:
// the retain becomes a plain assignment and the release goes. Runs after the
// memory pass has inserted every drop, the ownership verifier checks the result.
void eliminateRetainReleasePairs(MemMgmtContext *ctx, IrNode *func);

#ifdef __cplusplus
}
#endif
