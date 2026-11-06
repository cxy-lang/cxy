/**
 * `cxy utils mem analyze`: says whether the memory of a traced program looks normal.
 * The first call takes a baseline in the program (`mark`), later calls compare with
 * it, `--watch` takes the baseline and several snapshots by itself. The checks live in
 * utils/mem/rules.c, this file only talks to the program.
 * Design: docs/stage/memory-analyze.md.
 */

#include "utils/commands/mem_client.h"

#include "cJSON.h"
#include "core/log.h"
#include "utils/mem/report.h"
#include "utils/mem/rules.h"
#include "utils/mem/suspects.h"
#include "utils/mem/watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXIT_UNREACHABLE 2 // the program cannot be reached
#define EXIT_FAIL_ON 3     // a finding reached --fail-on

typedef struct {
    char *data;
    size_t length, capacity;
} Reply;

typedef struct {
    const char *path;
    i64 timeout;
    Log *log;
    bool json;
    bool noGraph;
    u32 suspects; // growing sites to look into
} Target;

static void appendReply(const char *data, size_t length, void *ctx)
{
    Reply *r = ctx;
    if (r->length + length + 1 > r->capacity) {
        size_t capacity = r->capacity ? r->capacity * 2 : 4096;
        while (capacity < r->length + length + 1)
            capacity *= 2;
        char *grown = realloc(r->data, capacity);
        if (grown == NULL)
            return;
        r->data = grown;
        r->capacity = capacity;
    }
    memcpy(r->data + r->length, data, length);
    r->length += length;
    r->data[r->length] = '\0';
}

static void freeReply(Reply *r)
{
    free(r->data);
    *r = (Reply){0};
}

// One request, the whole reply without its final newline, or a message in `error`
static bool request(const Target *t, const char *line, Reply *reply, char *error, size_t errorSize)
{
    *reply = (Reply){0};
    if (!memRequest(t->path, line, t->timeout, appendReply, reply, error, errorSize)) {
        freeReply(reply);
        return false;
    }
    if (reply->data == NULL) {
        snprintf(error, errorSize, "the program answered nothing");
        return false;
    }
    while (reply->length > 0 && (reply->data[reply->length - 1] == '\n' ||
                                 reply->data[reply->length - 1] == '\r'))
        reply->data[--reply->length] = '\0';
    return true;
}

static bool fetchSnapshot(const Target *t, MemSnapshot *out, char *error, size_t errorSize)
{
    Reply reply;
    if (!request(t, t->noGraph ? "snapshot nograph json" : "snapshot json", &reply, error, errorSize))
        return false;
    bool ok = memSnapshotParse(reply.data, out, error, errorSize);
    freeReply(&reply);
    return ok;
}

// `mark json` answered {"mark": n, ...}, or an error document
static bool markNow(const Target *t, u64 *mark)
{
    Reply reply;
    char error[512];
    if (!request(t, "mark json", &reply, error, sizeof(error))) {
        logError(t->log, NULL, "{s}", (FormatArg[]){{.s = error}});
        return false;
    }
    cJSON *doc = cJSON_Parse(reply.data);
    cJSON *item = doc != NULL ? cJSON_GetObjectItem(doc, "mark") : NULL;
    bool ok = item != NULL && (item->type & 0xFF) == cJSON_Number;
    if (ok)
        *mark = (u64)item->valuedouble;
    else
        logError(t->log,
                 NULL,
                 "the program does not understand `mark json`, was it built with the current "
                 "cxy and --memory-trace?",
                 NULL);
    cJSON_Delete(doc);
    freeReply(&reply);
    return ok;
}

static int takeBaseline(const Target *t)
{
    u64 mark;
    if (!markNow(t, &mark))
        return EXIT_UNREACHABLE;
    if (t->json)
        printf("{\"version\":1,\"baseline\":{\"taken\":true,\"mark\":%llu}}\n", (unsigned long long)mark);
    else
        printf("baseline taken (mark %llu). Run the workload, then run `cxy utils mem analyze` "
               "again.\n",
               (unsigned long long)mark);
    return EXIT_SUCCESS;
}

