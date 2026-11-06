#include "utils/mem/snapshot.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The bundled cJSON has no type predicates and its iteration macro needs a head
static int kindOf(const cJSON *item)
{
    return item != NULL ? (item->type & 0xFF) : 0;
}

static bool isObject(const cJSON *item) { return kindOf(item) == cJSON_Object; }
static bool isArray(const cJSON *item) { return kindOf(item) == cJSON_Array; }
static bool isNumber(const cJSON *item) { return kindOf(item) == cJSON_Number; }
static bool isString(const cJSON *item) { return kindOf(item) == cJSON_String; }
static bool isTrue(const cJSON *item) { return kindOf(item) == cJSON_True; }
static bool isFalse(const cJSON *item) { return kindOf(item) == cJSON_False; }

#undef EACH
#define EACH(pos, head) for (pos = (head) != NULL ? (head)->child : NULL; pos != NULL; pos = pos->next)

static const cJSON *member(const cJSON *object, const char *key)
{
    return isObject(object) ? cJSON_GetObjectItem(object, key) : NULL;
}

static u64 numberOf(const cJSON *object, const char *key)
{
    const cJSON *item = member(object, key);
    return isNumber(item) && item->valuedouble > 0 ? (u64)item->valuedouble : 0;
}

static char *stringOf(const cJSON *object, const char *key)
{
    const cJSON *item = member(object, key);
    return isString(item) && item->valuestring != NULL ? strdup(item->valuestring) : NULL;
}

static MemCount countOf(const cJSON *object, const char *key)
{
    const cJSON *item = member(object, key);
    return (MemCount){.count = numberOf(item, "count"), .bytes = numberOf(item, "bytes")};
}

static void parseTypes(const cJSON *array, MemSnapshot *s)
{
    u32 n = isArray(array) ? (u32)cJSON_GetArraySize(array) : 0;
    s->types = calloc(n + 1, sizeof(MemTypeStat));
    const cJSON *item;
    EACH(item, array)
    {
        MemTypeStat *t = &s->types[s->typeCount++];
        t->name = stringOf(item, "name");
        t->allocs = numberOf(item, "allocs");
        t->frees = numberOf(item, "frees");
        t->live = numberOf(item, "live");
        t->bytes = numberOf(item, "bytes");
    }
}

static void parseSites(const cJSON *array, MemSnapshot *s)
{
    u32 n = isArray(array) ? (u32)cJSON_GetArraySize(array) : 0;
    s->sites = calloc(n + 1, sizeof(MemSiteStat));
    const cJSON *item;
    EACH(item, array)
    {
        MemSiteStat *site = &s->sites[s->siteCount++];
        site->file = stringOf(item, "file");
        site->function = stringOf(item, "function");
        site->line = numberOf(item, "line");
        site->column = numberOf(item, "column");
        site->allocs = numberOf(item, "allocs");
        site->bytes = numberOf(item, "bytes");
        site->live = numberOf(item, "live");
        site->liveBytes = numberOf(item, "liveBytes");
        site->fresh = numberOf(item, "aliveSinceMark");
        site->bufferAllocs = numberOf(item, "bufferAllocs");
        site->liveBuffers = numberOf(item, "liveBuffers");
        site->young = numberOf(item, "young");
    }
}

static MemNode parseNode(const cJSON *item)
{
    return (MemNode){.kind = stringOf(item, "kind"),
                     .name = stringOf(item, "name"),
                     .type = stringOf(item, "type"),
                     .file = stringOf(item, "file"),
                     .line = numberOf(item, "line"),
                     .size = numberOf(item, "size"),
                     .refs = numberOf(item, "refs")};
}

static void freeNode(MemNode *n)
{
    free(n->kind);
    free(n->name);
    free(n->type);
    free(n->file);
}

static void parseRoots(const cJSON *array, MemSnapshot *s)
{
    u32 n = isArray(array) ? (u32)cJSON_GetArraySize(array) : 0;
    s->roots = calloc(n + 1, sizeof(MemRoot));
    const cJSON *item;
    EACH(item, array)
    {
        MemRoot *r = &s->roots[s->rootCount++];
        r->node = parseNode(member(item, "node"));
        r->exclusiveBytes = numberOf(item, "exclusiveBytes");
        r->exclusiveObjects = numberOf(item, "exclusiveObjects");
        r->reachBytes = numberOf(item, "reachBytes");
        r->reachObjects = numberOf(item, "reachObjects");
    }
}

static void parseCycles(const cJSON *doc, MemSnapshot *s)
{
    const cJSON *array = member(doc, "cycles");
    u32 n = isArray(array) ? (u32)cJSON_GetArraySize(array) : 0;
    s->cycles = calloc(n + 1, sizeof(MemCycle));
    s->cycleTotal = numberOf(doc, "total");
    s->cycleBytes = numberOf(doc, "bytes");
    const cJSON *item;
    EACH(item, array)
    {
        MemCycle *c = &s->cycles[s->cycleCount++];
        c->nodes = numberOf(item, "nodes");
        c->bytes = numberOf(item, "bytes");
        const cJSON *members = member(item, "members");
        u32 m = isArray(members) ? (u32)cJSON_GetArraySize(members) : 0;
        c->members = calloc(m + 1, sizeof(MemNode));
        const cJSON *node;
        EACH(node, members) c->members[c->memberCount++] = parseNode(node);
    }
}

