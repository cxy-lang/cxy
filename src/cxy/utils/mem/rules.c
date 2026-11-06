#include "utils/mem/rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ names */

const char *memRuleId(MemRule rule)
{
    static const char *const ids[] = {"G1", "G2", "G3", "C1", "D1", "Z1",
                                      "K1", "X1", "B1", "H1", "M1", "A1"};
    return rule < memRuleCount ? ids[rule] : "?";
}

const char *memSeverityName(MemSeverity severity)
{
    static const char *const names[] = {"info", "watch", "likely", "definite"};
    return names[severity];
}

const char *memVerdictName(MemVerdict verdict)
{
    static const char *const names[] = {"ok", "not enough activity", "watch", "likely", "definite"};
    return names[verdict];
}

const char *memVerdictId(MemVerdict verdict)
{
    static const char *const ids[] = {"ok", "notEnoughActivity", "watch", "likely", "definite"};
    return ids[verdict];
}

bool memReached(const MemReport *report, MemSeverity threshold)
{
    for (u32 i = 0; i < report->count; i++) {
        if (report->findings[i].severity >= threshold)
            return true;
    }
    return false;
}

bool memParseSeverity(const char *text, MemSeverity *out)
{
    for (int s = memInfo; s <= memDefinite; s++) {
        if (text != NULL && strcmp(text, memSeverityName((MemSeverity)s)) == 0) {
            *out = (MemSeverity)s;
            return true;
        }
    }
    return false;
}

MemConfig memDefaultConfig(void)
{
    return (MemConfig){.minCount = 100,
                       .minRatio = 0.9,
                       .minActivity = 100,
                       .plateauFactor = 0.5,
                       .minZombies = 10,
                       .concentration = 0.8,
                       .concentrationBytes = 1u << 20,
                       .maxSiteFindings = 5,
                       .ignoreTypes = NULL,
                       .minExternal = 100,
                       .minRefs = 100,
                       .refsRatio = 20.0,
                       .minRssGrowth = 8u << 20,
                       .rssShare = 0.25,
                       .youngRatio = 0.5,
                       .bufferChurn = 10};
}

/* ---------------------------------------------------------------- helpers */

// The snapshots in time order: the baseline, then the samples
typedef struct {
    const MemSnapshot *items[MEM_MAX_SAMPLES + 1];
    u32 count;
} Series;

static const MemSnapshot *lastOf(const Series *s)
{
    return s->items[s->count - 1];
}

static MemFinding *addFinding(MemReport *report, MemRule rule, MemSeverity severity)
{
    if (report->count == MEM_MAX_FINDINGS)
        return NULL;
    MemFinding *f = &report->findings[report->count++];
    memset(f, 0, sizeof(*f));
    f->rule = rule;
    f->severity = severity;
    return f;
}

static void addEvidence(MemFinding *f, const char *key, double value)
{
    if (f->evidenceCount < sizeof(f->evidence) / sizeof(f->evidence[0]))
        f->evidence[f->evidenceCount++] = (MemEvidence){.key = key, .value = value};
}

static const char *orUnknown(const char *s)
{
    return s != NULL ? s : "<unknown>";
}

static u64 minus(u64 a, u64 b)
{
    return a > b ? a - b : 0;
}

void memFormatBytes(char *out, size_t size, u64 bytes)
{
    if (bytes >= (1u << 20))
        snprintf(out, size, "%.1f MB", (double)bytes / (1 << 20));
    else if (bytes >= 1024)
        snprintf(out, size, "%.1f KB", (double)bytes / 1024);
    else
        snprintf(out, size, "%llu B", (unsigned long long)bytes);
}

static bool sameSite(const MemSiteStat *a, const MemSiteStat *b)
{
    return a->line == b->line && a->column == b->column &&
           strcmp(orUnknown(a->file), orUnknown(b->file)) == 0;
}