// {"exists": true, ...} when a baseline was taken, -1 when the reply is not a document
static int baselineExists(const Reply *reply)
{
    cJSON *doc = cJSON_Parse(reply->data);
    if (doc == NULL)
        return -1;
    cJSON *exists = cJSON_GetObjectItem(doc, "exists");
    int result = exists == NULL ? -1 : ((exists->type & 0xFF) == cJSON_True);
    cJSON_Delete(doc);
    return result;
}

/* ------------------------------------------------------------------- watch */

static u64 clockMs(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000 + (u64)(ts.tv_nsec / 1000000);
}

static void sleepMs(void *ctx, u64 milliseconds)
{
    (void)ctx;
    struct timespec left = {.tv_sec = (time_t)(milliseconds / 1000),
                            .tv_nsec = (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&left, &left) != 0)
        ;
}

static bool takeSample(void *ctx, u32 index, MemSnapshot *out, char *error, size_t errorSize)
{
    (void)index;
    return fetchSnapshot(ctx, out, error, errorSize);
}

/* ---------------------------------------------------------------- suspects */

// At most this many `path` queries per run: each one forks the program for a moment
#define SUSPECT_PATH_BUDGET 10
#define SUSPECT_SAMPLES_MAX 8
// Objects made since the baseline that are listed to pick samples from
#define SUSPECT_LIST_LIMIT 3000

static const char *textOf(const cJSON *object, const char *key)
{
    const cJSON *item = object != NULL ? cJSON_GetObjectItem(object, key) : NULL;
    return item != NULL && (item->type & 0xFF) == cJSON_String ? item->valuestring : NULL;
}

static u64 numberOf(const cJSON *object, const char *key)
{
    const cJSON *item = object != NULL ? cJSON_GetObjectItem(object, key) : NULL;
    return item != NULL && (item->type & 0xFF) == cJSON_Number ? (u64)item->valuedouble : 0;
}

// Samples objects of one growing site and follows their owners
static void investigateSite(const Target *t, MemFinding *finding, const cJSON *allocations, u32 samples)
{
    u32 total = (u32)cJSON_GetArraySize(allocations);
    u32 *matches = malloc((total + 1) * sizeof(u32));
    if (matches == NULL)
        return;
    u32 count = 0;
    for (u32 i = 0; i < total; i++) {
        const cJSON *entry = cJSON_GetArrayItem(allocations, (int)i);
        const char *kind = textOf(entry, "kind");
        const char *file = textOf(entry, "file");
        if (kind != NULL && strcmp(kind, "object") == 0 && file != NULL &&
            strcmp(file, finding->file) == 0 && numberOf(entry, "line") == finding->line)
            matches[count++] = i;
    }

    u32 picked[SUSPECT_SAMPLES_MAX];
    u32 wanted = samples < SUSPECT_SAMPLES_MAX ? samples : SUSPECT_SAMPLES_MAX;
    u32 n = memPickSamples(count, wanted, picked);
    MemShape shapes[SUSPECT_SAMPLES_MAX];
    char ptrs[SUSPECT_SAMPLES_MAX][24];
    u32 followed = 0;
    for (u32 i = 0; i < n; i++) {
        const cJSON *entry = cJSON_GetArrayItem(allocations, (int)matches[picked[i]]);
        const char *ptr = textOf(entry, "ptr");
        if (ptr == NULL)
            continue;
        char line[64], error[256];
        snprintf(line, sizeof(line), "path %s json", ptr);
        Reply reply;
        if (!request(t, line, &reply, error, sizeof(error)))
            continue;
        // The object may be gone by now, or the scan may have failed: then there is no shape
        if (memShapeFromPath(reply.data, &shapes[followed])) {
            snprintf(ptrs[followed], sizeof(ptrs[0]), "%s", ptr);
            followed++;
        }
        freeReply(&reply);
    }

    if (followed > 0) {
        u32 covered = 0;
        u32 best = memGroupShapes(shapes, followed, &covered);
        const char *type = textOf(cJSON_GetArrayItem(allocations, (int)matches[0]), "type");
        memApplySuspect(finding, type, ptrs[best], &shapes[best], followed, covered);
    }
    free(matches);
}

