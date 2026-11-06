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

/**
 * The report as one line of JSON, the format of docs/stage/memory-analyze.md section 7.
 * Fields are only ever added, `version` changes when one is renamed or removed.
 */
void memReportJson(const MemReport *report, const MemSnapshot *last, FILE *out);

#ifdef __cplusplus
}
#endif
