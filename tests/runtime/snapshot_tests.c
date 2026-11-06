/*
 * Tests for the counters behind `mark`, `baseline` and `snapshot`. The trace library
 * source is included, objects are registered by hand with known sites and sizes, and
 * the numbers and the JSON are checked against what was registered.
 */
#include "stdlib/trace/mem.c"

// An object as the generated program lays it out: the trace record, the 16 byte
// header (strong, weak and flags, type), then the payload
typedef struct {
    TraceRec rec;
    uint32_t strong, flags;
    const TypeRecord *type;
    char payload[16];
} Block;

uint32_t __cxy_mem_header_refs(void *ptr)
{
    return *(uint32_t *)((char *)ptr - OBJ_HEADER_SIZE);
}

static const SiteRecord siteA = {"a.cxy", 10, 5, "fa", 0};
static const SiteRecord siteB = {"b.cxy", 20, 7, "fb", 1};
const uint32_t __cxy_site_count = 2;
const SiteRecord *const __cxy_sites[] = {&siteA, &siteB, NULL};
static const TypeRecord typeNode = {.name = "Node", .size = 16, .align = 8, .index = 0};
const uint32_t __cxy_type_count = 1;
const TypeRecord *const __cxy_types[] = {&typeNode, NULL};
const uint32_t __cxy_root_count = 0;
const RootRecord __cxy_roots[] = {{NULL, NULL, 0, NULL}};

#include "capture.h"

_Static_assert(offsetof(Block, payload) == sizeof(TraceRec) + OBJ_HEADER_SIZE, "layout");

static Block *all[16]; // in the order they were added
static int allCount;

static Block *add(const SiteRecord *site, uint32_t kind, uint64_t size)
{
    Block *b = calloc(1, sizeof(Block));
    all[allCount++] = b;
    b->rec.site = site;
    b->rec.kind = kind;
    b->rec.size = size;
    b->strong = 1;
    b->type = &typeNode;
    __cxy_mem_trace_register(&b->rec);
    if (kind == KIND_OBJECT)
        __cxy_mem_trace_count(0, site->index, size);
    return b;
}

static void drop(Block *b)
{
    __cxy_mem_trace_unregister(&b->rec);
    __cxy_mem_trace_type_free(0);
    free(b);
}

static void testNoBaseline(void)
{
    CHECK(!hasBaseline);
    CAPTURE_JSON(baselineReport(fd, &w));
    EXPECT("{\"exists\":false}");
    CAPTURE(baselineReport(fd, NULL));
    CONTAINS("no baseline");
}

static Block *first;

static void testMark(void)
{
    first = add(&siteA, KIND_OBJECT, 16);
    add(&siteA, KIND_OBJECT, 16);
    add(&siteA, KIND_OBJECT, 16);

    CAPTURE(markCommand(fd, NULL));
    EXPECT("mark 3\n");
    CHECK(hasBaseline);
    CHECK(baseline.tick == 3 && baseline.markGen == 3);

    CAPTURE_JSON(baselineReport(fd, &w));
    CHECK(strncmp(out, "{\"exists\":true,\"tick\":3,\"timeMs\":", 33) == 0);
    CONTAINS(",\"mark\":3,\"objects\":{\"count\":3,\"bytes\":48},\"buffers\":{\"count\":0,\"bytes\":0},"
             "\"zombies\":{\"count\":0,\"bytes\":0},\"objectsAllocated\":3,");
    CONTAINS("\"types\":[{\"name\":\"Node\",\"allocs\":3,\"frees\":0,\"live\":3,\"bytes\":48}]");
    // site B never allocated and has nothing alive: it is left out
    CONTAINS("\"sites\":[{\"file\":\"a.cxy\",\"line\":10,\"column\":5,\"function\":\"fa\","
             "\"allocs\":3,\"bytes\":48,\"live\":3,\"liveBytes\":48,\"aliveSinceMark\":0}]}");
    CAPTURE(baselineReport(fd, NULL));
    CONTAINS("baseline: tick 3, 3 live objects (48 bytes), 3 allocated since start");
}