static const MemSiteStat *findSite(const MemSnapshot *s, const MemSiteStat *like)
{
    for (u32 i = 0; i < s->siteCount; i++) {
        if (sameSite(&s->sites[i], like))
            return &s->sites[i];
    }
    return NULL;
}

static const MemTypeStat *findType(const MemSnapshot *s, const char *name)
{
    for (u32 i = 0; i < s->typeCount; i++) {
        if (strcmp(orUnknown(s->types[i].name), orUnknown(name)) == 0)
            return &s->types[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ G1, G2 */

// The survivors of `site` grew in each of the last three snapshots
static bool siteGrowing(const Series *series, const MemSiteStat *site)
{
    if (series->count < 3)
        return false;
    u64 fresh[3];
    for (u32 i = 0; i < 3; i++) {
        const MemSiteStat *at = findSite(series->items[series->count - 3 + i], site);
        fresh[i] = at != NULL ? at->fresh : 0;
    }
    return fresh[0] < fresh[1] && fresh[1] < fresh[2];
}

// Live objects grew, and the latest growth rate is a small part of the one before.
// Rates, not counts, when the snapshots are not evenly spaced.
static bool detectPlateau(const Series *series, const MemConfig *config, i64 *previous, i64 *latest)
{
    if (series->count < 3)
        return false;
    const MemSnapshot *a = series->items[series->count - 3];
    const MemSnapshot *b = series->items[series->count - 2];
    const MemSnapshot *c = series->items[series->count - 1];
    i64 dPrev = (i64)b->objects.count - (i64)a->objects.count;
    i64 dLast = (i64)c->objects.count - (i64)b->objects.count;
    *previous = dPrev;
    *latest = dLast;
    if (dPrev <= 0)
        return false;
    i64 tPrev = (i64)b->timeMs - (i64)a->timeMs;
    i64 tLast = (i64)c->timeMs - (i64)b->timeMs;
    if (tPrev > 0 && tLast > 0)
        return (double)dLast * (double)tPrev <= config->plateauFactor * (double)dPrev * (double)tLast;
    return (double)dLast <= config->plateauFactor * (double)dPrev;
}

typedef struct {
    const MemSiteStat *site;
    u64 made, alive;
} Candidate;

static int compareCandidates(const void *lhs, const void *rhs)
{
    const Candidate *l = lhs, *r = rhs;
    if (l->alive != r->alive)
        return l->alive < r->alive ? 1 : -1;
    return l->made < r->made ? 1 : (l->made > r->made ? -1 : 0);
}

static void ruleG1(const MemSnapshot *baseline,
                   const Series *series,
                   const MemConfig *config,
                   bool plateau,
                   MemReport *report)
{
    const MemSnapshot *last = lastOf(series);
    Candidate *candidates = calloc(last->siteCount + 1, sizeof(Candidate));
    if (candidates == NULL)
        return;
    u32 n = 0;
    for (u32 i = 0; i < last->siteCount; i++) {
        const MemSiteStat *site = &last->sites[i];
        const MemSiteStat *before = findSite(baseline, site);
        u64 made = minus(site->allocs, before != NULL ? before->allocs : 0);
        u64 alive = site->fresh;
        if (made < config->minCount || made == 0)
            continue;
        if ((double)alive < config->minRatio * (double)made)
            continue;
        candidates[n++] = (Candidate){.site = site, .made = made, .alive = alive};
    }
    qsort(candidates, n, sizeof(Candidate), compareCandidates);

    u32 reported = 0;
    for (u32 i = 0; i < n && i < config->maxSiteFindings; i++) {
        const Candidate *c = &candidates[i];
        bool growing = siteGrowing(series, c->site);
        // survivors that are nearly all from the last tenth of the window are work in
        // flight, not growth
        bool inFlight = c->site->young > 0 &&
                        (double)c->site->young >= config->youngRatio * (double)c->alive;
        MemFinding *f = addFinding(
            report, memRuleG1, plateau || inFlight ? memInfo : (growing ? memLikely : memWatch));
        if (f == NULL)
            break;
        reported++;
        snprintf(f->file, sizeof(f->file), "%s", orUnknown(c->site->file));
        f->line = c->site->line;
        double ratio = (double)c->alive / (double)c->made;
        snprintf(f->subject, sizeof(f->subject), "%s:%llu", orUnknown(c->site->file),
                 (unsigned long long)c->site->line);
        snprintf(f->title,
                 sizeof(f->title),
                 "Objects made at %s are not freed: %llu of %llu made since the baseline are "
                 "alive%s",
                 f->subject,
                 (unsigned long long)c->alive,
                 (unsigned long long)c->made,
                 plateau ? " (growth is slowing, probably a cache)" : "");
        snprintf(f->fix, sizeof(f->fix), "find what keeps them with `cxy utils mem path <ptr>`");
        snprintf(f->see, sizeof(f->see), "cxy utils mem difflist 5");
        addEvidence(f, "made", (double)c->made);
        addEvidence(f, "alive", (double)c->alive);
        addEvidence(f, "ratio", ratio);
        addEvidence(f, "samplesGrowing", growing ? 3 : 0);
        if (inFlight) {
            snprintf(f->title + strlen(f->title),
                     sizeof(f->title) - strlen(f->title),
                     " (most were made in the last tenth of the window)");
            MemFinding *a = addFinding(report, memRuleA1, memInfo);
            if (a != NULL) {
                snprintf(a->subject, sizeof(a->subject), "%s", f->subject);
                snprintf(a->title,
                         sizeof(a->title),
                         "%llu of the %llu objects made at %s that are alive were made in the last "
                         "tenth of the window: work still in flight, not necessarily growth",
                         (unsigned long long)c->site->young,
                         (unsigned long long)c->alive,
                         a->subject);
                snprintf(a->fix, sizeof(a->fix), "run analyze again once the workload has finished");
                addEvidence(a, "young", (double)c->site->young);
                addEvidence(a, "alive", (double)c->alive);
            }
        }
    }
    free(candidates);

    if (plateau && reported > 0) {
        i64 previous = 0, latest = 0;
        detectPlateau(series, config, &previous, &latest);
        MemFinding *f = addFinding(report, memRuleG2, memInfo);
        if (f != NULL) {
            snprintf(f->subject, sizeof(f->subject), "live objects");
            snprintf(f->title,
                     sizeof(f->title),
                     "Growth is slowing (+%lld, then +%lld objects): probably a cache filling up "
                     "or a warm-up, not a leak",
                     (long long)previous,
                     (long long)latest);
            snprintf(f->fix, sizeof(f->fix), "run analyze again later: a cache stops growing");
            addEvidence(f, "previous", (double)previous);
            addEvidence(f, "latest", (double)latest);
        }
    }
}

/* ---------------------------------------------------------------------- G3 */

static bool ignored(const MemConfig *config, const char *name)
{
    for (const char *const *p = config->ignoreTypes; p != NULL && *p != NULL; p++) {
        if (strcmp(*p, orUnknown(name)) == 0)
            return true;
    }
    return false;
}

static void ruleG3(const MemSnapshot *baseline, const MemSnapshot *last, const MemConfig *config, MemReport *report)
{
    for (u32 i = 0; i < last->typeCount; i++) {
        const MemTypeStat *t = &last->types[i];
        if (ignored(config, t->name))
            continue;
        // With a baseline only what happened since the mark counts
        const MemTypeStat *before = baseline != NULL ? findType(baseline, t->name) : NULL;
        u64 made = minus(t->allocs, before != NULL ? before->allocs : 0);
        u64 freed = minus(t->frees, before != NULL ? before->frees : 0);
        if (baseline == NULL) {
            made = t->allocs;
            freed = t->frees;
        }
        if (made < config->minCount || freed != 0)
            continue;
        MemFinding *f = addFinding(report, memRuleG3, memWatch);
        if (f == NULL)
            return;
        snprintf(f->subject, sizeof(f->subject), "%s", orUnknown(t->name));
        snprintf(f->title,
                 sizeof(f->title),
                 baseline != NULL ? "No %s object was freed since the baseline: %llu made"
                                  : "No %s object was ever freed: %llu made",
                 f->subject,
                 (unsigned long long)made);
        snprintf(f->fix, sizeof(f->fix), "check who owns them with `cxy utils mem sites`");
        snprintf(f->see, sizeof(f->see), "cxy utils mem types");
        addEvidence(f, "allocs", (double)made);
        addEvidence(f, "frees", (double)freed);
    }
}

/* ------------------------------------------------------------ C1, D1, Z1 */

static void ruleC1(const MemSnapshot *baseline, const MemSnapshot *last, MemReport *report)
{
    for (u32 i = 0; i < last->cycleCount; i++) {
        const MemCycle *c = &last->cycles[i];
        if (baseline != NULL && c->freshNodes == 0)
            continue; // already there at the mark
        MemFinding *f = addFinding(report, memRuleC1, memDefinite);
        if (f == NULL)
            return;

        // The class, when every listed member is of the same one
        const char *type = c->memberCount > 0 ? c->members[0].type : NULL;
        for (u32 j = 1; j < c->memberCount && type != NULL; j++) {
            if (c->members[j].type == NULL || strcmp(c->members[j].type, type) != 0)
                type = NULL;
        }
        size_t len = 0;
        f->subject[0] = '\0';
        for (u32 j = 0; j < c->memberCount && j < 6; j++) {
            len += (size_t)snprintf(f->subject + len,
                                    sizeof(f->subject) - len,
                                    "%s%s:%llu",
                                    j > 0 ? " " : "",
                                    orUnknown(c->members[j].file),
                                    (unsigned long long)c->members[j].line);
            if (len >= sizeof(f->subject))
                break;
        }
        snprintf(f->title,
                 sizeof(f->title),
                 "Leaked cycle: %llu %s%sobjects, %llu bytes",
                 (unsigned long long)c->nodes,
                 type != NULL ? type : "",
                 type != NULL ? " " : "",
                 (unsigned long long)c->bytes);
        snprintf(f->fix,
                 sizeof(f->fix),
                 "make one side a weak reference (@weak %s)",
                 type != NULL ? type : "T");
        snprintf(f->see, sizeof(f->see), "cxy utils mem cycles");
        addEvidence(f, "nodes", (double)c->nodes);
        addEvidence(f, "bytes", (double)c->bytes);
        if (baseline != NULL)
            addEvidence(f, "freshNodes", (double)c->freshNodes);
    }
}

static void ruleD1(const MemSnapshot *last, MemReport *report)
{
    for (u32 i = 0; i < last->danglingCount; i++) {
        const MemDangling *d = &last->dangling[i];
        MemFinding *f = addFinding(report, memRuleD1, memDefinite);
        if (f == NULL)
            return;
        snprintf(f->subject, sizeof(f->subject), "%s:%llu", orUnknown(d->file), (unsigned long long)d->line);
        snprintf(f->title,
                 sizeof(f->title),
                 "Dangling borrow: %s at %s borrows an object destroyed at %s:%llu",
                 orUnknown(d->ownerType),
                 f->subject,
                 orUnknown(d->dropFile),
                 (unsigned long long)d->dropLine);
        snprintf(f->fix,
                 sizeof(f->fix),
                 "keep the owner alive as long as the borrow, or store an owning reference");
        snprintf(f->see, sizeof(f->see), "cxy utils mem dangling");
    }
}

static void ruleZ1(const Series *series, const MemConfig *config, MemReport *report)
{
    const MemSnapshot *first = series->items[0];
    const MemSnapshot *last = lastOf(series);
    u64 added = minus(last->zombies.count, first->zombies.count);
    if (added < config->minZombies)
        return;

    bool growing = false;
    if (series->count >= 3) {
        u64 z[3];
        for (u32 i = 0; i < 3; i++)
            z[i] = series->items[series->count - 3 + i]->zombies.count;
        growing = z[0] < z[1] && z[1] < z[2];
    }
    MemFinding *f = addFinding(report, memRuleZ1, growing ? memLikely : memWatch);
    if (f == NULL)
        return;
    snprintf(f->subject, sizeof(f->subject), "zombies");
    snprintf(f->title,
             sizeof(f->title),
             "Weak references keep destroyed objects alive: %llu now, %llu more than at the baseline",
             (unsigned long long)last->zombies.count,
             (unsigned long long)added);
    snprintf(f->fix, sizeof(f->fix), "release weak references that are no longer needed");
    snprintf(f->see, sizeof(f->see), "cxy utils mem zombies");
    addEvidence(f, "zombies", (double)last->zombies.count);
    addEvidence(f, "added", (double)added);
}

/* ---------------------------------------------------------------------- K1 */

// With a baseline the question is who keeps what was made since the mark
static void ruleK1(const MemSnapshot *baseline, const MemSnapshot *last, const MemConfig *config, MemReport *report)
{
    bool fresh = baseline != NULL;
    u64 total = fresh ? last->freshBytes : last->objects.bytes + last->buffers.bytes;
    const MemRoot *top = NULL;
    u64 topBytes = 0;
    for (u32 i = 0; i < last->rootCount; i++) {
        u64 bytes = fresh ? last->roots[i].exclusiveFreshBytes : last->roots[i].exclusiveBytes;
        if (top == NULL || bytes > topBytes) {
            top = &last->roots[i];
            topBytes = bytes;
        }
    }
    if (top == NULL || total == 0 || topBytes < config->concentrationBytes ||
        (double)topBytes < config->concentration * (double)total)
        return;

    MemFinding *f = addFinding(report, memRuleK1, memInfo);
    if (f == NULL)
        return;
    bool global = top->node.kind != NULL && strcmp(top->node.kind, "global") == 0;
    char what[160], size[32];
    if (global)
        snprintf(what, sizeof(what), "global %s", orUnknown(top->node.name));
    else
        snprintf(what,
                 sizeof(what),
                 "an object made at %s:%llu, held from outside the heap,",
                 orUnknown(top->node.file),
                 (unsigned long long)top->node.line);
    memFormatBytes(size, sizeof(size), topBytes);
    snprintf(f->subject, sizeof(f->subject), "%s", global ? orUnknown(top->node.name) : what);
    snprintf(f->title,
             sizeof(f->title),
             fresh ? "%s keeps %llu%% of what was made since the baseline (%s)"
                   : "%s keeps %llu%% of the live bytes (%s)",
             what,
             (unsigned long long)(topBytes * 100 / total),
             size);
    snprintf(f->fix, sizeof(f->fix), "if this is a cache, bound its size");
    snprintf(f->see, sizeof(f->see), "cxy utils mem retained");
    addEvidence(f, fresh ? "exclusiveFreshBytes" : "exclusiveBytes", (double)topBytes);
    addEvidence(f, fresh ? "freshBytes" : "liveBytes", (double)total);
}

/* --------------------------------------------------------- X1, B1, H1, M1 */

// Objects with references that nothing in the heap accounts for: a stack, a thread,
// a coroutine that never finished
// With a baseline only the objects made since the mark count
static void ruleX1(const MemSnapshot *baseline, const Series *series, const MemConfig *config, MemReport *report)
{
    const MemSnapshot *last = lastOf(series);
    bool fresh = baseline != NULL;
    u64 objects = fresh ? last->externalFreshObjects : last->externalObjects;
    u64 refs = fresh ? last->externalFreshRefs : last->externalRefs;
    if (objects < config->minExternal)
        return;

    bool growing = false;
    if (series->count >= 3) {
        u64 held[3];
        bool all = true;
        for (u32 i = 0; i < 3; i++) {
            const MemSnapshot *s = series->items[series->count - 3 + i];
            all = all && s->hasGraph;
            held[i] = fresh ? s->externalFreshObjects : s->externalObjects;
        }
        growing = all && held[0] < held[1] && held[1] < held[2];
    }
    MemFinding *f = addFinding(report, memRuleX1, growing ? memLikely : memWatch);
    if (f == NULL)
        return;
    snprintf(f->subject, sizeof(f->subject), "objects held from outside the heap");
    snprintf(f->title,
             sizeof(f->title),
             fresh ? "%llu objects made since the baseline (%llu references) are kept alive from "
                     "outside the heap: a stack, a thread or a coroutine"
                   : "%llu objects (%llu references) are kept alive from outside the heap: a "
                     "stack, a thread or a coroutine",
             (unsigned long long)objects,
             (unsigned long long)refs);
    snprintf(f->fix,
             sizeof(f->fix),
             "look for a long-lived function, a thread or a coroutine that never finished");
    snprintf(f->see, sizeof(f->see), "cxy utils mem holders <ptr>");
    addEvidence(f, "objects", (double)objects);
    addEvidence(f, "refs", (double)refs);
}

// Many allocations at a buffer site and few buffers alive: a container that grows by
// reallocation, or a new one for each piece of work
static void ruleB1(const MemSnapshot *baseline, const MemSnapshot *last, const MemConfig *config, MemReport *report)
{
    Candidate *candidates = calloc(last->siteCount + 1, sizeof(Candidate));
    if (candidates == NULL)
        return;
    u32 n = 0;
    for (u32 i = 0; i < last->siteCount; i++) {
        const MemSiteStat *site = &last->sites[i];
        const MemSiteStat *before = findSite(baseline, site);
        u64 made = minus(site->bufferAllocs, before != NULL ? before->bufferAllocs : 0);
        if (made < config->minCount || made == 0)
            continue;
        if (site->freshBuffers * config->bufferChurn > made)
            continue; // most of what was made is still alive: that is growth, not churn
        candidates[n++] = (Candidate){.site = site, .made = made, .alive = site->freshBuffers};
    }
    // the biggest churn first
    for (u32 i = 1; i < n; i++) {
        Candidate c = candidates[i];
        u32 j = i;
        while (j > 0 && candidates[j - 1].made < c.made) {
            candidates[j] = candidates[j - 1];
            j--;
        }
        candidates[j] = c;
    }
    for (u32 i = 0; i < n && i < config->maxSiteFindings; i++) {
        const Candidate *c = &candidates[i];
        MemFinding *f = addFinding(report, memRuleB1, memInfo);
        if (f == NULL)
            break;
        snprintf(f->subject, sizeof(f->subject), "%s:%llu", orUnknown(c->site->file), (unsigned long long)c->site->line);
        snprintf(f->title,
                 sizeof(f->title),
                 "Buffers made at %s were allocated %llu times since the baseline, %llu of "
                 "those are still alive: a container growing by reallocation?",
                 f->subject,
                 (unsigned long long)c->made,
                 (unsigned long long)c->alive);
        snprintf(f->fix, sizeof(f->fix), "reserve the capacity up front, or reuse the container");
        snprintf(f->see, sizeof(f->see), "cxy utils mem sitecounts");
        addEvidence(f, "allocs", (double)c->made);
        addEvidence(f, "alive", (double)c->alive);
    }
    free(candidates);
}

// An object with far more references than the others: a hot shared object, or a
// retain that is never released
// With a baseline it fires only when the highest count went up since the mark
static void ruleH1(const MemSnapshot *baseline, const Series *series, const MemConfig *config, MemReport *report)
{
    const MemSnapshot *last = lastOf(series);
    if (last->refCount == 0)
        return;
    double mean = (double)last->totalRefs / (double)last->refCount;
    if (last->maxRefs < config->minRefs || (double)last->maxRefs < config->refsRatio * mean)
        return;
    bool compare = baseline != NULL && baseline->hasRefs;
    if (compare && last->maxRefs <= baseline->maxRefs)
        return; // as high at the mark: a long-lived shared object, not a change

    bool growing = false;
    if (series->count >= 3) {
        u64 refs[3];
        bool all = true;
        for (u32 i = 0; i < 3; i++) {
            const MemSnapshot *s = series->items[series->count - 3 + i];
            all = all && s->hasRefs;
            refs[i] = s->maxRefs;
        }
        growing = all && refs[0] < refs[1] && refs[1] < refs[2];
    }
    MemFinding *f = addFinding(report, memRuleH1, growing ? memLikely : memWatch);
    if (f == NULL)
        return;
    snprintf(f->subject,
             sizeof(f->subject),
             "%s:%llu",
             orUnknown(last->maxRefsFile),
             (unsigned long long)last->maxRefsLine);
    char who[96];
    if (last->maxRefsType != NULL)
        snprintf(who, sizeof(who), "A `%s` object", last->maxRefsType);
    else
        snprintf(who, sizeof(who), "An object");
    char before[64] = "";
    if (compare)
        snprintf(before, sizeof(before), ", it was %llu at the baseline", (unsigned long long)baseline->maxRefs);
    snprintf(f->title,
             sizeof(f->title),
             "%s made at %s has %llu references%s, the average is %.1f: a hot shared object, or "
             "a retain that is never released",
             who,
             f->subject,
             (unsigned long long)last->maxRefs,
             before,
             mean);
    snprintf(f->fix,
             sizeof(f->fix),
             "pass `&T` where the callee only reads, and check who retains it");
    snprintf(f->see, sizeof(f->see), "cxy utils mem refs 5");
    addEvidence(f, "maxRefs", (double)last->maxRefs);
    addEvidence(f, "meanRefs", mean);
    if (compare)
        addEvidence(f, "baselineMaxRefs", (double)baseline->maxRefs);
}

// The process grew much more than the traced heap since the baseline: memory the
// trace cannot see
static void ruleM1(const MemSnapshot *first, const MemSnapshot *last, const MemConfig *config, MemReport *report)
{
    if (last->rssBytes <= first->rssBytes)
        return;
    u64 rssGrowth = last->rssBytes - first->rssBytes;
    i64 tracedDelta = (i64)(last->objects.bytes + last->buffers.bytes) -
                      (i64)(first->objects.bytes + first->buffers.bytes);
    u64 traced = tracedDelta > 0 ? (u64)tracedDelta : 0;
    if (rssGrowth < config->minRssGrowth || (double)traced >= config->rssShare * (double)rssGrowth)
        return;

    MemFinding *f = addFinding(report, memRuleM1, memInfo);
    if (f == NULL)
        return;
    char rss[32], heap[32];
    memFormatBytes(rss, sizeof(rss), rssGrowth);
    memFormatBytes(heap, sizeof(heap), traced);
    snprintf(f->subject, sizeof(f->subject), "process memory");
    snprintf(f->title,
             sizeof(f->title),
             "The process grew by %s since the baseline but the traced heap by %s: the rest is "
             "outside it (C libraries, raw allocations, fragmentation, thread stacks)",
             rss,
             heap);
    snprintf(f->fix, sizeof(f->fix), "look at what the program allocates outside the traced heap");
    addEvidence(f, "rssGrowth", (double)rssGrowth);
    addEvidence(f, "tracedGrowth", (double)traced);
    addEvidence(f, "rssBytes", (double)last->rssBytes);
}

/* ------------------------------------------------------------------ driver */

static void sortFindings(MemReport *report)
{
    // Insertion sort: stable, so findings of one severity stay in rule order
    for (u32 i = 1; i < report->count; i++) {
        MemFinding f = report->findings[i];
        u32 j = i;
        while (j > 0 && report->findings[j - 1].severity < f.severity) {
            report->findings[j] = report->findings[j - 1];
            j--;
        }
        report->findings[j] = f;
    }
}

void memAnalyze(const MemSnapshot *baseline,
                const MemSnapshot *samples,
                u32 sampleCount,
                const MemConfig *config,
                MemReport *report)
{
    memset(report, 0, sizeof(*report));
    report->verdict = memVerdictNotEnough;
    if (sampleCount == 0)
        return;
    if (sampleCount > MEM_MAX_SAMPLES) {
        samples += sampleCount - MEM_MAX_SAMPLES;
        sampleCount = MEM_MAX_SAMPLES;
    }

    Series series = {.count = 0};
    if (baseline != NULL)
        series.items[series.count++] = baseline;
    for (u32 i = 0; i < sampleCount; i++)
        series.items[series.count++] = &samples[i];
    const MemSnapshot *first = series.items[0];
    const MemSnapshot *last = lastOf(&series);

    report->samples = sampleCount;
    for (u32 i = 0; i < series.count; i++)
        report->approximate |= series.items[i]->approximate > 0;
    if (series.count >= 2) {
        report->enoughActivity =
            minus(last->objectsAllocated, first->objectsAllocated) >= config->minActivity;
        if (last->timeMs > first->timeMs)
            report->windowSeconds = (double)(last->timeMs - first->timeMs) / 1000.0;
    }
    report->deltaObjects = (i64)last->objects.count - (i64)first->objects.count;
    report->deltaBytes = (i64)(last->objects.bytes + last->buffers.bytes) -
                         (i64)(first->objects.bytes + first->buffers.bytes);
    if (report->windowSeconds > 0) {
        report->objectsPerSecond = (double)report->deltaObjects / report->windowSeconds;
        report->bytesPerSecond = (double)report->deltaBytes / report->windowSeconds;
    }

    i64 previous = 0, latest = 0;
    report->plateau = detectPlateau(&series, config, &previous, &latest);

    if (baseline != NULL) {
        report->ran |= 1u << memRuleG1;
        report->ran |= 1u << memRuleA1; // it only ever explains a G1 finding
        ruleG1(baseline, &series, config, report->plateau, report);
    }
    if (series.count >= 3)
        report->ran |= 1u << memRuleG2;
    report->ran |= 1u << memRuleG3;
    ruleG3(baseline, last, config, report);
    if (last->hasGraph) {
        report->ran |= 1u << memRuleC1;
        ruleC1(baseline, last, report);
    }
    if (last->hasDangling) {
        report->ran |= 1u << memRuleD1;
        ruleD1(last, report);
    }
    if (series.count >= 2) {
        report->ran |= 1u << memRuleZ1;
        ruleZ1(&series, config, report);
    }
    if (last->hasGraph) {
        report->ran |= 1u << memRuleK1;
        ruleK1(baseline, last, config, report);
        report->ran |= 1u << memRuleX1;
        ruleX1(baseline, &series, config, report);
    }
    if (baseline != NULL) {
        report->ran |= 1u << memRuleB1;
        ruleB1(baseline, last, config, report);
    }
    if (last->hasRefs) {
        report->ran |= 1u << memRuleH1;
        ruleH1(baseline, &series, config, report);
    }
    if (baseline != NULL && baseline->rssBytes > 0 && last->rssBytes > 0) {
        report->ran |= 1u << memRuleM1;
        ruleM1(baseline, last, config, report);
    }

    memRefreshVerdict(report);
}

void memRefreshVerdict(MemReport *report)
{
    sortFindings(report);

    MemSeverity worst = memInfo;
    bool anything = false;
    for (u32 i = 0; i < report->count; i++) {
        if (report->findings[i].severity >= memWatch) {
            anything = true;
            if (report->findings[i].severity > worst)
                worst = report->findings[i].severity;
        }
    }
    if (anything)
        report->verdict = worst == memDefinite ? memVerdictDefinite
                                               : (worst == memLikely ? memVerdictLikely : memVerdictWatch);
    else
        report->verdict = report->enoughActivity ? memVerdictOk : memVerdictNotEnough;
}
