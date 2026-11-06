/**
 * Looking into the suspects of rule G1: sample objects of a growing site, follow their
 * owners with `path`, group the answers by the chain of owners (the "shape") and report
 * the largest group. The functions here are pure, the client does the asking
 * (utils/commands/mem_analyze.c). Design: docs/stage/memory-analyze.md, "Looking into
 * suspects".
 */

#pragma once

#include "utils/mem/rules.h"

#ifdef __cplusplus
extern "C" {
#endif

// The chain of owners of one object, from its root down, the object itself left out
typedef struct {
    MemHeldBy heldBy;
    u32 stepCount;
    char steps[MEM_SHAPE_MAX][96];
} MemShape;

/**
 * Reads the answer of `path <ptr> json`. False for an error document or anything that
 * is not one (the object may be gone by now). A chain longer than MEM_SHAPE_MAX steps
 * keeps its first four and last three steps with `...` between.
 */
bool memShapeFromPath(const char *json, MemShape *out);

bool memShapeEqual(const MemShape *a, const MemShape *b);

/**
 * The shape most objects share: its index in `shapes` (the first of the group), and in
 * `covered` how many objects have it. Ties go to the group seen first.
 */
u32 memGroupShapes(const MemShape *shapes, u32 count, u32 *covered);

/**
 * Chooses `wanted` of `available` objects spread over the whole list, not only the
 * newest. Returns how many were chosen (never more than `available`), in `indexes`.
 */
u32 memPickSamples(u32 available, u32 wanted, u32 *indexes);

/**
 * Records what was found on a G1 finding: the class, the shape, and what it means. A
 * group of objects reachable only through cycles raises the finding one level, nothing
 * can free them. `ptr` is one of the sampled objects. Run memRefreshVerdict() after.
 */
void memApplySuspect(MemFinding *finding,
                     const char *type,
                     const char *ptr,
                     const MemShape *shape,
                     u32 sampled,
                     u32 covered);

#ifdef __cplusplus
}
#endif