// Best effort: whatever cannot be asked is left out, the report is still right
static void investigate(const Target *t, MemReport *report)
{
    u32 targets[MEM_MAX_FINDINGS];
    u32 n = 0;
    for (u32 i = 0; i < report->count && n < t->suspects; i++) {
        if (report->findings[i].rule == memRuleG1 && report->findings[i].file[0] != '\0')
            targets[n++] = i;
    }
    if (n == 0)
        return;

    Reply list;
    char error[256];
    char line[64];
    snprintf(line, sizeof(line), "difflist %d json", SUSPECT_LIST_LIMIT);
    if (!request(t, line, &list, error, sizeof(error)))
        return;
    cJSON *doc = cJSON_Parse(list.data);
    const cJSON *allocations = doc != NULL ? cJSON_GetObjectItem(doc, "allocations") : NULL;
    if (allocations != NULL && (allocations->type & 0xFF) == cJSON_Array) {
        u32 perSite = SUSPECT_PATH_BUDGET / n;
        for (u32 i = 0; i < n; i++)
            investigateSite(t, &report->findings[targets[i]], allocations, perSite > 0 ? perSite : 1);
        memRefreshVerdict(report);
    }
    cJSON_Delete(doc);
    freeReply(&list);
}

/* ------------------------------------------------------------------ report */

// Prints the report of `baseline` and `samples` and returns the exit code
static int report(const Target *t,
                  const MemSnapshot *baseline,
                  const MemSnapshot *samples,
                  u32 count,
                  bool useFailOn,
                  MemSeverity failOn)
{
    MemConfig config = memDefaultConfig();
    MemReport *result = calloc(1, sizeof(MemReport));
    if (result == NULL) {
        logError(t->log, NULL, "out of memory", NULL);
        return EXIT_FAILURE;
    }
    memAnalyze(baseline, samples, count, &config, result);
    if (!t->noGraph && t->suspects > 0)
        investigate(t, result);
    if (t->json)
        memReportJson(result, &samples[count - 1], stdout);
    else
        memReportText(result, &samples[count - 1], config.minActivity, stdout);
    fflush(stdout);
    int code = useFailOn && memReached(result, failOn) ? EXIT_FAIL_ON : EXIT_SUCCESS;
    free(result);
    return code;
}

static bool fetchBaseline(const Target *t, MemSnapshot *out, char *error, size_t errorSize)
{
    Reply reply;
    if (!request(t, "baseline json", &reply, error, errorSize))
        return false;
    bool ok = memSnapshotParse(reply.data, out, error, errorSize);
    freeReply(&reply);
    return ok;
}

// --watch: a new baseline, then `samples` snapshots at equal intervals, one report
static int watch(const Target *t, u64 totalMs, u32 samples, bool useFailOn, MemSeverity failOn)
{
    u64 intervalMs = totalMs / samples;
    if (intervalMs == 0)
        intervalMs = 1;
    u64 mark;
    if (!markNow(t, &mark))
        return EXIT_UNREACHABLE;
    fprintf(stderr,
            "watching for %llu ms: %u snapshot%s, one every %llu ms\n",
            (unsigned long long)(intervalMs * samples),
            samples,
            samples == 1 ? "" : "s",
            (unsigned long long)intervalMs);

    char error[512];
    MemSnapshot baseline;
    if (!fetchBaseline(t, &baseline, error, sizeof(error))) {
        logError(t->log, NULL, "{s}", (FormatArg[]){{.s = error}});
        return EXIT_UNREACHABLE;
    }
    MemSnapshot *taken = calloc(samples, sizeof(MemSnapshot));
    if (taken == NULL) {
        memSnapshotFree(&baseline);
        logError(t->log, NULL, "out of memory", NULL);
        return EXIT_FAILURE;
    }
    MemWatchOps ops = {.now = clockMs, .wait = sleepMs, .take = takeSample};
    int code;
    if (!memWatchRun(&ops, (void *)t, samples, intervalMs, taken, error, sizeof(error))) {
        logError(t->log, NULL, "{s}", (FormatArg[]){{.s = error}});
        code = EXIT_UNREACHABLE;
    }
    else {
        code = report(t, &baseline, taken, samples, useFailOn, failOn);
        for (u32 i = 0; i < samples; i++)
            memSnapshotFree(&taken[i]);
    }
    free(taken);
    memSnapshotFree(&baseline);
    return code;
}

