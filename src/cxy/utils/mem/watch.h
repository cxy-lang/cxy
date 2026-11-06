/**
 * `cxy utils mem analyze --watch`: the sampling loop and the duration syntax. The loop
 * does not know about sockets or clocks, it is given them, so it can be tested with a
 * fake clock.
 */

#pragma once

#include "utils/mem/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * `500ms`, `30s`, `2m`, `1h`, `1.5s`. A number alone is seconds. False for anything
 * else, a zero or a negative duration.
 */
bool memParseDuration(const char *text, u64 *milliseconds);

typedef struct {
    u64 (*now)(void *ctx);                // a monotonic clock, in milliseconds
    void (*wait)(void *ctx, u64 milliseconds);
    // Takes one snapshot, false with a message when the program cannot be reached
    bool (*take)(void *ctx, u32 index, MemSnapshot *out, char *error, size_t errorSize);
} MemWatchOps;

/**
 * Takes `samples` snapshots into `out`, the k-th at k * `intervalMs` after the start
 * (a snapshot that takes a while does not push the later ones back). On failure every
 * snapshot taken so far is released and the message is in `error`.
 */
bool memWatchRun(const MemWatchOps *ops,
                 void *ctx,
                 u32 samples,
                 u64 intervalMs,
                 MemSnapshot *out,
                 char *error,
                 size_t errorSize);

#ifdef __cplusplus
}
#endif
