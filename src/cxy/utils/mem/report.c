#include "utils/mem/report.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

static const char *verdictTitle(MemVerdict verdict)
{
    static const char *const titles[] = {
        "OK", "NOT ENOUGH ACTIVITY", "WATCH", "LIKELY PROBLEM", "DEFINITE PROBLEM"};
    return titles[verdict];
}

static const char *severityLabel(MemSeverity severity)
{
    static const char *const labels[] = {"[info]    ", "[watch]   ", "[likely]  ", "[definite]"};
    return labels[severity];
}

// What a rule that ran and found nothing says in the "ok" line
static const char *quietPhrase(MemRule rule)
{
    switch (rule) {
        case memRuleG1: return "no allocation site keeps what it makes";
        case memRuleG3: return "no type that is never freed";
        case memRuleC1: return "no leaked cycles";
        case memRuleD1: return "no dangling borrows";
        case memRuleZ1: return "no growing zombies";
        case memRuleK1: return "no root holding most of the heap";
        default: return NULL; // G2 only ever explains another finding
    }
}

static void formatSigned(char *out, size_t size, i64 value)
{
    char bytes[32];
    memFormatBytes(bytes, sizeof(bytes), (u64)(value < 0 ? -value : value));
    snprintf(out, size, "%c%s", value < 0 ? '-' : '+', bytes);
}

static void formatWindow(char *out, size_t size, double seconds)
{
    if (seconds <= 0)
        snprintf(out, size, "<1s");
    else if (seconds < 60)
        snprintf(out, size, "%.0fs", seconds);
    else
        snprintf(out, size, "%dm%02ds", (int)seconds / 60, (int)seconds % 60);
}

static const char *heldByToken(MemHeldBy heldBy)
{
    switch (heldBy) {
        case memHeldGlobal: return "global";
        case memHeldOutside: return "outside";
        case memHeldCycle: return "cycle";
        default: return "none";
    }
}

// `global cache -> Vector -> buffer app.cxy:13`, how the objects are kept alive
static void describeRetention(const MemRetention *r, char *out, size_t size)
{
    char chain[384] = "";
    size_t len = 0;
    for (u32 i = 0; i < r->stepCount && len < sizeof(chain); i++)
        len += (size_t)snprintf(chain + len,
                                sizeof(chain) - len,
                                "%s%s",
                                i > 0 ? " -> " : "",
                                r->steps[i]);
    if (r->heldBy == memHeldCycle)
        snprintf(out, size, "nothing outside a cycle");
    else if (r->heldBy == memHeldOutside && len == 0)
        snprintf(out, size, "held from outside the heap");
    else if (r->heldBy == memHeldOutside)
        snprintf(out, size, "%s (held from outside the heap)", chain);
    else
        snprintf(out, size, "%s", chain);
}

static bool findingFor(const MemReport *report, MemRule rule)
{
    for (u32 i = 0; i < report->count; i++) {
        if (report->findings[i].rule == rule)
            return true;
    }
    return false;
}

