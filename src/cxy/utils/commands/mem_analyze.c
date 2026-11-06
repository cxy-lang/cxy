/**
 * `cxy utils mem analyze`: says whether the memory of a traced program looks normal.
 * The first call takes a baseline in the program (`mark`), later calls compare with
 * it. The checks live in utils/mem/rules.c, this file only talks to the program.
 * Design: docs/stage/memory-analyze.md.
 */

#include "utils/commands/mem_client.h"

#include "cJSON.h"
#include "core/log.h"
#include "utils/mem/report.h"
#include "utils/mem/rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXIT_UNREACHABLE 2 // the program cannot be reached
#define EXIT_FAIL_ON 3     // a finding reached --fail-on

typedef struct {
    char *data;
    size_t length, capacity;
} Reply;

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

// One request, the whole reply without its final newline
static bool fetch(const char *path, const char *request, i64 timeout, Reply *reply, Log *log)
{
    *reply = (Reply){0};
    char error[512];
    if (!memRequest(path, request, timeout, appendReply, reply, error, sizeof(error))) {
        logError(log, NULL, "{s}", (FormatArg[]){{.s = error}});
        freeReply(reply);
        return false;
    }
    if (reply->data == NULL) {
        logError(log, NULL, "the program answered nothing", NULL);
        return false;
    }
    while (reply->length > 0 && (reply->data[reply->length - 1] == '\n' ||
                                 reply->data[reply->length - 1] == '\r'))
        reply->data[--reply->length] = '\0';
    return true;
}

// `mark json` answered {"mark": n, ...}, or an error document
static int takeBaseline(const char *path, i64 timeout, Log *log)
{
    Reply reply;
    if (!fetch(path, "mark json", timeout, &reply, log))
        return EXIT_UNREACHABLE;
    cJSON *doc = cJSON_Parse(reply.data);
    cJSON *mark = doc != NULL ? cJSON_GetObjectItem(doc, "mark") : NULL;
    int code = EXIT_SUCCESS;
    if (mark == NULL || (mark->type & 0xFF) != cJSON_Number) {
        logError(log,
                 NULL,
                 "the program does not understand `mark json`, was it built with the current "
                 "cxy and --memory-trace?",
                 NULL);
        code = EXIT_FAILURE;
    }
    else {
        printf("baseline taken (mark %llu). Run the workload, then run `cxy utils mem analyze` "
               "again.\n",
               (unsigned long long)mark->valuedouble);
    }
    cJSON_Delete(doc);
    freeReply(&reply);
    return code;
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

    char path[256];
    if (!memResolveSocket(options, path, sizeof(path), log))
        return EXIT_UNREACHABLE;
    i64 timeout = options->utils.memTimeout;

    if (options->utils.memReset)
        return takeBaseline(path, timeout, log);

    Reply baselineReply;
    if (!fetch(path, "baseline json", timeout, &baselineReply, log))
        return EXIT_UNREACHABLE;
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
        return takeBaseline(path, timeout, log);
    }

    Reply snapshotReply;
    if (!fetch(path, "snapshot json", timeout, &snapshotReply, log)) {
        freeReply(&baselineReply);
        return EXIT_UNREACHABLE;
    }

    MemSnapshot baseline, snapshot;
    char error[256];
    int code = EXIT_SUCCESS;
    bool parsedBaseline = memSnapshotParse(baselineReply.data, &baseline, error, sizeof(error));
    bool parsedSnapshot = parsedBaseline &&
                          memSnapshotParse(snapshotReply.data, &snapshot, error, sizeof(error));
    if (!parsedSnapshot) {
        logError(log, NULL, "{s}", (FormatArg[]){{.s = error}});
        code = EXIT_FAILURE;
    }
    else {
        MemConfig config = memDefaultConfig();
        MemReport *report = calloc(1, sizeof(MemReport));
        if (report == NULL) {
            logError(log, NULL, "out of memory", NULL);
            code = EXIT_FAILURE;
        }
        else {
            memAnalyze(&baseline, &snapshot, 1, &config, report);
            memReportText(report, &snapshot, config.minActivity, stdout);
            fflush(stdout);
            if (useFailOn && memReached(report, failOn))
                code = EXIT_FAIL_ON;
            free(report);
        }
        memSnapshotFree(&snapshot);
    }
    if (parsedBaseline)
        memSnapshotFree(&baseline);
    freeReply(&baselineReply);
    freeReply(&snapshotReply);
    return code;
}
