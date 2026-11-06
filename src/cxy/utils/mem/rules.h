/**
 * The rules of `cxy utils mem analyze`: pure functions from snapshots of a traced
 * program to findings. No I/O, so every rule can be tested with synthetic snapshots.
 * Design and thresholds: docs/stage/memory-analyze.md.
 */

#pragma once

#include "utils/mem/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_MAX_FINDINGS 64
#define MEM_MAX_SAMPLES 32

typedef enum { memInfo, memWatch, memLikely, memDefinite } MemSeverity;

// One bit each in MemReport.ran
typedef enum {
    memRuleG1, // survivor ratio of an allocation site
    memRuleG2, // plateau: growth is slowing
    memRuleG3, // a type that is never freed
    memRuleC1, // leaked cycles
    memRuleD1, // dangling borrows
    memRuleZ1, // zombies increasing
    memRuleK1, // one root holds most of the heap
    memRuleCount
} MemRule;

typedef enum {
    memVerdictOk,
    memVerdictNotEnough, // too little activity to tell, this is not "ok"
    memVerdictWatch,
    memVerdictLikely,
    memVerdictDefinite
} MemVerdict;

typedef struct {
    const char *key;
    double value;
} MemEvidence;

typedef struct {
    MemRule rule;
    MemSeverity severity;
    char subject[200]; // what it is about: `file:line`, a type, a root
    char title[256];
    char fix[160];
    char see[96]; // a command that shows more
    MemEvidence evidence[8];
    u32 evidenceCount;
} MemFinding;

typedef struct {
    u64 minCount;           // G1, G3: fewer objects than this say nothing
    double minRatio;        // G1: alive / made
    u64 minActivity;        // objects allocated in the window, below it: "not enough activity"
    double plateauFactor;   // G2: the latest growth rate is at most this part of the previous one
    u64 minZombies;         // Z1: growth below this is noise
    double concentration;   // K1: share of live bytes held by one root
    u64 concentrationBytes; // K1: and at least this many
    u32 maxSiteFindings;    // G1: report this many sites at most
    const char *const *ignoreTypes; // G3: NULL terminated names, runtime objects
} MemConfig;

typedef struct {
    MemFinding findings[MEM_MAX_FINDINGS]; // most severe first
    u32 count;
    u32 ran; // bit per MemRule that could run and had the input it needs
    MemVerdict verdict;
    bool approximate; // a thread did not reach a safepoint in some snapshot
    bool enoughActivity;
    bool plateau; // G2: growth is slowing
    u32 samples;  // snapshots after the baseline
    double windowSeconds;
    i64 deltaObjects, deltaBytes; // live objects and bytes, first to last snapshot
    double objectsPerSecond, bytesPerSecond;
} MemReport;

MemConfig memDefaultConfig(void);

/**
 * Runs every rule. `baseline` is the set `mark` stored (NULL when there is none, the
 * rules that compare then do not run). `samples` are the snapshots taken since, oldest
 * first, at most MEM_MAX_SAMPLES are used (the newest).
 */
void memAnalyze(const MemSnapshot *baseline,
                const MemSnapshot *samples,
                u32 sampleCount,
                const MemConfig *config,
                MemReport *report);

/** True when some finding is at least `threshold` severe (`--fail-on`). */
bool memReached(const MemReport *report, MemSeverity threshold);

/** Parses `info`, `watch`, `likely` or `definite`. */
bool memParseSeverity(const char *text, MemSeverity *out);

/** `1.2 MB`, `3.0 KB`, `512 B`. */
void memFormatBytes(char *out, size_t size, u64 bytes);

const char *memRuleId(MemRule rule);
const char *memSeverityName(MemSeverity severity);
const char *memVerdictName(MemVerdict verdict);
/** The verdict as a token for JSON: ok, notEnoughActivity, watch, likely, definite. */
const char *memVerdictId(MemVerdict verdict);

#ifdef __cplusplus
}
#endif
