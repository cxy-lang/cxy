#include "utils/mem/suspects.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int kindOf(const cJSON *item)
{
    return item != NULL ? (item->type & 0xFF) : 0;
}

static const cJSON *member(const cJSON *object, const char *key)
{
    return kindOf(object) == cJSON_Object ? cJSON_GetObjectItem(object, key) : NULL;
}

static const char *stringOf(const cJSON *object, const char *key)
{
    const cJSON *item = member(object, key);
    return kindOf(item) == cJSON_String ? item->valuestring : NULL;
}

// How a node of the chain reads in a shape
static void describeStep(const cJSON *node, char *out, size_t size)
{
    const char *kind = stringOf(node, "kind");
    const char *name = stringOf(node, "name");
    const char *type = stringOf(node, "type");
    const char *file = stringOf(node, "file");
    const cJSON *line = member(node, "line");
    long long lineNumber = kindOf(line) == cJSON_Number ? (long long)line->valuedouble : 0;

    if (kind != NULL && strcmp(kind, "global") == 0)
        snprintf(out, size, "global %s", name != NULL ? name : "?");
    else if (kind != NULL && strcmp(kind, "buffer") == 0)
        snprintf(out, size, "buffer %s:%lld", file != NULL ? file : "<unknown>", lineNumber);
    else if (type != NULL)
        snprintf(out, size, "%s", type);
    else
        snprintf(out, size, "object %s:%lld", file != NULL ? file : "<unknown>", lineNumber);
}

bool memShapeFromPath(const char *json, MemShape *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *doc = json != NULL ? cJSON_Parse(json) : NULL;
    if (doc == NULL)
        return false;
    const cJSON *found = member(doc, "found");
    const cJSON *path = member(doc, "path");
    if (kindOf(found) == 0 || kindOf(path) != cJSON_Array) {
        cJSON_Delete(doc);
        return false; // an error document, or not a path at all
    }

    if (kindOf(found) != cJSON_True) {
        out->heldBy = memHeldCycle;
        cJSON_Delete(doc);
        return true;
    }

    // Everything but the last node, which is the object that was asked about
    int total = cJSON_GetArraySize(path);
    int owners = total > 0 ? total - 1 : 0;
    const cJSON *first = total > 0 ? cJSON_GetArrayItem(path, 0) : NULL;
    const char *firstKind = stringOf(first, "kind");
    bool rooted = owners > 0 && firstKind != NULL && strcmp(firstKind, "global") == 0;
    out->heldBy = rooted ? memHeldGlobal : memHeldOutside;

    for (int i = 0; i < owners; i++) {
        // A long chain keeps its first four and last three steps
        if (owners > MEM_SHAPE_MAX && i >= 4 && i < owners - 3) {
            if (i == 4)
                snprintf(out->steps[out->stepCount++], sizeof(out->steps[0]), "...");
            continue;
        }
        describeStep(cJSON_GetArrayItem(path, i), out->steps[out->stepCount], sizeof(out->steps[0]));
        out->stepCount++;
    }
    cJSON_Delete(doc);
    return true;
}

bool memShapeEqual(const MemShape *a, const MemShape *b)
{
    if (a->heldBy != b->heldBy || a->stepCount != b->stepCount)
        return false;
    for (u32 i = 0; i < a->stepCount; i++) {
        if (strcmp(a->steps[i], b->steps[i]) != 0)
            return false;
    }
    return true;
}

u32 memGroupShapes(const MemShape *shapes, u32 count, u32 *covered)
{
    u32 best = 0, bestCount = 0;
    for (u32 i = 0; i < count; i++) {
        u32 same = 0;
        for (u32 j = 0; j < count; j++)
            same += memShapeEqual(&shapes[i], &shapes[j]);
        if (same > bestCount) {
            best = i;
            bestCount = same;
        }
    }
    *covered = bestCount;
    return best;
}

u32 memPickSamples(u32 available, u32 wanted, u32 *indexes)
{
    u32 n = wanted < available ? wanted : available;
    for (u32 i = 0; i < n; i++)
        indexes[i] = (u32)((u64)i * available / n);
    return n;
}


void memApplySuspect(MemFinding *f,
                     const char *type,
                     const char *ptr,
                     const MemShape *shape,
                     u32 sampled,
                     u32 covered)
{
    if (type != NULL && type[0] != '\0') {
        snprintf(f->type, sizeof(f->type), "%s", type);
        // "Objects made at ..." becomes "`Node` objects made at ..."
        static const char lead[] = "Objects ";
        if (strncmp(f->title, lead, sizeof(lead) - 1) == 0) {
            char rest[sizeof(f->title)];
            snprintf(rest, sizeof(rest), "%s", f->title + sizeof(lead) - 1);
            snprintf(f->title, sizeof(f->title), "`%s` objects %s", f->type, rest);
        }
    }

    MemRetention *r = &f->retention;
    memset(r, 0, sizeof(*r));
    r->present = true;
    r->heldBy = shape->heldBy;
    r->sampled = sampled;
    r->covered = covered;
    r->stepCount = shape->stepCount;
    memcpy(r->steps, shape->steps, sizeof(r->steps));
    if (ptr != NULL)
        snprintf(r->ptr, sizeof(r->ptr), "%s", ptr);

    switch (r->heldBy) {
        case memHeldGlobal:
            snprintf(r->note,
                     sizeof(r->note),
                     "this is a cache that only grows. Intended? If not, evict entries or bound it.");
            snprintf(f->fix, sizeof(f->fix), "evict entries, or bound the size of what the global holds");
            break;
        case memHeldOutside:
            snprintf(r->note,
                     sizeof(r->note),
                     "held from outside the heap: a local variable, a thread or a coroutine keeps them.");
            snprintf(f->fix, sizeof(f->fix), "release them when the local, thread or coroutine is done");
            break;
        case memHeldCycle:
            snprintf(r->note,
                     sizeof(r->note),
                     "only reachable through cycles, nothing can free them: a leak.");
            snprintf(f->fix, sizeof(f->fix), "make one side of the cycle a weak reference");
            // Nothing owns them, so it is not a cache that may be intended
            if (f->severity == memWatch)
                f->severity = memLikely;
            break;
        default:
            break;
    }
    if (f->see[0] != '\0' && r->ptr[0] != '\0')
        snprintf(f->see, sizeof(f->see), "cxy utils mem path %s", r->ptr);
}
