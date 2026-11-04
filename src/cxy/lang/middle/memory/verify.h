#pragma once

#include "core/log.h"
#include "lang/middle/ir/node.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ownership verifier (docs/stage/memory-verifier.md).
 *
 * `ownershipSnapshot` runs before `runMemoryManagement` and records the moves,
 * uses and defs the user's code contains (the pass strips `irMove` nodes).
 * `ownershipVerify` runs after it and checks them against the drops the pass
 * emitted. It never changes the IR.
 */

typedef struct OwnershipFinding {
    const char *rule;       // "O1", "O2", ...
    bool definite;          // true: every path, false: some path
    const char *func;
    const char *var;
    FileLoc loc;
    bool hasOrigin;
    FileLoc origin;
    const char *originNote; // "moved here", "dropped here", "declared here"
    char message[192];
} OwnershipFinding;

typedef void (*OwnershipReporter)(void *ctx, const OwnershipFinding *finding);

typedef struct OwnershipSnapshot OwnershipSnapshot;

OwnershipSnapshot *ownershipSnapshot(IrNode *module);
void freeOwnershipSnapshot(OwnershipSnapshot *snapshot);

// Returns the number of findings
u64 ownershipVerify(const OwnershipSnapshot *snapshot,
                    IrNode *module,
                    OwnershipReporter reporter,
                    void *ctx);

// Reporters for a `Log *` context
void ownershipReportAsWarning(void *ctx, const OwnershipFinding *finding);
void ownershipReportAsError(void *ctx, const OwnershipFinding *finding);

#ifdef __cplusplus
}
#endif