static void parseGraph(const cJSON *graph, MemSnapshot *s)
{
    // A graph that could not be built is {"error": ...}
    if (!isObject(graph) || member(graph, "error") != NULL)
        return;
    s->hasGraph = true;
    s->graphNodes = numberOf(graph, "nodes");
    s->graphEdges = numberOf(graph, "edges");
    const cJSON *external = member(graph, "external");
    s->externalObjects = numberOf(external, "objects");
    s->externalRefs = numberOf(external, "refs");
    parseRoots(member(member(graph, "retained"), "roots"), s);
    parseCycles(member(graph, "cycles"), s);
}

static void parseDangling(const cJSON *doc, MemSnapshot *s)
{
    if (!isObject(doc) || member(doc, "error") != NULL)
        return;
    s->hasDangling = true;
    s->danglingTotal = numberOf(doc, "total");
    const cJSON *array = member(doc, "dangling");
    u32 n = isArray(array) ? (u32)cJSON_GetArraySize(array) : 0;
    s->dangling = calloc(n + 1, sizeof(MemDangling));
    const cJSON *item;
    EACH(item, array)
    {
        MemDangling *d = &s->dangling[s->danglingCount++];
        d->ownerType = stringOf(item, "ownerType");
        d->file = stringOf(item, "file");
        d->line = numberOf(item, "line");
        // The target's timeline ends with the event that destroyed it
        const cJSON *events = member(item, "events");
        const cJSON *event;
        EACH(event, events)
        {
            const cJSON *op = member(event, "op");
            if (isString(op) && strcmp(op->valuestring, "drop") == 0) {
                free(d->dropFile);
                d->dropFile = stringOf(event, "file");
                d->dropLine = numberOf(event, "line");
            }
        }
    }
}

static bool fail(char *error, size_t errorSize, const char *message)
{
    if (error != NULL && errorSize > 0)
        snprintf(error, errorSize, "%s", message);
    return false;
}

bool memSnapshotParse(const char *json, MemSnapshot *out, char *error, size_t errorSize)
{
    memset(out, 0, sizeof(*out));
    if (json == NULL || json[0] == '\0')
        return fail(error, errorSize, "empty reply");
    cJSON *doc = cJSON_Parse(json);
    if (doc == NULL)
        return fail(error, errorSize, "the reply is not JSON");
    if (!isObject(doc)) {
        cJSON_Delete(doc);
        return fail(error, errorSize, "the reply is not a JSON object");
    }

    const cJSON *message = member(doc, "error");
    if (isString(message)) {
        char text[256];
        snprintf(text, sizeof(text), "the program answered an error: %s", message->valuestring);
        cJSON_Delete(doc);
        return fail(error, errorSize, text);
    }
    const cJSON *exists = member(doc, "exists");
    if (isFalse(exists)) {
        cJSON_Delete(doc);
        return fail(error, errorSize, "there is no baseline, run `mark` first");
    }

    out->pid = numberOf(doc, "pid");
    out->tick = numberOf(doc, "tick");
    out->timeMs = numberOf(doc, "timeMs");
    out->mark = numberOf(doc, "mark");
    out->objects = countOf(doc, "objects");
    out->buffers = countOf(doc, "buffers");
    out->zombies = countOf(doc, "zombies");
    out->objectsAllocated = numberOf(doc, "objectsAllocated");
    out->approximate = (u32)numberOf(doc, "approximate");
    out->rssBytes = numberOf(doc, "rssBytes");
    const cJSON *refs = member(doc, "refs");
    if (isObject(refs)) {
        out->hasRefs = true;
        out->refCount = numberOf(refs, "count");
        out->maxRefs = numberOf(refs, "maxRefs");
        out->totalRefs = numberOf(refs, "totalRefs");
        out->maxRefsType = stringOf(refs, "maxType");
        out->maxRefsFile = stringOf(refs, "maxFile");
        out->maxRefsLine = numberOf(refs, "maxLine");
    }
    out->hasBaseline = isTrue(member(doc, "hasBaseline"));
    parseTypes(member(doc, "types"), out);
    parseSites(member(doc, "sites"), out);
    parseGraph(member(doc, "graph"), out);
    parseDangling(member(doc, "dangling"), out);

    cJSON_Delete(doc);
    return true;
}

void memSnapshotFree(MemSnapshot *s)
{
    free(s->maxRefsType);
    free(s->maxRefsFile);
    for (u32 i = 0; i < s->typeCount; i++)
        free(s->types[i].name);
    free(s->types);
    for (u32 i = 0; i < s->siteCount; i++) {
        free(s->sites[i].file);
        free(s->sites[i].function);
    }
    free(s->sites);
    for (u32 i = 0; i < s->rootCount; i++)
        freeNode(&s->roots[i].node);
    free(s->roots);
    for (u32 i = 0; i < s->cycleCount; i++) {
        for (u32 j = 0; j < s->cycles[i].memberCount; j++)
            freeNode(&s->cycles[i].members[j]);
        free(s->cycles[i].members);
    }
    free(s->cycles);
    for (u32 i = 0; i < s->danglingCount; i++) {
        free(s->dangling[i].ownerType);
        free(s->dangling[i].file);
        free(s->dangling[i].dropFile);
    }
    free(s->dangling);
    memset(s, 0, sizeof(*s));
}
