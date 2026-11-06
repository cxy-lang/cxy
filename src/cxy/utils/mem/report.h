/**
 * The text of an `analyze` report. Pure, writes to a stream so a test can read it back.
 * The layout is described in docs/stage/memory-analyze.md section 3.
 */

#pragma once

#include "utils/mem/rules.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * `last` is the newest snapshot, the one the report talks about. `minActivity` is the
 * threshold of the configuration, used to explain a "not enough activity" verdict.
 */
void memReportText(const MemReport *report,
                   const MemSnapshot *last,
                   u64 minActivity,
                   FILE *out);

#ifdef __cplusplus
}
#endif
