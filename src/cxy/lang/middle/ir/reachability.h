#pragma once

#include "driver/driver.h"
#include "lang/middle/ir/node.h"

#ifdef __cplusplus
extern "C" {
#endif

void runReachabilityAnalysis(CompilerDriver *driver);

#ifdef __cplusplus
}
#endif
