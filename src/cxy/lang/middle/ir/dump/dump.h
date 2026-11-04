#pragma once

#include "lang/middle/ir/node.h"
#include "core/format.h"
#include "driver/driver.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void dumpIrType(FormatState *state, const IrType *type);
AstNode *dumpIr(CompilerDriver *driver, const IrNode *module, FILE *file);

#ifdef __cplusplus
}
#endif