void memReportText(const MemReport *report, const MemSnapshot *last, u64 minActivity, FILE *out)
{
    char head[96], window[32];
    snprintf(head, sizeof(head), "verdict: %s", verdictTitle(report->verdict));
    formatWindow(window, sizeof(window), report->windowSeconds);
    fprintf(out,
            "%-40s window: %s, %u sample%s\n\n",
            head,
            window,
            report->samples,
            report->samples == 1 ? "" : "s");

    char live[32], buffers[32], deltaBytes[40];
    memFormatBytes(live, sizeof(live), last->objects.bytes);
    memFormatBytes(buffers, sizeof(buffers), last->buffers.bytes);
    formatSigned(deltaBytes, sizeof(deltaBytes), report->deltaBytes);
    fprintf(out,
            "memory       live %s in %llu object%s (%+lld objects since the baseline)\n"
            "             buffers %s   zombies %llu\n",
            live,
            (unsigned long long)last->objects.count,
            last->objects.count == 1 ? "" : "s",
            (long long)report->deltaObjects,
            buffers,
            (unsigned long long)last->zombies.count);
    if (report->windowSeconds > 0) {
        char perSecond[40];
        formatSigned(perSecond, sizeof(perSecond), (i64)report->bytesPerSecond);
        fprintf(out,
                "growth       %s (objects and buffers), %+.1f objects/s, %s/s\n",
                deltaBytes,
                report->objectsPerSecond,
                perSecond);
    }
    fprintf(out, "\n");

    for (u32 i = 0; i < report->count; i++) {
        const MemFinding *f = &report->findings[i];
        fprintf(out, "%s %s %s\n", severityLabel(f->severity), memRuleId(f->rule), f->title);
        if (f->subject[0] != '\0' && strstr(f->title, f->subject) == NULL)
            fprintf(out, "    at: %s\n", f->subject);
        if (f->retention.present) {
            char chain[512];
            describeRetention(&f->retention, chain, sizeof(chain));
            fprintf(out,
                    "    held by: %s (%u of %u sampled object%s)\n",
                    chain,
                    f->retention.covered,
                    f->retention.sampled,
                    f->retention.sampled == 1 ? "" : "s");
            if (f->retention.note[0] != '\0')
                fprintf(out, "    %s\n", f->retention.note);
        }
        if (f->fix[0] != '\0')
            fprintf(out, "    fix: %s\n", f->fix);
        if (f->see[0] != '\0')
            fprintf(out, "    see: %s\n", f->see);
        fprintf(out, "\n");
    }

    char quiet[512] = "";
    size_t len = 0;
    for (int rule = 0; rule < memRuleCount; rule++) {
        const char *phrase = quietPhrase((MemRule)rule);
        if (phrase == NULL || (report->ran & (1u << rule)) == 0 || findingFor(report, (MemRule)rule))
            continue;
        len += (size_t)snprintf(quiet + len, sizeof(quiet) - len, "%s%s", len > 0 ? ", " : "", phrase);
        if (len >= sizeof(quiet))
            break;
    }
    if (len > 0) {
        const char *lead = "ok:";
        if (!report->enoughActivity)
            lead = report->count == 0 ? "nothing found, but too little happened to rely on it:"
                                      : "nothing else found, but too little happened to rely on it:";
        fprintf(out, "%s %s\n", lead, quiet);
    }

    char notChecked[128] = "";
    len = 0;
    for (int rule = 0; rule < memRuleCount; rule++) {
        if (quietPhrase((MemRule)rule) == NULL || (report->ran & (1u << rule)) != 0)
            continue;
        len += (size_t)snprintf(notChecked + len,
                                sizeof(notChecked) - len,
                                "%s%s",
                                len > 0 ? ", " : "",
                                memRuleId((MemRule)rule));
    }
    if (len > 0)
        fprintf(out, "not checked: %s\n", notChecked);

    if (!report->enoughActivity)
        fprintf(out,
                "note: fewer than %llu objects were allocated since the baseline. Run the workload, "
                "then run analyze again.\n",
                (unsigned long long)minActivity);
    if (report->approximate)
        fprintf(out, "note: approximate, a thread did not reach a safepoint in time.\n");
}

/* -------------------------------------------------------------------- JSON */

static void addNumber(cJSON *object, const char *key, double value)
{
    cJSON_AddItemToObject(object, key, cJSON_CreateNumber(value));
}

static void addBool(cJSON *object, const char *key, bool value)
{
    cJSON_AddItemToObject(object, key, cJSON_CreateBool(value));
}

static void addString(cJSON *object, const char *key, const char *value)
{
    cJSON_AddItemToObject(object, key, value != NULL ? cJSON_CreateString(value) : cJSON_CreateNull());
}

static cJSON *countObject(MemCount count)
{
    cJSON *o = cJSON_CreateObject();
    addNumber(o, "count", (double)count.count);
    addNumber(o, "bytes", (double)count.bytes);
    return o;
}

static cJSON *findingObject(const MemFinding *f)
{
    cJSON *o = cJSON_CreateObject();
    addString(o, "id", memRuleId(f->rule));
    addString(o, "severity", memSeverityName(f->severity));
    addString(o, "subject", f->subject);
    addString(o, "title", f->title);
    addString(o, "fix", f->fix[0] != '\0' ? f->fix : NULL);
    cJSON *evidence = cJSON_CreateObject();
    for (u32 i = 0; i < f->evidenceCount; i++)
        addNumber(evidence, f->evidence[i].key, f->evidence[i].value);
    cJSON_AddItemToObject(o, "evidence", evidence);
    cJSON *suggest = cJSON_CreateArray();
    if (f->see[0] != '\0')
        cJSON_AddItemToArray(suggest, cJSON_CreateString(f->see));
    cJSON_AddItemToObject(o, "suggest", suggest);
    addString(o, "type", f->type[0] != '\0' ? f->type : NULL);
    if (f->retention.present) {
        const MemRetention *r = &f->retention;
        cJSON *retention = cJSON_CreateObject();
        addString(retention, "heldBy", heldByToken(r->heldBy));
        addNumber(retention, "sampled", r->sampled);
        addNumber(retention, "covered", r->covered);
        cJSON *shape = cJSON_CreateArray();
        for (u32 i = 0; i < r->stepCount; i++)
            cJSON_AddItemToArray(shape, cJSON_CreateString(r->steps[i]));
        cJSON_AddItemToObject(retention, "shape", shape);
        addString(retention, "note", r->note[0] != '\0' ? r->note : NULL);
        addString(retention, "ptr", r->ptr[0] != '\0' ? r->ptr : NULL);
        cJSON_AddItemToObject(o, "retention", retention);
    }
    else {
        cJSON_AddItemToObject(o, "retention", cJSON_CreateNull());
    }
    return o;
}