// After the mark: two more objects, a buffer, and one old object freed
static void testCounters(void)
{
    add(&siteA, KIND_OBJECT, 16);
    add(&siteA, KIND_OBJECT, 16);
    add(&siteB, KIND_BUFFER, 64);
    drop(first);

    CounterSet s;
    CHECK(collectCounters(&s, true));
    CHECK(s.tick == 6 && s.markGen == 3);
    CHECK(s.objects == 4 && s.objectBytes == 64);
    CHECK(s.buffers == 1 && s.bufferBytes == 64);
    CHECK(s.objectsAllocated == 5);
    CHECK(s.types[0].allocs == 5 && s.types[0].frees == 1);
    CHECK(s.sites[0].allocs == 5 && s.sites[0].bytes == 80);
    CHECK(s.sites[0].live == 4 && s.sites[0].liveBytes == 64);
    CHECK(s.sites[0].fresh == 2); // only the two made after the mark
    CHECK(s.sites[1].allocs == 0 && s.sites[1].live == 1 && s.sites[1].fresh == 1);
    freeCounterSet(&s);

    // The baseline did not move
    CHECK(baseline.objects == 3 && baseline.sites[0].live == 3);

    // Unlocked, as in the forked child, gives the same numbers
    CHECK(collectCounters(&s, false));
    CHECK(s.objects == 4 && s.sites[0].allocs == 5 && s.sites[0].fresh == 2);
    freeCounterSet(&s);
}

static void testSnapshot(void)
{
    CAPTURE_JSON(snapshotReport(fd, &w));
    char expectedStart[64];
    snprintf(expectedStart, sizeof(expectedStart), "{\"version\":1,\"pid\":%d,\"tick\":6,", (int)getpid());
    CHECK(strncmp(out, expectedStart, strlen(expectedStart)) == 0);
    CONTAINS("\"objects\":{\"count\":4,\"bytes\":64}");
    CONTAINS("\"buffers\":{\"count\":1,\"bytes\":64}");
    CONTAINS("\"sites\":[{\"file\":\"a.cxy\",\"line\":10,\"column\":5,\"function\":\"fa\","
             "\"allocs\":5,\"bytes\":80,\"live\":4,\"liveBytes\":64,\"aliveSinceMark\":2},"
             "{\"file\":\"b.cxy\",\"line\":20,\"column\":7,\"function\":\"fb\","
             "\"allocs\":0,\"bytes\":0,\"live\":1,\"liveBytes\":64,\"aliveSinceMark\":1}]");
    CONTAINS(",\"hasBaseline\":true,\"approximate\":0,\"graph\":{\"nodes\":5,\"edges\":0,");
    // four objects with one reference each and nothing in the heap owning them
    CONTAINS("\"external\":{\"objects\":4,\"refs\":4}");
    CONTAINS("\"cycles\":{\"cycles\":[],\"total\":0,\"bytes\":0,\"limited\":false,\"approximate\":0}");
    CONTAINS("\"dangling\":{\"dangling\":[],\"total\":0,\"approximate\":0}}");
}

// Live objects: all[1] (tick 2), all[2], all[3], all[4]. The buffer is not an object.
static void testRefs(void)
{
    all[1]->strong = 5;
    all[3]->strong = 2;

    CAPTURE_JSON(refsReportFd(fd, 3, &w));
    CONTAINS("\"refs\":5,\"type\":\"Node\",\"file\":\"a.cxy\",\"line\":10,\"gen\":2}");
    CONTAINS("\"refs\":2,\"type\":\"Node\",\"file\":\"a.cxy\",\"line\":10,\"gen\":4}");
    // ties go to the newest: the third entry is the object made at tick 5
    CONTAINS("\"refs\":1,\"type\":\"Node\",\"file\":\"a.cxy\",\"line\":10,\"gen\":5}");
    MISSING("\"gen\":3}");
    MISSING("\"kind\":\"buffer\"");
    CHECK(strstr(out, "\"gen\":2}") < strstr(out, "\"gen\":4}"));
    CHECK(strstr(out, "\"gen\":4}") < strstr(out, "\"gen\":5}"));
    CONTAINS("],\"count\":4,\"maxRefs\":5,\"totalRefs\":9,\"limited\":true}");

    CAPTURE_JSON(refsReportFd(fd, 0, &w));
    CONTAINS("\"gen\":3}");
    CONTAINS("],\"count\":4,\"maxRefs\":5,\"totalRefs\":9,\"limited\":false}");

    CAPTURE(refsReportFd(fd, 2, NULL));
    CONTAINS("refs=5 @ a.cxy:10 (Node)");
    CONTAINS("refs=2 @ a.cxy:10 (Node)");
    CONTAINS("Total: 4 objects, max refs 5, showing the highest");
}

static void testMarkJson(void)
{
    CAPTURE_JSON(markCommand(fd, &w));
    CHECK(strncmp(out, "{\"mark\":6,\"tick\":6,\"timeMs\":", 27) == 0);
    CONTAINS(",\"baseline\":true}");
    CHECK(baseline.tick == 6 && baseline.objects == 4);
    CHECK(baseline.sites[0].fresh == 0); // nothing is newer than the new mark
}

int main(void)
{
    started = true;
    testNoBaseline();
    testMark();
    testCounters();
    testSnapshot();
    testRefs();
    testMarkJson();

    if (failures == 0)
        printf("snapshot tests: ok\n");
    else
        printf("snapshot tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
