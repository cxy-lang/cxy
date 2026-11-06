#include "utils/mem/watch.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool memParseDuration(const char *text, u64 *milliseconds)
{
    if (text == NULL || !isdigit((unsigned char)text[0]))
        return false;
    char *end = NULL;
    double value = strtod(text, &end);
    if (end == text || !(value > 0))
        return false;

    double scale;
    if (*end == '\0' || strcmp(end, "s") == 0)
        scale = 1000;
    else if (strcmp(end, "ms") == 0)
        scale = 1;
    else if (strcmp(end, "m") == 0)
        scale = 60 * 1000;
    else if (strcmp(end, "h") == 0)
        scale = 60 * 60 * 1000;
    else
        return false;

    double total = value * scale;
    if (total < 1 || total > 7 * 24 * 60 * 60 * 1000.0)
        return false;
    *milliseconds = (u64)(total + 0.5);
    return true;
}

bool memWatchRun(const MemWatchOps *ops,
                 void *ctx,
                 u32 samples,
                 u64 intervalMs,
                 MemSnapshot *out,
                 char *error,
                 size_t errorSize)
{
    u64 start = ops->now(ctx);
    for (u32 i = 0; i < samples; i++) {
        u64 target = start + (u64)(i + 1) * intervalMs;
        u64 now = ops->now(ctx);
        if (now < target)
            ops->wait(ctx, target - now);
        if (!ops->take(ctx, i, &out[i], error, errorSize)) {
            for (u32 j = 0; j < i; j++)
                memSnapshotFree(&out[j]);
            return false;
        }
    }
    return true;
}
