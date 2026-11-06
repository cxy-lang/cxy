/**
 * What `snapshot json` and `baseline json` of the trace server (stdlib/trace/mem.c)
 * answer, parsed into plain structs for the rule engine (rules.h). Strings are owned
 * by the snapshot, release it with memSnapshotFree().
 */

#pragma once

#include "core/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u64 count, bytes;
} MemCount;

typedef struct {
    char *name;
    u64 allocs, frees, live, bytes;
} MemTypeStat;

// A site is identified by file, line and column
typedef struct {
    char *file, *function;
    u64 line, column;
    u64 allocs, bytes;  // cumulative
    u64 live, liveBytes; // alive now
    u64 fresh;          // alive now and born after the mark
} MemSiteStat;

// An object, buffer or global of the ownership graph
typedef struct {
    char *kind, *name, *type, *file;
    u64 line, size, refs;
} MemNode;

typedef struct {
    MemNode node;
    u64 exclusiveBytes, exclusiveObjects, reachBytes, reachObjects;
} MemRoot;

typedef struct {
    u64 nodes, bytes;
    MemNode *members;
    u32 memberCount; // the server lists at most 12
} MemCycle;

typedef struct {
    char *ownerType, *file, *dropFile;
    u64 line, dropLine;
} MemDangling;

typedef struct {
    u64 tick, timeMs, mark;
    MemCount objects, buffers, zombies;
    u64 objectsAllocated;
    MemTypeStat *types;
    u32 typeCount;
    MemSiteStat *sites;
    u32 siteCount;
    bool hasBaseline;
    u32 approximate; // threads that did not reach a safepoint

    // Only in a `snapshot`, not in a baseline
    bool hasGraph;
    u64 graphNodes, graphEdges;
    u64 externalObjects, externalRefs;
    MemRoot *roots;
    u32 rootCount;
    MemCycle *cycles;
    u32 cycleCount;
    u64 cycleTotal, cycleBytes; // all leaked cycles, `cycles` lists the largest
    bool hasDangling;
    MemDangling *dangling;
    u32 danglingCount;
    u64 danglingTotal;
} MemSnapshot;

/**
 * Parses one reply. False with a message in `error` when the text is not JSON, is an
 * error document, or is a baseline that does not exist.
 */
bool memSnapshotParse(const char *json, MemSnapshot *out, char *error, size_t errorSize);

void memSnapshotFree(MemSnapshot *snapshot);

#ifdef __cplusplus
}
#endif