/* ------------------------------------------------------------------- command */

int memAnalyzeCommand(const Options *options, Log *log)
{
    MemSeverity failOn = memInfo;
    bool useFailOn = false;
    const char *failOnText = options->utils.memFailOn;
    if (failOnText != NULL && failOnText[0] != '\0') {
        if (!memParseSeverity(failOnText, &failOn)) {
            logError(log,
                     NULL,
                     "invalid --fail-on '{s}', use info, watch, likely or definite",
                     (FormatArg[]){{.s = failOnText}});
            return EXIT_FAILURE;
        }
        useFailOn = true;
    }

    const char *watchText = options->utils.memWatch;
    bool watching = watchText != NULL && watchText[0] != '\0';
    u64 watchMs = 0;
    i64 samples = options->utils.memSamples;
    if (watching) {
        if (!memParseDuration(watchText, &watchMs)) {
            logError(log,
                     NULL,
                     "invalid --watch '{s}', use a duration such as 30s, 2m or 500ms",
                     (FormatArg[]){{.s = watchText}});
            return EXIT_FAILURE;
        }
        if (samples < 1 || samples > MEM_MAX_SAMPLES) {
            logError(log, NULL, "--samples must be between 1 and 32", NULL);
            return EXIT_FAILURE;
        }
        if (options->utils.memReset) {
            logError(log, NULL, "--watch always takes a new baseline, drop --reset", NULL);
            return EXIT_FAILURE;
        }
    }

    char path[256];
    if (!memResolveSocket(options, path, sizeof(path), log))
        return EXIT_UNREACHABLE;
    i64 suspects = options->utils.memSuspects;
    Target target = {.path = path,
                     .timeout = options->utils.memTimeout,
                     .log = log,
                     .json = options->utils.memJson,
                     .noGraph = options->utils.memNoGraph,
                     .suspects = suspects < 0 ? 0 : (suspects > 20 ? 20 : (u32)suspects)};

    if (watching)
        return watch(&target, watchMs, (u32)samples, useFailOn, failOn);
    if (options->utils.memReset)
        return takeBaseline(&target);

    Reply baselineReply;
    char error[512];
    if (!request(&target, "baseline json", &baselineReply, error, sizeof(error))) {
        logError(log, NULL, "{s}", (FormatArg[]){{.s = error}});
        return EXIT_UNREACHABLE;
    }
    int exists = baselineExists(&baselineReply);
    if (exists < 0) {
        logError(log,
                 NULL,
                 "the program does not understand `baseline json`, was it built with the current "
                 "cxy and --memory-trace?",
                 NULL);
        freeReply(&baselineReply);
        return EXIT_FAILURE;
    }
    if (exists == 0) {
        freeReply(&baselineReply);
        return takeBaseline(&target);
    }

    MemSnapshot baseline, snapshot;
    bool parsedBaseline = memSnapshotParse(baselineReply.data, &baseline, error, sizeof(error));
    freeReply(&baselineReply);
    bool parsedSnapshot = parsedBaseline && fetchSnapshot(&target, &snapshot, error, sizeof(error));
    int code = EXIT_FAILURE;
    if (!parsedSnapshot)
        logError(log, NULL, "{s}", (FormatArg[]){{.s = error}});
    else {
        code = report(&target, &baseline, &snapshot, 1, useFailOn, failOn);
        memSnapshotFree(&snapshot);
    }
    if (parsedBaseline)
        memSnapshotFree(&baseline);
    return code;
}