void memReportJson(const MemReport *report, const MemSnapshot *last, FILE *out)
{
    cJSON *root = cJSON_CreateObject();
    addNumber(root, "version", 1);

    cJSON *program = cJSON_CreateObject();
    cJSON_AddItemToObject(program, "pid", last->pid != 0 ? cJSON_CreateNumber((double)last->pid) : cJSON_CreateNull());
    cJSON_AddItemToObject(root, "program", program);

    cJSON *window = cJSON_CreateObject();
    addNumber(window, "seconds", report->windowSeconds);
    addNumber(window, "samples", report->samples);
    addBool(window, "baseline", true);
    cJSON_AddItemToObject(root, "window", window);

    addBool(root, "approximate", report->approximate);
    addBool(root, "enoughActivity", report->enoughActivity);
    addBool(root, "plateau", report->plateau);
    addString(root, "verdict", memVerdictId(report->verdict));

    cJSON *totals = cJSON_CreateObject();
    cJSON *objects = countObject(last->objects);
    addNumber(objects, "deltaCount", (double)report->deltaObjects);
    cJSON_AddItemToObject(totals, "objects", objects);
    cJSON_AddItemToObject(totals, "buffers", countObject(last->buffers));
    cJSON_AddItemToObject(totals, "zombies", countObject(last->zombies));
    addNumber(totals, "deltaBytes", (double)report->deltaBytes); // objects and buffers
    cJSON_AddItemToObject(root, "totals", totals);

    cJSON *rates = cJSON_CreateObject();
    addNumber(rates, "objectsPerSecond", report->objectsPerSecond);
    addNumber(rates, "bytesPerSecond", report->bytesPerSecond);
    cJSON_AddItemToObject(root, "rates", rates);

    cJSON *findings = cJSON_CreateArray();
    for (u32 i = 0; i < report->count; i++)
        cJSON_AddItemToArray(findings, findingObject(&report->findings[i]));
    cJSON_AddItemToObject(root, "findings", findings);

    // A rule that ran and found nothing is clean, one that did not run is unknown
    cJSON *checked = cJSON_CreateArray();
    cJSON *notChecked = cJSON_CreateArray();
    for (int rule = 0; rule < memRuleCount; rule++) {
        if (report->ran & (1u << rule)) {
            if (!findingFor(report, (MemRule)rule))
                cJSON_AddItemToArray(checked, cJSON_CreateString(memRuleId((MemRule)rule)));
        }
        else {
            cJSON_AddItemToArray(notChecked, cJSON_CreateString(memRuleId((MemRule)rule)));
        }
    }
    cJSON_AddItemToObject(root, "checked", checked);
    cJSON_AddItemToObject(root, "notChecked", notChecked);
    // One sampled object for each finding that was looked into
    cJSON *suspects = cJSON_CreateArray();
    for (u32 i = 0; i < report->count; i++) {
        const MemFinding *f = &report->findings[i];
        if (!f->retention.present)
            continue;
        cJSON *s = cJSON_CreateObject();
        addString(s, "ptr", f->retention.ptr[0] != '\0' ? f->retention.ptr : NULL);
        addString(s, "type", f->type[0] != '\0' ? f->type : NULL);
        cJSON *site = cJSON_CreateObject();
        addString(site, "file", f->file);
        addNumber(site, "line", (double)f->line);
        cJSON_AddItemToObject(s, "site", site);
        cJSON_AddItemToArray(suspects, s);
    }
    cJSON_AddItemToObject(root, "suspects", suspects);

    char *text = cJSON_PrintUnformatted(root);
    if (text != NULL) {
        fputs(text, out);
        fputc('\n', out);
        free(text);
    }
    cJSON_Delete(root);
}
