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
    if (!request(t, "snapshot json", &reply, error, errorSize))
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
    Target target = {.path = path,
                     .timeout = options->utils.memTimeout,
                     .log = log,
                     .json = options->utils.memJson};

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
