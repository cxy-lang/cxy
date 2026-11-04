#pragma once

#include "core/format.h"
#include "driver/driver.h"
#include "lang/middle/ir/node.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * S-expression dump of the IR, for tests. Every node is `(Tag children...)`.
 * Statements are one per line, expressions are inline.
 *
 *   (Module m
 *     (Function main.f (Param n Node) (Ret void)
 *       (Block _L_entry
 *         (VarDecl x Node)
 *         (Assign (VarRef x) (Call (GlobalRef make)))
 *         (Drop (VarRef x))
 *         (Return))))
 *
 * Types: a class handle is its name (`Node`), a reference is `&Node`, a raw
 * pointer is `^T`. Types with spaces or parentheses are quoted.
 */
typedef struct IrSexpOptions {
    // Rename compiler generated variables (`__t14`, `_m4`) by order of first
    // appearance in the function (`__t0`, `_m0`), so a test does not depend on
    // the global temporary counter.
    bool canonicalNames;
} IrSexpOptions;

void dumpIrToSexpState(const IrNode *node,
                       FormatState *state,
                       const IrSexpOptions *options);

AstNode *dumpIrToSexp(CompilerDriver *driver, const IrNode *module, FILE *file);

#ifdef __cplusplus
}
#endif
