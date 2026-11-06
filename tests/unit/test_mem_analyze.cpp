/**
 * Unit Tests: the rules of `cxy utils mem analyze` (src/cxy/utils/mem).
 *
 * Snapshots are built as JSON in the format the trace server answers, then parsed,
 * so the tests cover the parser and the rules with the same input a real run gives.
 */

#include "doctest.h"

#include "cJSON.h"
#include "utils/mem/report.h"
#include "utils/mem/rules.h"
#include "utils/mem/suspects.h"

#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SiteSpec {
    std::string file = "app.cxy";
    u64 line = 10;
    u64 allocs = 0, live = 0, fresh = 0;
    u64 bufferAllocs = 0, liveBuffers = 0, young = 0;
    u64 bytes() const { return allocs * 16; }
    u64 liveBytes() const { return live * 16; }
};

struct TypeSpec {
    std::string name = "Node";
    u64 allocs = 0, frees = 0;
};

struct RootSpec {
    std::string name = "cache";
    u64 exclusiveBytes = 0;
};

struct CycleSpec {
    u64 nodes = 2, bytes = 32;
    std::string type = "Node";
};

struct DanglingSpec {
    std::string ownerType = "View";
    u64 line = 25, dropLine = 27;
};

struct Snap {
    u64 tick = 0, timeMs = 0, mark = 0;
    u64 objectCount = 0, objectBytes = 0, bufferBytes = 0, zombies = 0;
    u64 objectsAllocated = 0;
    u32 approximate = 0;
    u64 rssBytes = 0;
    u64 refCount = 0, maxRefs = 0, totalRefs = 0; // refs are left out when refCount is 0
    u64 externalObjects = 1, externalRefs = 2;
    bool graph = false;
    std::vector<SiteSpec> sites;
    std::vector<TypeSpec> types;
    std::vector<RootSpec> roots;
    std::vector<CycleSpec> cycles;
    std::vector<DanglingSpec> dangling;

    static std::string node(const std::string &type, u64 line)
    {
        std::ostringstream o;
        o << "{\"kind\":\"object\",\"ptr\":\"0x1000\",\"size\":16,\"name\":null,\"refs\":1,"
          << "\"type\":\"" << type << "\",\"file\":\"app.cxy\",\"line\":" << line << "}";
        return o.str();
    }

    std::string json(bool baseline = false) const
    {
        std::ostringstream o;
        o << "{";
        if (baseline)
            o << "\"exists\":true,";
        else
            o << "\"version\":1,\"pid\":4242,";
        o << "\"tick\":" << tick << ",\"timeMs\":" << timeMs << ",\"mark\":" << mark
          << ",\"objects\":{\"count\":" << objectCount << ",\"bytes\":" << objectBytes << "}"
          << ",\"buffers\":{\"count\":0,\"bytes\":" << bufferBytes << "}"
          << ",\"zombies\":{\"count\":" << zombies << ",\"bytes\":" << zombies * 16 << "}"
          << ",\"objectsAllocated\":" << objectsAllocated << ",\"rssBytes\":" << rssBytes;
        if (refCount > 0)
            o << ",\"refs\":{\"count\":" << refCount << ",\"maxRefs\":" << maxRefs
              << ",\"totalRefs\":" << totalRefs
              << ",\"maxType\":\"Node\",\"maxFile\":\"app.cxy\",\"maxLine\":10}";
        o << ",\"types\":[";
        for (size_t i = 0; i < types.size(); i++) {
            const TypeSpec &t = types[i];
            o << (i ? "," : "") << "{\"name\":\"" << t.name << "\",\"allocs\":" << t.allocs
              << ",\"frees\":" << t.frees << ",\"live\":" << t.allocs - t.frees
              << ",\"bytes\":" << t.allocs * 16 << "}";
        }
        o << "],\"sites\":[";
        for (size_t i = 0; i < sites.size(); i++) {
            const SiteSpec &s = sites[i];
            o << (i ? "," : "") << "{\"file\":\"" << s.file << "\",\"line\":" << s.line
              << ",\"column\":1,\"function\":\"main\",\"allocs\":" << s.allocs
              << ",\"bytes\":" << s.bytes() << ",\"live\":" << s.live
              << ",\"liveBytes\":" << s.liveBytes() << ",\"aliveSinceMark\":" << s.fresh
              << ",\"bufferAllocs\":" << s.bufferAllocs << ",\"liveBuffers\":" << s.liveBuffers
              << ",\"young\":" << s.young << "}";
        }
        o << "]";
        if (!baseline) {
            o << ",\"hasBaseline\":true,\"approximate\":" << approximate;
            if (graph)
                o << ",\"graph\":" << graphJson();
            else
                o << ",\"graph\":{\"error\":\"out of memory while building the graph\"}";
            o << ",\"dangling\":" << danglingJson();
        }
        o << "}";
        return o.str();
    }

    std::string graphJson() const
    {
        std::ostringstream o;
        o << "{\"nodes\":10,\"edges\":4,\"external\":{\"objects\":" << externalObjects
          << ",\"refs\":" << externalRefs << "},"
          << "\"retained\":{\"roots\":[";
        for (size_t i = 0; i < roots.size(); i++) {
            const RootSpec &r = roots[i];
            o << (i ? "," : "") << "{\"node\":{\"kind\":\"global\",\"ptr\":\"0x500\",\"size\":8,"
              << "\"name\":\"" << r.name << "\",\"refs\":null,\"type\":null,\"file\":null,\"line\":0},"
              << "\"exclusiveBytes\":" << r.exclusiveBytes << ",\"exclusiveObjects\":5,"
              << "\"reachBytes\":" << r.exclusiveBytes << ",\"reachObjects\":5,\"sites\":[]}";
        }
        o << "],\"objects\":5,\"edges\":4,\"rootCount\":2,\"holding\":1,\"limited\":false,"
          << "\"unrooted\":{\"objects\":0,\"bytes\":0,\"sites\":[]},\"approximate\":0},"
          << "\"cycles\":{\"cycles\":[";
        u64 total = 0;
        for (size_t i = 0; i < cycles.size(); i++) {
            const CycleSpec &c = cycles[i];
            total += c.bytes;
            o << (i ? "," : "") << "{\"nodes\":" << c.nodes << ",\"bytes\":" << c.bytes
              << ",\"moreMembers\":0,\"members\":[" << node(c.type, 14 + i * 2) << ","
              << node(c.type, 15 + i * 2) << "]}";
        }
        o << "],\"total\":" << cycles.size() << ",\"bytes\":" << total
          << ",\"limited\":false,\"approximate\":0}}";
        return o.str();
    }

    std::string danglingJson() const
    {
        std::ostringstream o;
        o << "{\"dangling\":[";
        for (size_t i = 0; i < dangling.size(); i++) {
            const DanglingSpec &d = dangling[i];
            o << (i ? "," : "") << "{\"owner\":\"0x10\",\"ownerType\":\"" << d.ownerType
              << "\",\"file\":\"app.cxy\",\"line\":" << d.line << ",\"target\":\"0x20\",\"events\":["
              << "{\"op\":\"alloc\",\"refs\":1,\"file\":\"app.cxy\",\"line\":24},"
              << "{\"op\":\"drop\",\"refs\":1,\"file\":\"app.cxy\",\"line\":" << d.dropLine << "}]}";
        }
        o << "],\"total\":" << dangling.size() << ",\"approximate\":0}";
        return o.str();
    }
};

struct Parsed {
    MemSnapshot s{};
    explicit Parsed(const std::string &json, bool *ok = nullptr)
    {
        char error[256];
        bool parsed = memSnapshotParse(json.c_str(), &s, error, sizeof(error));
        if (ok)
            *ok = parsed;
        else
            REQUIRE_MESSAGE(parsed, error);
    }
    ~Parsed() { memSnapshotFree(&s); }
    Parsed(const Parsed &) = delete;
    Parsed &operator=(const Parsed &) = delete;
};

// The parsed snapshots of a window, released together
struct Owned {
    MemSnapshot baseline{};
    std::vector<MemSnapshot> samples;
    ~Owned()
    {
        memSnapshotFree(&baseline);
        for (auto &s : samples)
            memSnapshotFree(&s);
    }
};

// A window: the baseline and the snapshots taken since, oldest first
struct Window {
    std::vector<Snap> specs; // [0] is the baseline
    MemConfig config = memDefaultConfig();
    MemReport report{};
    std::shared_ptr<Owned> owned;

    const MemReport &run()
    {
        owned = std::make_shared<Owned>();
        char error[256];
        REQUIRE(memSnapshotParse(specs[0].json(true).c_str(), &owned->baseline, error, sizeof(error)));
        for (size_t i = 1; i < specs.size(); i++) {
            MemSnapshot s{};
            REQUIRE_MESSAGE(memSnapshotParse(specs[i].json().c_str(), &s, error, sizeof(error)),
                            error);
            owned->samples.push_back(s);
        }
        memAnalyze(&owned->baseline,
                   owned->samples.data(),
                   (u32)owned->samples.size(),
                   &config,
                   &report);
        return report;
    }
};

const MemFinding *find(const MemReport &r, MemRule rule, const char *subject = nullptr)
{
    for (u32 i = 0; i < r.count; i++) {
        if (r.findings[i].rule != rule)
            continue;
        if (subject == nullptr || std::string(r.findings[i].subject).find(subject) != std::string::npos)
            return &r.findings[i];
    }
    return nullptr;
}

u32 countOf(const MemReport &r, MemRule rule)
{
    u32 n = 0;
    for (u32 i = 0; i < r.count; i++)
        n += r.findings[i].rule == rule;
    return n;
}

double evidence(const MemFinding &f, const char *key)
{
    for (u32 i = 0; i < f.evidenceCount; i++) {
        if (std::string(f.evidence[i].key) == key)
            return f.evidence[i].value;
    }
    return -1;
}

bool ran(const MemReport &r, MemRule rule)
{
    return (r.ran & (1u << rule)) != 0;
}

// A window with steady churn: plenty allocated, almost nothing survives
Window healthy()
{
    Window w;
    Snap base;
    base.timeMs = 0;
    base.objectCount = 100;
    base.objectBytes = 1600;
    base.objectsAllocated = 1000;
    base.rssBytes = 8u << 20;
    base.refCount = 100;
    base.maxRefs = 3;
    base.totalRefs = 150;
    base.sites = {{.line = 10, .allocs = 1000, .live = 50}};
    base.types = {{.allocs = 1000, .frees = 950}};
    Snap now = base;
    now.timeMs = 10000;
    now.objectsAllocated = 2000;
    now.sites = {{.line = 10, .allocs = 2000, .live = 52, .fresh = 2}};
    now.types = {{.allocs = 2000, .frees = 1948}};
    now.graph = true;
    w.specs = {base, now};
    return w;
}

} // namespace

/* ------------------------------------------------------------------- parser */

TEST_CASE("Mem snapshot: parses the fields of a snapshot")
{
    Snap spec;
    spec.tick = 7;
    spec.timeMs = 1234;
    spec.mark = 3;
    spec.objectCount = 5;
    spec.objectBytes = 80;
    spec.bufferBytes = 64;
    spec.zombies = 2;
    spec.objectsAllocated = 9;
    spec.approximate = 1;
    spec.graph = true;
    spec.rssBytes = 123456;
    spec.refCount = 10;
    spec.maxRefs = 7;
    spec.totalRefs = 30;
    spec.sites = {{.file = "a.cxy", .line = 3, .allocs = 4, .live = 3, .fresh = 2,
                   .bufferAllocs = 2, .liveBuffers = 1, .young = 1}};
    spec.types = {{.name = "Node", .allocs = 4, .frees = 1}};
    spec.roots = {{.name = "registry", .exclusiveBytes = 32}};
    spec.cycles = {{.nodes = 2, .bytes = 32, .type = "Node"}};
    spec.dangling = {{}};

    Parsed p(spec.json());
    const MemSnapshot &s = p.s;
    CHECK(s.pid == 4242);
    CHECK(s.tick == 7);
    CHECK(s.timeMs == 1234);
    CHECK(s.mark == 3);
    CHECK(s.objects.count == 5);
    CHECK(s.objects.bytes == 80);
    CHECK(s.buffers.bytes == 64);
    CHECK(s.zombies.count == 2);
    CHECK(s.objectsAllocated == 9);
    CHECK(s.approximate == 1);
    CHECK(s.hasBaseline);

    REQUIRE(s.siteCount == 1);
    CHECK(std::string(s.sites[0].file) == "a.cxy");
    CHECK(s.sites[0].line == 3);
    CHECK(s.sites[0].allocs == 4);
    CHECK(s.sites[0].live == 3);
    CHECK(s.sites[0].fresh == 2);
    CHECK(s.sites[0].bufferAllocs == 2);
    CHECK(s.sites[0].liveBuffers == 1);
    CHECK(s.sites[0].young == 1);
    CHECK(s.rssBytes == 123456);
    CHECK(s.hasRefs);
    CHECK(s.refCount == 10);
    CHECK(s.maxRefs == 7);
    CHECK(s.totalRefs == 30);
    CHECK(std::string(s.maxRefsType) == "Node");
    CHECK(std::string(s.maxRefsFile) == "app.cxy");
    CHECK(s.maxRefsLine == 10);

    REQUIRE(s.typeCount == 1);
    CHECK(std::string(s.types[0].name) == "Node");
    CHECK(s.types[0].allocs == 4);
    CHECK(s.types[0].frees == 1);
    CHECK(s.types[0].live == 3);

    CHECK(s.hasGraph);
    CHECK(s.graphNodes == 10);
    CHECK(s.externalObjects == 1);
    CHECK(s.externalRefs == 2);
    REQUIRE(s.rootCount == 1);
    CHECK(std::string(s.roots[0].node.name) == "registry");
    CHECK(s.roots[0].exclusiveBytes == 32);
    REQUIRE(s.cycleCount == 1);
    CHECK(s.cycles[0].nodes == 2);
    REQUIRE(s.cycles[0].memberCount == 2);
    CHECK(std::string(s.cycles[0].members[0].type) == "Node");
    CHECK(s.cycleTotal == 1);
    CHECK(s.cycleBytes == 32);
    REQUIRE(s.danglingCount == 1);
    CHECK(std::string(s.dangling[0].ownerType) == "View");
    CHECK(s.dangling[0].line == 25);
    CHECK(s.dangling[0].dropLine == 27);
    CHECK(s.danglingTotal == 1);
}

TEST_CASE("Mem snapshot: a baseline has no graph")
{
    Snap spec;
    spec.sites = {{.line = 1, .allocs = 1}};
    Parsed p(spec.json(true));
    CHECK_FALSE(p.s.hasGraph);
    CHECK(p.s.rootCount == 0);
    CHECK(p.s.siteCount == 1);
}

TEST_CASE("Mem snapshot: a graph that failed is not a graph")
{
    Snap spec; // graph = false answers {"error": ...} for the graph
    Parsed p(spec.json());
    CHECK_FALSE(p.s.hasGraph);
}

TEST_CASE("Mem snapshot: documents that are not a snapshot are refused")
{
    char error[256];
    MemSnapshot s{};
    CHECK_FALSE(memSnapshotParse("not json", &s, error, sizeof(error)));
    CHECK_FALSE(memSnapshotParse("", &s, error, sizeof(error)));
    CHECK_FALSE(memSnapshotParse("[]", &s, error, sizeof(error)));

    CHECK_FALSE(memSnapshotParse("{\"error\":\"out of memory\"}", &s, error, sizeof(error)));
    CHECK(std::string(error).find("out of memory") != std::string::npos);

    CHECK_FALSE(memSnapshotParse("{\"exists\":false}", &s, error, sizeof(error)));
    CHECK(std::string(error).find("baseline") != std::string::npos);
    memSnapshotFree(&s);
}

TEST_CASE("Mem snapshot: parses the snapshot a real traced program answered")
{
    // The trace test `json_snapshot` stores what the server really answered
    std::ifstream in(std::string(TEST_ROOT_DIR) + "/tests/trace/programs/__snapshots/json_snapshot.txt");
    REQUIRE(in.good());
    std::string line, json;
    bool next = false;
    while (std::getline(in, line)) {
        if (next) {
            json = line;
            break;
        }
        next = line == "$ snapshot json";
    }
    REQUIRE_FALSE(json.empty());
    // The snapshot files hide what changes between runs
    for (size_t at = json.find("TIME"); at != std::string::npos; at = json.find("TIME", at))
        json.replace(at, 4, "0");
    for (size_t at = json.find("PID"); at != std::string::npos; at = json.find("PID", at))
        json.replace(at, 3, "0");
    const std::string rss = "\"rssBytes\":N";
    for (size_t at = json.find(rss); at != std::string::npos; at = json.find(rss, at))
        json.replace(at, rss.size(), "\"rssBytes\":0");

    Parsed p(json);
    CHECK(p.s.objects.count == 5);
    CHECK(p.s.mark == 2);
    CHECK(p.s.siteCount == 5);
    CHECK(p.s.sites[0].fresh == 1);
    CHECK(p.s.hasGraph);
    CHECK(p.s.graphNodes == 6);
    CHECK(p.s.hasRefs);
    CHECK(p.s.refCount == 5);
    CHECK(p.s.maxRefs == 1);
    CHECK(p.s.externalObjects == 1);
    REQUIRE(p.s.rootCount == 1);
    CHECK(std::string(p.s.roots[0].node.name) == "registry");
    CHECK(p.s.roots[0].exclusiveBytes == 32);
    REQUIRE(p.s.cycleCount == 1);
    CHECK(p.s.cycles[0].memberCount == 2);
    CHECK(p.s.danglingTotal == 0);
}

/* -------------------------------------------------------------------- rules */

TEST_CASE("Mem rules: a healthy program has no findings")
{
    Window w = healthy();
    const MemReport &r = w.run();
    CHECK(r.count == 0);
    CHECK(r.verdict == memVerdictOk);
    CHECK(r.enoughActivity);
    CHECK(ran(r, memRuleG1));
    CHECK(ran(r, memRuleG3));
    CHECK(ran(r, memRuleC1));
    CHECK(ran(r, memRuleD1));
    CHECK(ran(r, memRuleZ1));
    CHECK(ran(r, memRuleK1));
    CHECK(ran(r, memRuleX1));
    CHECK(ran(r, memRuleB1));
    CHECK(ran(r, memRuleH1));
    CHECK(ran(r, memRuleM1));
    CHECK(ran(r, memRuleA1));
    CHECK_FALSE(ran(r, memRuleG2)); // needs three snapshots
}

TEST_CASE("Mem rules: window figures and rates")
{
    Window w = healthy();
    w.specs[1].objectCount = 150;
    w.specs[1].objectBytes = 2400;
    w.specs[1].bufferBytes = 100;
    const MemReport &r = w.run();
    CHECK(r.samples == 1);
    CHECK(r.windowSeconds == doctest::Approx(10.0));
    CHECK(r.deltaObjects == 50);
    CHECK(r.deltaBytes == 900);
    CHECK(r.objectsPerSecond == doctest::Approx(5.0));
    CHECK(r.bytesPerSecond == doctest::Approx(90.0));
}

TEST_CASE("Mem rules: not enough activity is not ok")
{
    Window w = healthy();
    w.specs[1].objectsAllocated = w.specs[0].objectsAllocated + 10;
    const MemReport &r = w.run();
    CHECK(r.count == 0);
    CHECK_FALSE(r.enoughActivity);
    CHECK(r.verdict == memVerdictNotEnough);

    w.specs[1].objectsAllocated = w.specs[0].objectsAllocated + w.config.minActivity;
    CHECK(w.run().verdict == memVerdictOk);
}

TEST_CASE("Mem rules: approximate when a thread missed the safepoint")
{
    Window w = healthy();
    CHECK_FALSE(w.run().approximate);
    w.specs[1].approximate = 2;
    CHECK(w.run().approximate);
}

/* G1 */

TEST_CASE("Mem rules G1: survivor ratio boundaries")
{
    Window w = healthy();
    // made = 2000 - 1900, alive = fresh
    w.specs[0].sites[0].allocs = 1900;
    auto setAlive = [&](u64 alive, u64 made) {
        w.specs[1].sites[0].allocs = w.specs[0].sites[0].allocs + made;
        w.specs[1].sites[0].fresh = alive;
    };

    setAlive(90, 100); // exactly the minimum count and the minimum ratio
    CHECK(find(w.run(), memRuleG1) != nullptr);

    setAlive(89, 100); // just below the ratio
    CHECK(find(w.run(), memRuleG1) == nullptr);

    setAlive(99, 99); // just below the count
    CHECK(find(w.run(), memRuleG1) == nullptr);
}

TEST_CASE("Mem rules G1: only what was made since the baseline counts")
{
    Window w = healthy();
    // 5000 allocated before the mark, 100 since and all of those alive
    w.specs[0].sites[0].allocs = 5000;
    w.specs[1].sites[0].allocs = 5100;
    w.specs[1].sites[0].fresh = 100;
    const MemFinding *f = find(w.run(), memRuleG1, "app.cxy:10");
    REQUIRE(f != nullptr);
    CHECK(evidence(*f, "made") == 100);
    CHECK(evidence(*f, "alive") == 100);
    CHECK(evidence(*f, "ratio") == doctest::Approx(1.0));
    CHECK(f->severity == memWatch);
}

TEST_CASE("Mem rules G1: a site the baseline never saw counts from zero")
{
    Window w = healthy();
    w.specs[1].sites.push_back({.line = 99, .allocs = 200, .live = 200, .fresh = 200});
    const MemReport &r = w.run();
    const MemFinding *f = find(r, memRuleG1, "app.cxy:99");
    REQUIRE(f != nullptr);
    CHECK(evidence(*f, "made") == 200);
    CHECK(find(r, memRuleG1, "app.cxy:10") == nullptr);
}

TEST_CASE("Mem rules G1: likely when the last three snapshots all grew")
{
    Window w;
    Snap base;
    base.objectsAllocated = 0;
    base.sites = {{.line = 10, .allocs = 0}};
    auto step = [&](u64 n, u64 t) {
        Snap s = base;
        s.timeMs = t;
        s.objectCount = n;
        s.objectsAllocated = n;
        s.sites = {{.line = 10, .allocs = n, .live = n, .fresh = n}};
        s.graph = true;
        return s;
    };
    w.specs = {base, step(100, 1000), step(200, 2000), step(300, 3000)};
    const MemFinding *f = find(w.run(), memRuleG1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memLikely);
    CHECK(evidence(*f, "samplesGrowing") == 3);
    CHECK(w.report.verdict == memVerdictLikely);

    // The survivors of the site stopped growing between the last two snapshots
    w.specs[3] = step(300, 3000);
    w.specs[3].sites[0] = {.line = 10, .allocs = 210, .live = 300, .fresh = 200};
    f = find(w.run(), memRuleG1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
}

TEST_CASE("Mem rules G1: one snapshot after the baseline can only say watch")
{
    Window w = healthy();
    w.specs[1].sites[0] = {.line = 10, .allocs = 3000, .live = 1100, .fresh = 1000};
    w.specs[0].sites[0].allocs = 2000;
    const MemFinding *f = find(w.run(), memRuleG1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
    CHECK(w.report.verdict == memVerdictWatch);
}

TEST_CASE("Mem rules G1: the worst sites are reported, up to the limit")
{
    Window w = healthy();
    w.specs[1].sites.clear();
    for (u64 i = 0; i < 8; i++)
        w.specs[1].sites.push_back({.line = 100 + i, .allocs = 100 + i * 10, .live = 100 + i * 10,
                                    .fresh = 100 + i * 10});
    w.config.maxSiteFindings = 3;
    const MemReport &r = w.run();
    CHECK(countOf(r, memRuleG1) == 3);
    // the largest first
    CHECK(find(r, memRuleG1, "app.cxy:107") != nullptr);
    CHECK(find(r, memRuleG1, "app.cxy:106") != nullptr);
    CHECK(find(r, memRuleG1, "app.cxy:105") != nullptr);
    CHECK(find(r, memRuleG1, "app.cxy:104") == nullptr);
}

TEST_CASE("Mem rules G1: no baseline, no comparison")
{
    Window w = healthy();
    w.specs[1].sites[0] = {.line = 10, .allocs = 3000, .live = 1100, .fresh = 1000};
    w.run();
    MemReport r;
    memAnalyze(nullptr, w.owned->samples.data(), (u32)w.owned->samples.size(), &w.config, &r);
    CHECK_FALSE(ran(r, memRuleG1));
    CHECK_FALSE(ran(r, memRuleG2));
    CHECK(countOf(r, memRuleG1) == 0);
}

/* G2 */

namespace {

// Live objects grow by `d1`, `d2`, `d3` between snapshots one second apart; every
// object of the site made since the mark is alive
Window growing(u64 d1, u64 d2, u64 d3)
{
    Window w;
    Snap base;
    base.objectCount = 1000;
    base.objectsAllocated = 1000;
    base.sites = {{.line = 10, .allocs = 1000, .live = 1000}};
    auto step = [&](u64 total, u64 t) {
        Snap s = base;
        s.timeMs = t;
        s.objectCount = 1000 + total;
        s.objectsAllocated = 1000 + total;
        s.sites = {{.line = 10, .allocs = 1000 + total, .live = 1000 + total, .fresh = total}};
        s.graph = true;
        return s;
    };
    w.specs = {base, step(d1, 1000), step(d1 + d2, 2000), step(d1 + d2 + d3, 3000)};
    return w;
}

} // namespace

TEST_CASE("Mem rules G2: growth that is slowing is a plateau, G1 drops to info")
{
    Window w = growing(100, 60, 10);
    const MemReport &r = w.run();
    CHECK(r.plateau);
    CHECK(ran(r, memRuleG2));
    const MemFinding *g1 = find(r, memRuleG1);
    REQUIRE(g1 != nullptr);
    CHECK(g1->severity == memInfo);
    const MemFinding *g2 = find(r, memRuleG2);
    REQUIRE(g2 != nullptr);
    CHECK(g2->severity == memInfo);
    CHECK(evidence(*g2, "previous") == 60);
    CHECK(evidence(*g2, "latest") == 10);
    CHECK(r.verdict == memVerdictOk);
}

TEST_CASE("Mem rules G2: growth that keeps its pace is not a plateau")
{
    Window w = growing(100, 60, 70);
    const MemReport &r = w.run();
    CHECK_FALSE(r.plateau);
    CHECK(find(r, memRuleG2) == nullptr);
    const MemFinding *g1 = find(r, memRuleG1);
    REQUIRE(g1 != nullptr);
    CHECK(g1->severity == memLikely);
}

TEST_CASE("Mem rules G2: the boundary is the factor")
{
    Window w = growing(100, 60, 30); // exactly half of the previous growth
    CHECK(w.run().plateau);
    w = growing(100, 60, 31);
    CHECK_FALSE(w.run().plateau);
}

TEST_CASE("Mem rules G2: rates, not counts, when the snapshots are unevenly spaced")
{
    Window w = growing(100, 60, 60);
    w.specs[3].timeMs = 5000; // the same growth over 3 seconds is a third of the rate
    CHECK(w.run().plateau);
}

TEST_CASE("Mem rules G2: nothing to say without growth")
{
    Window w = growing(0, 0, 0);
    const MemReport &r = w.run();
    CHECK_FALSE(r.plateau);
    CHECK(find(r, memRuleG2) == nullptr);
}

/* G3 */

TEST_CASE("Mem rules G3: a type that is never freed")
{
    Window w = healthy();
    w.specs[0].types = {{.name = "Leaky", .allocs = 100, .frees = 0}};
    w.specs[1].types = {{.name = "Leaky", .allocs = 150, .frees = 0}};
    const MemFinding *f = find(w.run(), memRuleG3, "Leaky");
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
    CHECK(evidence(*f, "allocs") == 150);
    CHECK(evidence(*f, "frees") == 0);
}

TEST_CASE("Mem rules G3: boundaries and exceptions")
{
    Window w = healthy();
    w.specs[0].types = {{.name = "T", .allocs = 0, .frees = 0}};

    w.specs[1].types = {{.name = "T", .allocs = 100, .frees = 0}};
    CHECK(find(w.run(), memRuleG3) != nullptr);

    w.specs[1].types = {{.name = "T", .allocs = 99, .frees = 0}}; // too few
    CHECK(find(w.run(), memRuleG3) == nullptr);

    w.specs[1].types = {{.name = "T", .allocs = 500, .frees = 1}}; // one was freed
    CHECK(find(w.run(), memRuleG3) == nullptr);

    // Never freed, but nothing was made in the window: not growth
    w.specs[0].types = {{.name = "T", .allocs = 500, .frees = 0}};
    w.specs[1].types = {{.name = "T", .allocs = 500, .frees = 0}};
    CHECK(find(w.run(), memRuleG3) == nullptr);
}

TEST_CASE("Mem rules G3: runtime types on the ignore list are skipped")
{
    Window w = healthy();
    w.specs[0].types = {{.name = "Scheduler", .allocs = 0, .frees = 0}};
    w.specs[1].types = {{.name = "Scheduler", .allocs = 500, .frees = 0}};
    CHECK(find(w.run(), memRuleG3, "Scheduler") != nullptr);

    static const char *const ignore[] = {"Scheduler", nullptr};
    w.config.ignoreTypes = ignore;
    CHECK(find(w.run(), memRuleG3) == nullptr);
}

/* C1 */

TEST_CASE("Mem rules C1: a leaked cycle is definite")
{
    Window w = healthy();
    w.specs[1].graph = true;
    w.specs[1].cycles = {{.nodes = 2, .bytes = 32, .type = "Node"}};
    const MemReport &r = w.run();
    const MemFinding *f = find(r, memRuleC1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memDefinite);
    CHECK(std::string(f->title).find("2 Node objects") != std::string::npos);
    CHECK(std::string(f->title).find("32 bytes") != std::string::npos);
    CHECK(std::string(f->subject).find("app.cxy:14") != std::string::npos);
    CHECK(std::string(f->subject).find("app.cxy:15") != std::string::npos);
    CHECK(std::string(f->fix).find("weak") != std::string::npos);
    CHECK(r.verdict == memVerdictDefinite);
}

TEST_CASE("Mem rules C1: definite even when nothing else happened in the window")
{
    Window w = healthy();
    w.specs[1].objectsAllocated = w.specs[0].objectsAllocated; // no activity at all
    w.specs[1].cycles = {{}};
    const MemReport &r = w.run();
    CHECK_FALSE(r.enoughActivity);
    CHECK(r.verdict == memVerdictDefinite);
}

TEST_CASE("Mem rules C1: one finding per cycle")
{
    Window w = healthy();
    w.specs[1].cycles = {{.nodes = 2, .bytes = 32}, {.nodes = 3, .bytes = 48, .type = "Peer"}};
    const MemReport &r = w.run();
    CHECK(countOf(r, memRuleC1) == 2);
}

TEST_CASE("Mem rules C1, K1: not run without a graph, D1 does not need one")
{
    Window w = healthy();
    w.specs[1].graph = false;
    const MemReport &r = w.run();
    CHECK_FALSE(ran(r, memRuleC1));
    CHECK_FALSE(ran(r, memRuleK1));
    CHECK_FALSE(ran(r, memRuleX1));
    CHECK(ran(r, memRuleD1));
}

/* D1 */

TEST_CASE("Mem rules D1: a dangling borrow is definite and names both sites")
{
    Window w = healthy();
    w.specs[1].dangling = {{.ownerType = "View", .line = 25, .dropLine = 27}};
    const MemReport &r = w.run();
    const MemFinding *f = find(r, memRuleD1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memDefinite);
    CHECK(std::string(f->title).find("View") != std::string::npos);
    CHECK(std::string(f->title).find("app.cxy:25") != std::string::npos);
    CHECK(std::string(f->title).find("app.cxy:27") != std::string::npos);
    CHECK(r.verdict == memVerdictDefinite);
}

/* Z1 */

TEST_CASE("Mem rules Z1: zombies increasing")
{
    Window w = healthy();
    w.specs[0].zombies = 0;
    w.specs[1].zombies = 10; // the minimum
    const MemFinding *f = find(w.run(), memRuleZ1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
    CHECK(evidence(*f, "zombies") == 10);
    CHECK(evidence(*f, "added") == 10);

    w.specs[1].zombies = 9;
    CHECK(find(w.run(), memRuleZ1) == nullptr);

    // The same zombies that were there at the baseline are not growth
    w.specs[0].zombies = 50;
    w.specs[1].zombies = 50;
    CHECK(find(w.run(), memRuleZ1) == nullptr);
}

TEST_CASE("Mem rules Z1: likely when the last three snapshots all grew")
{
    Window w = healthy();
    Snap s2 = w.specs[1], s3 = w.specs[1];
    w.specs[1].zombies = 15;
    s2.zombies = 30;
    s3.zombies = 60;
    s2.timeMs = 20000;
    s3.timeMs = 30000;
    w.specs.push_back(s2);
    w.specs.push_back(s3);
    const MemFinding *f = find(w.run(), memRuleZ1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memLikely);
}

/* K1 */

TEST_CASE("Mem rules K1: one root holds most of the heap")
{
    Window w = healthy();
    w.specs[1].objectBytes = 2 * 1024 * 1024;
    w.specs[1].roots = {{.name = "cache", .exclusiveBytes = 1900000}}; // 90.6%
    const MemFinding *f = find(w.run(), memRuleK1, "cache");
    REQUIRE(f != nullptr);
    CHECK(f->severity == memInfo);
    CHECK(std::string(f->title).find("cache") != std::string::npos);
    CHECK(evidence(*f, "exclusiveBytes") == 1900000);
}

TEST_CASE("Mem rules K1: boundaries")
{
    Window w = healthy();
    w.specs[1].objectBytes = 2 * 1024 * 1024;

    w.specs[1].roots = {{.name = "cache", .exclusiveBytes = 1000000}}; // 47%
    CHECK(find(w.run(), memRuleK1) == nullptr);

    // A big share of a small heap says nothing
    w.specs[1].objectBytes = 600 * 1024;
    w.specs[1].roots = {{.name = "cache", .exclusiveBytes = 590000}};
    CHECK(find(w.run(), memRuleK1) == nullptr);
}

/* ordering */

TEST_CASE("Mem rules: findings are ordered by severity, definite first")
{
    Window w = growing(100, 60, 70); // G1 likely
    w.specs[3].cycles = {{}};         // C1 definite
    w.specs[3].zombies = 20;          // Z1 watch
    w.specs[3].objectBytes = 2 * 1024 * 1024;
    w.specs[3].roots = {{.name = "cache", .exclusiveBytes = 1900000}}; // K1 info
    const MemReport &r = w.run();
    REQUIRE(r.count >= 4);
    for (u32 i = 1; i < r.count; i++)
        CHECK(r.findings[i - 1].severity >= r.findings[i].severity);
    CHECK(r.findings[0].rule == memRuleC1);
    CHECK(r.verdict == memVerdictDefinite);
}

/* ------------------------------------------------------------------- report */

namespace {

std::string render(Window &w, bool run = true)
{
    if (run)
        w.run();
    FILE *file = tmpfile();
    memReportText(&w.report, &w.owned->samples.back(), w.config.minActivity, file);
    fflush(file);
    long size = ftell(file);
    rewind(file);
    std::string text((size_t)size, '\0');
    size_t got = fread(text.data(), 1, (size_t)size, file);
    text.resize(got);
    fclose(file);
    return text;
}

std::string pad(const std::string &text, size_t width)
{
    return text + std::string(width > text.size() ? width - text.size() : 0, ' ');
}

bool has(const std::string &text, const std::string &part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("Mem report: a healthy program")
{
    Window w = healthy();
    CHECK(render(w) ==
          pad("verdict: OK", 41) + "window: 10s, 1 sample\n"
          "\n"
          "memory       live 1.6 KB in 100 objects (+0 objects since the baseline)\n"
          "             buffers 0 B   zombies 0   process 8.0 MB\n"
          "growth       +0 B (objects and buffers), +0.0 objects/s, +0 B/s\n"
          "\n"
          "ok: no allocation site keeps what it makes, no type that is never freed, no leaked "
          "cycles, no dangling borrows, no growing zombies, no root holding most of the heap, no "
          "large number of objects held from outside the heap, no buffer allocated again and "
          "again, no object with an unusual reference count, process memory matches the traced "
          "heap\n");
}

TEST_CASE("Mem report: a finding has its fix and the command to look further")
{
    Window w = healthy();
    w.specs[1].cycles = {{.nodes = 2, .bytes = 32, .type = "Node"}};
    std::string text = render(w);
    CHECK(has(text, pad("verdict: DEFINITE PROBLEM", 41) + "window: 10s, 1 sample\n"));
    CHECK(has(text,
              "[definite] C1 Leaked cycle: 2 Node objects, 32 bytes\n"
              "    at: app.cxy:14 app.cxy:15\n"
              "    fix: make one side a weak reference (@weak Node)\n"
              "    see: cxy utils mem cycles\n"
              "\n"));
    // what was found is not in the "nothing found" line
    CHECK_FALSE(has(text, "no leaked cycles"));
    CHECK(has(text, "ok: no allocation site keeps what it makes"));
}

TEST_CASE("Mem report: a subject that the title already says is not repeated")
{
    Window w = healthy();
    w.specs[1].dangling = {{.ownerType = "View", .line = 25, .dropLine = 27}};
    std::string text = render(w);
    CHECK(has(text, "[definite] D1 Dangling borrow: View at app.cxy:25 borrows an object destroyed at app.cxy:27\n"
                    "    fix:"));
}

TEST_CASE("Mem report: too little activity is said, not hidden behind ok")
{
    Window w = healthy();
    w.specs[1].objectsAllocated = w.specs[0].objectsAllocated + 5;
    std::string text = render(w);
    CHECK(has(text, "verdict: NOT ENOUGH ACTIVITY"));
    CHECK(has(text, "nothing found, but too little happened to rely on it: no allocation site"));
    CHECK(has(text, "note: fewer than 100 objects were allocated since the baseline."));
    CHECK_FALSE(has(text, "\nok:"));
}

TEST_CASE("Mem report: a finding with too little activity does not claim nothing was found")
{
    Window w = healthy();
    w.specs[1].objectsAllocated = w.specs[0].objectsAllocated + 5;
    w.specs[1].cycles = {{}};
    std::string text = render(w);
    CHECK(has(text, "verdict: DEFINITE PROBLEM"));
    CHECK(has(text, "nothing else found, but too little happened to rely on it: no allocation site"));
    CHECK_FALSE(has(text, "nothing found,"));
}

TEST_CASE("Mem report: one object is not plural")
{
    Window w = healthy();
    w.specs[1].objectCount = 1;
    CHECK(has(render(w), "in 1 object ("));
}

TEST_CASE("Mem report: rules that could not run are listed")
{
    Window w = healthy();
    w.specs[1].graph = false;
    std::string text = render(w);
    CHECK(has(text, "not checked: C1, K1, X1\n"));
    CHECK_FALSE(has(text, "no leaked cycles"));
}

TEST_CASE("Mem report: an approximate answer says so")
{
    Window w = healthy();
    w.specs[1].approximate = 1;
    CHECK(has(render(w), "note: approximate, a thread did not reach a safepoint in time.\n"));
}

TEST_CASE("Mem report: growth and long windows")
{
    Window w = healthy();
    w.specs[1].timeMs = 125000;
    w.specs[1].objectCount = 300;
    w.specs[1].objectBytes = 1600 + 2048;
    std::string text = render(w);
    CHECK(has(text, "window: 2m05s, 1 sample\n"));
    CHECK(has(text, "(+200 objects since the baseline)"));
    CHECK(has(text, "growth       +2.0 KB (objects and buffers), +1.6 objects/s, +16 B/s\n"));
}

/* ---------------------------------------------------------------- JSON report */

namespace {

struct JsonDoc {
    std::string text;
    cJSON *root = nullptr;

    explicit JsonDoc(Window &w, bool run = true)
    {
        if (run)
            w.run();
        FILE *file = tmpfile();
        memReportJson(&w.report, &w.owned->samples.back(), file);
        fflush(file);
        long size = ftell(file);
        rewind(file);
        text.resize((size_t)size);
        text.resize(fread(text.data(), 1, (size_t)size, file));
        fclose(file);
        root = cJSON_Parse(text.c_str());
    }
    ~JsonDoc() { cJSON_Delete(root); }
    JsonDoc(const JsonDoc &) = delete;
    JsonDoc &operator=(const JsonDoc &) = delete;

    // `path` is `a.b.c`, an array index is a number
    cJSON *at(const std::string &path) const
    {
        cJSON *item = root;
        std::stringstream parts(path);
        std::string part;
        while (item != nullptr && std::getline(parts, part, '.')) {
            if (!part.empty() && std::isdigit((unsigned char)part[0]))
                item = cJSON_GetArrayItem(item, std::stoi(part));
            else
                item = cJSON_GetObjectItem(item, part.c_str());
        }
        return item;
    }
    double number(const std::string &path) const
    {
        cJSON *item = at(path);
        REQUIRE_MESSAGE(item != nullptr, path);
        return item->valuedouble;
    }
    std::string string(const std::string &path) const
    {
        cJSON *item = at(path);
        REQUIRE_MESSAGE(item != nullptr, path);
        REQUIRE_MESSAGE(item->valuestring != nullptr, path);
        return item->valuestring;
    }
    int size(const std::string &path) const
    {
        cJSON *item = at(path);
        REQUIRE_MESSAGE(item != nullptr, path);
        return cJSON_GetArraySize(item);
    }
    bool isTrue(const std::string &path) const
    {
        cJSON *item = at(path);
        REQUIRE_MESSAGE(item != nullptr, path);
        return (item->type & 0xFF) == cJSON_True;
    }
};

} // namespace

TEST_CASE("Mem JSON report: a healthy program")
{
    Window w = healthy();
    JsonDoc doc(w);
    REQUIRE(doc.root != nullptr);
    CHECK(doc.number("version") == 1);
    CHECK(doc.number("program.pid") == 4242);
    CHECK(doc.number("window.seconds") == doctest::Approx(10.0));
    CHECK(doc.number("window.samples") == 1);
    CHECK(doc.isTrue("window.baseline"));
    CHECK_FALSE(doc.isTrue("approximate"));
    CHECK(doc.isTrue("enoughActivity"));
    CHECK_FALSE(doc.isTrue("plateau"));
    CHECK(doc.string("verdict") == "ok");
    CHECK(doc.number("totals.objects.count") == 100);
    CHECK(doc.number("totals.objects.bytes") == 1600);
    CHECK(doc.number("totals.objects.deltaCount") == 0);
    CHECK(doc.number("totals.buffers.bytes") == 0);
    CHECK(doc.number("totals.zombies.count") == 0);
    CHECK(doc.number("totals.deltaBytes") == 0);
    CHECK(doc.number("rates.objectsPerSecond") == 0);
    CHECK(doc.size("findings") == 0);
    CHECK(doc.size("suspects") == 0);
    // every rule that ran is listed as clean, G2 needs three snapshots
    CHECK(doc.number("totals.rssBytes") == 8 * 1024 * 1024);
    REQUIRE(doc.size("checked") == 11);
    CHECK(doc.string("checked.0") == "G1");
    CHECK(doc.string("checked.5") == "K1");
    CHECK(doc.string("checked.10") == "A1");
    REQUIRE(doc.size("notChecked") == 1);
    CHECK(doc.string("notChecked.0") == "G2");
}

TEST_CASE("Mem JSON report: a finding keeps its numbers as numbers")
{
    Window w = healthy();
    w.specs[1].cycles = {{.nodes = 2, .bytes = 32, .type = "Node"}};
    w.specs[1].sites = {{.line = 10, .allocs = 2000, .live = 150, .fresh = 140}};
    w.specs[0].sites = {{.line = 10, .allocs = 1850, .live = 0}};
    JsonDoc doc(w);
    REQUIRE(doc.root != nullptr);
    CHECK(doc.string("verdict") == "definite");
    REQUIRE(doc.size("findings") == 2);

    CHECK(doc.string("findings.0.id") == "C1");
    CHECK(doc.string("findings.0.severity") == "definite");
    CHECK(doc.string("findings.0.title") == "Leaked cycle: 2 Node objects, 32 bytes");
    CHECK(doc.string("findings.0.subject") == "app.cxy:14 app.cxy:15");
    CHECK(doc.string("findings.0.fix") == "make one side a weak reference (@weak Node)");
    CHECK(doc.number("findings.0.evidence.nodes") == 2);
    CHECK(doc.number("findings.0.evidence.bytes") == 32);
    REQUIRE(doc.size("findings.0.suggest") == 1);
    CHECK(doc.string("findings.0.suggest.0") == "cxy utils mem cycles");

    CHECK(doc.string("findings.1.id") == "G1");
    CHECK(doc.string("findings.1.severity") == "watch");
    CHECK(doc.number("findings.1.evidence.made") == 150);
    CHECK(doc.number("findings.1.evidence.alive") == 140);
    CHECK(doc.number("findings.1.evidence.ratio") == doctest::Approx(140.0 / 150.0));
}

TEST_CASE("Mem JSON report: one line, and the verdict as a token")
{
    Window w = healthy();
    w.specs[1].objectsAllocated = w.specs[0].objectsAllocated + 1;
    JsonDoc doc(w);
    REQUIRE(doc.root != nullptr);
    REQUIRE_FALSE(doc.text.empty());
    CHECK(doc.text.back() == '\n');
    CHECK(doc.text.find('\n') == doc.text.size() - 1);
    CHECK(doc.string("verdict") == "notEnoughActivity");
    CHECK_FALSE(doc.isTrue("enoughActivity"));
}

TEST_CASE("Mem JSON report: approximate and no pid")
{
    Window w = healthy();
    w.specs[1].approximate = 1;
    JsonDoc doc(w);
    CHECK(doc.isTrue("approximate"));
}

/* ------------------------------------------------------------------ suspects */

namespace {

MemShape cacheShape()
{
    MemShape s{};
    s.heldBy = memHeldGlobal;
    s.stepCount = 3;
    snprintf(s.steps[0], sizeof(s.steps[0]), "global cache");
    snprintf(s.steps[1], sizeof(s.steps[1]), "Vector");
    snprintf(s.steps[2], sizeof(s.steps[2]), "buffer app.cxy:13");
    return s;
}

MemShape shapeHeldBy(MemHeldBy heldBy)
{
    MemShape s{};
    s.heldBy = heldBy;
    return s;
}

MemFinding &g1Of(Window &w)
{
    for (u32 i = 0; i < w.report.count; i++) {
        if (w.report.findings[i].rule == memRuleG1)
            return w.report.findings[i];
    }
    FAIL("no G1 finding");
    return w.report.findings[0];
}

} // namespace

TEST_CASE("Mem suspects: a growing site is shown with who keeps its objects")
{
    Window w = growing(100, 60, 70);
    w.run();
    MemFinding &f = g1Of(w);
    MemShape shape = cacheShape();
    memApplySuspect(&f, "Node", "0x79c6c00bf0", &shape, 3, 3);
    memRefreshVerdict(&w.report);

    CHECK(std::string(f.type) == "Node");
    CHECK(std::string(f.title).rfind("`Node` objects made at app.cxy:10 are not freed", 0) == 0);
    CHECK(std::string(f.see) == "cxy utils mem path 0x79c6c00bf0");
    CHECK(f.retention.present);
    CHECK(f.retention.sampled == 3);
    CHECK(f.retention.covered == 3);
    CHECK(std::string(f.retention.ptr) == "0x79c6c00bf0");

    std::string text = render(w, false);
    CHECK(has(text,
              "[likely]   G1 `Node` objects made at app.cxy:10 are not freed: 230 of 230 made since "
              "the baseline are alive\n"
              "    held by: global cache -> Vector -> buffer app.cxy:13 (3 of 3 sampled objects)\n"
              "    this is a cache that only grows. Intended? If not, evict entries or bound it.\n"
              "    fix: "));
    CHECK(has(text, "    see: cxy utils mem path 0x79c6c00bf0\n"));
}

TEST_CASE("Mem suspects: some of the sampled objects held elsewhere")
{
    Window w = growing(100, 60, 70);
    w.run();
    MemShape shape = cacheShape();
    memApplySuspect(&g1Of(w), "Node", "0x10", &shape, 8, 5);
    CHECK(has(render(w, false), "(5 of 8 sampled objects)\n"));
}

TEST_CASE("Mem suspects: objects only reachable through cycles are a leak, not a cache")
{
    Window w = healthy();
    w.specs[1].sites[0] = {.line = 10, .allocs = 3000, .live = 1100, .fresh = 1000};
    w.specs[0].sites[0].allocs = 2000;
    w.run();
    MemFinding &f = g1Of(w);
    REQUIRE(f.severity == memWatch);
    CHECK(w.report.verdict == memVerdictWatch);

    MemShape cycle = shapeHeldBy(memHeldCycle);
    memApplySuspect(&f, "Node", "0x10", &cycle, 4, 4);
    memRefreshVerdict(&w.report);
    CHECK(f.severity == memLikely);
    CHECK(w.report.verdict == memVerdictLikely);
    std::string text = render(w, false);
    CHECK(has(text, "    held by: nothing outside a cycle (4 of 4 sampled objects)\n"));
    CHECK(has(text, "a leak."));
}

TEST_CASE("Mem suspects: held from outside the heap does not change the severity")
{
    Window w = healthy();
    w.specs[1].sites[0] = {.line = 10, .allocs = 3000, .live = 1100, .fresh = 1000};
    w.specs[0].sites[0].allocs = 2000;
    w.run();
    MemShape outside = shapeHeldBy(memHeldOutside);
    memApplySuspect(&g1Of(w), "Node", "0x10", &outside, 2, 2);
    CHECK(g1Of(w).severity == memWatch);
    CHECK(has(render(w, false), "    held by: held from outside the heap (2 of 2 sampled object"));

    MemShape holder = shapeHeldBy(memHeldOutside);
    holder.stepCount = 1;
    snprintf(holder.steps[0], sizeof(holder.steps[0]), "Holder");
    memApplySuspect(&g1Of(w), "Node", "0x10", &holder, 1, 1);
    CHECK(has(render(w, false), "    held by: Holder (held from outside the heap) (1 of 1 sampled object)\n"));
}

TEST_CASE("Mem suspects: a cache that is flattening stays info whatever holds it")
{
    Window w = growing(100, 60, 10); // plateau: G1 is info
    w.run();
    MemFinding &f = g1Of(w);
    REQUIRE(f.severity == memInfo);
    MemShape cycle = shapeHeldBy(memHeldCycle);
    memApplySuspect(&f, "Node", "0x10", &cycle, 2, 2);
    CHECK(f.severity == memInfo);
}

TEST_CASE("Mem suspects: a type that is not known leaves the title alone")
{
    Window w = growing(100, 60, 70);
    w.run();
    MemFinding &f = g1Of(w);
    std::string before = f.title;
    MemShape shape = cacheShape();
    memApplySuspect(&f, nullptr, "0x10", &shape, 1, 1);
    CHECK(std::string(f.title) == before);
    CHECK(std::string(f.type).empty());
}

TEST_CASE("Mem suspects: JSON has the type, the retention and the suspects")
{
    Window w = growing(100, 60, 70);
    w.run();
    MemShape shape = cacheShape();
    memApplySuspect(&g1Of(w), "Node", "0x79c6c00bf0", &shape, 3, 3);
    JsonDoc doc(w, false);
    REQUIRE(doc.root != nullptr);

    CHECK(doc.string("findings.0.type") == "Node");
    CHECK(doc.string("findings.0.retention.heldBy") == "global");
    CHECK(doc.number("findings.0.retention.sampled") == 3);
    CHECK(doc.number("findings.0.retention.covered") == 3);
    REQUIRE(doc.size("findings.0.retention.shape") == 3);
    CHECK(doc.string("findings.0.retention.shape.0") == "global cache");
    CHECK(doc.string("findings.0.retention.shape.2") == "buffer app.cxy:13");
    CHECK(doc.string("findings.0.retention.ptr") == "0x79c6c00bf0");
    CHECK(doc.string("findings.0.suggest.0") == "cxy utils mem path 0x79c6c00bf0");

    REQUIRE(doc.size("suspects") == 1);
    CHECK(doc.string("suspects.0.ptr") == "0x79c6c00bf0");
    CHECK(doc.string("suspects.0.type") == "Node");
    CHECK(doc.string("suspects.0.site.file") == "app.cxy");
    CHECK(doc.number("suspects.0.site.line") == 10);
}

TEST_CASE("Mem suspects: a finding nobody looked into has no retention")
{
    Window w = growing(100, 60, 70);
    JsonDoc doc(w);
    REQUIRE(doc.root != nullptr);
    CHECK(doc.size("suspects") == 0);
    cJSON *retention = doc.at("findings.0.retention");
    REQUIRE(retention != nullptr);
    CHECK((retention->type & 0xFF) == cJSON_NULL);
    CHECK_FALSE(has(render(w), "held by"));
}

TEST_CASE("Mem report: --fail-on")
{
    MemSeverity s;
    CHECK(memParseSeverity("info", &s));
    CHECK(s == memInfo);
    CHECK(memParseSeverity("watch", &s));
    CHECK(s == memWatch);
    CHECK(memParseSeverity("likely", &s));
    CHECK(s == memLikely);
    CHECK(memParseSeverity("definite", &s));
    CHECK(s == memDefinite);
    CHECK_FALSE(memParseSeverity("loud", &s));
    CHECK_FALSE(memParseSeverity("", &s));
    CHECK_FALSE(memParseSeverity(nullptr, &s));

    Window w = healthy();
    w.specs[1].zombies = 20; // Z1, watch
    const MemReport &r = w.run();
    CHECK(memReached(&r, memInfo));
    CHECK(memReached(&r, memWatch));
    CHECK_FALSE(memReached(&r, memLikely));
    CHECK_FALSE(memReached(&r, memDefinite));

    Window clean = healthy();
    CHECK_FALSE(memReached(&clean.run(), memInfo));
}

/* X1 */

TEST_CASE("Mem rules X1: objects kept alive from outside the heap")
{
    Window w = healthy();
    w.specs[1].externalObjects = 100; // the minimum
    w.specs[1].externalRefs = 130;
    const MemFinding *f = find(w.run(), memRuleX1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
    CHECK(evidence(*f, "objects") == 100);
    CHECK(evidence(*f, "refs") == 130);
    CHECK(std::string(f->title).find("100 objects (130 references)") != std::string::npos);

    w.specs[1].externalObjects = 99;
    CHECK(find(w.run(), memRuleX1) == nullptr);
}

TEST_CASE("Mem rules X1: likely when the count grew in each of the last three snapshots")
{
    Window w = growing(100, 60, 70);
    w.specs[1].externalObjects = 100;
    w.specs[2].externalObjects = 150;
    w.specs[3].externalObjects = 200;
    const MemFinding *f = find(w.run(), memRuleX1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memLikely);

    w.specs[3].externalObjects = 150; // not growing any more
    f = find(w.run(), memRuleX1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
}

/* B1 */

TEST_CASE("Mem rules B1: a buffer site that allocates again and again")
{
    Window w = healthy();
    w.specs[0].sites.push_back({.line = 20});
    w.specs[1].sites.push_back({.line = 20, .allocs = 150, .bufferAllocs = 150, .liveBuffers = 5});
    const MemFinding *f = find(w.run(), memRuleB1, "app.cxy:20");
    REQUIRE(f != nullptr);
    CHECK(f->severity == memInfo);
    CHECK(evidence(*f, "allocs") == 150);
    CHECK(evidence(*f, "alive") == 5);
    CHECK(std::string(f->fix).find("reserve") != std::string::npos);
}

TEST_CASE("Mem rules B1: boundaries")
{
    Window w = healthy();
    w.specs[0].sites.push_back({.line = 20});
    auto at = [&](u64 made, u64 alive) {
        w.specs[1].sites.back() = {.line = 20, .allocs = made, .bufferAllocs = made, .liveBuffers = alive};
        return find(w.run(), memRuleB1) != nullptr;
    };
    w.specs[1].sites.push_back({.line = 20});
    CHECK(at(100, 10));  // exactly the minimum count and ten allocations per buffer
    CHECK_FALSE(at(99, 1));
    CHECK_FALSE(at(100, 11)); // most of it is alive: growth, G1 looks at that
    CHECK(at(150, 15));
    CHECK_FALSE(at(150, 16));
}

TEST_CASE("Mem rules B1: only what was allocated since the baseline counts")
{
    Window w = healthy();
    w.specs[0].sites.push_back({.line = 20, .allocs = 100, .bufferAllocs = 100});
    w.specs[1].sites.push_back({.line = 20, .allocs = 180, .bufferAllocs = 180, .liveBuffers = 1});
    CHECK(find(w.run(), memRuleB1) == nullptr); // 80 since the baseline
    w.specs[1].sites.back().bufferAllocs = 200;
    const MemFinding *f = find(w.run(), memRuleB1);
    REQUIRE(f != nullptr);
    CHECK(evidence(*f, "allocs") == 100);
}

/* H1 */

TEST_CASE("Mem rules H1: an object with far more references than the others")
{
    Window w = healthy();
    w.specs[1].maxRefs = 100; // the minimum, and more than 20 times the average of 1.5
    const MemFinding *f = find(w.run(), memRuleH1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memWatch);
    CHECK(evidence(*f, "maxRefs") == 100);
    CHECK(evidence(*f, "meanRefs") == doctest::Approx(1.5));
    CHECK(std::string(f->title).find("A `Node` object made at app.cxy:10 has 100 references, the average is 1.5") !=
          std::string::npos);
    CHECK(std::string(f->see) == "cxy utils mem refs 5");
}

TEST_CASE("Mem rules H1: boundaries")
{
    Window w = healthy();
    w.specs[1].maxRefs = 99; // below the minimum
    CHECK(find(w.run(), memRuleH1) == nullptr);

    // a program where everything is shared: 1000 is only 20 times an average of 50
    w.specs[1].totalRefs = 5000;
    w.specs[1].maxRefs = 999;
    CHECK(find(w.run(), memRuleH1) == nullptr);
    w.specs[1].maxRefs = 1000;
    CHECK(find(w.run(), memRuleH1) != nullptr);
}

TEST_CASE("Mem rules H1: likely when the highest count grew in each of the last three snapshots")
{
    Window w = growing(100, 60, 70);
    for (Snap &s : w.specs) {
        s.refCount = 100;
        s.totalRefs = 150;
    }
    w.specs[1].maxRefs = 150;
    w.specs[2].maxRefs = 200;
    w.specs[3].maxRefs = 300;
    const MemFinding *f = find(w.run(), memRuleH1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memLikely);
}

TEST_CASE("Mem rules H1: not run when the program could not read the counts")
{
    Window w = healthy();
    w.specs[1].refCount = 0; // the builder leaves the refs out
    const MemReport &r = w.run();
    CHECK_FALSE(ran(r, memRuleH1));
}

/* M1 */

TEST_CASE("Mem rules M1: the process holds much more than the traced heap")
{
    Window w = healthy();
    w.specs[1].rssBytes = 64u << 20;
    const MemFinding *f = find(w.run(), memRuleM1);
    REQUIRE(f != nullptr);
    CHECK(f->severity == memInfo);
    CHECK(evidence(*f, "rssBytes") == 64 * 1024 * 1024);
    CHECK(evidence(*f, "tracedBytes") == 1600);
    // the baseline had 8 MB: the process grew, the traced heap did not
    CHECK(evidence(*f, "rssDelta") == 56 * 1024 * 1024);
    CHECK(evidence(*f, "tracedDelta") == 0);
    CHECK(std::string(f->title).find("The process holds 64.0 MB but only 1.6 KB is in the traced heap") !=
          std::string::npos);
}

TEST_CASE("Mem rules M1: boundaries")
{
    Window w = healthy();
    w.specs[1].rssBytes = (32u << 20) - 1; // below the minimum
    CHECK(find(w.run(), memRuleM1) == nullptr);
    w.specs[1].rssBytes = 32u << 20;
    CHECK(find(w.run(), memRuleM1) != nullptr);

    // the traced heap is a quarter of the process: not a gap
    w.specs[1].objectBytes = 8u << 20;
    CHECK(find(w.run(), memRuleM1) == nullptr);
    w.specs[1].objectBytes = (8u << 20) - 1;
    CHECK(find(w.run(), memRuleM1) != nullptr);
}

TEST_CASE("Mem rules M1: not run when the process size is unknown")
{
    Window w = healthy();
    w.specs[1].rssBytes = 0;
    CHECK_FALSE(ran(w.run(), memRuleM1));
}

/* A1 */

TEST_CASE("Mem rules A1: survivors that are young are work in flight")
{
    Window w = healthy();
    w.specs[0].sites[0].allocs = 2000;
    w.specs[1].sites[0] = {.line = 10, .allocs = 3000, .live = 1100, .fresh = 1000, .young = 600};
    const MemReport &r = w.run();
    const MemFinding *g1 = find(r, memRuleG1);
    REQUIRE(g1 != nullptr);
    CHECK(g1->severity == memInfo);
    CHECK(std::string(g1->title).find("most were made in the last tenth of the window") != std::string::npos);
    const MemFinding *a1 = find(r, memRuleA1);
    REQUIRE(a1 != nullptr);
    CHECK(a1->severity == memInfo);
    CHECK(evidence(*a1, "young") == 600);
    CHECK(evidence(*a1, "alive") == 1000);
    CHECK(r.verdict == memVerdictOk); // nothing above info
}

TEST_CASE("Mem rules A1: boundaries")
{
    Window w = healthy();
    w.specs[0].sites[0].allocs = 2000;
    auto young = [&](u64 n) {
        w.specs[1].sites[0] = {.line = 10, .allocs = 3000, .live = 1100, .fresh = 1000, .young = n};
        return w.run();
    };
    const MemReport &half = young(500); // exactly half
    CHECK(find(half, memRuleA1) != nullptr);
    const MemReport &below = young(499);
    CHECK(find(below, memRuleA1) == nullptr);
    CHECK(find(below, memRuleG1)->severity == memWatch);
    // a steady leak has about a tenth of its survivors in the last tenth
    const MemReport &steady = young(100);
    CHECK(find(steady, memRuleA1) == nullptr);
    CHECK(find(steady, memRuleG1)->severity == memWatch);
}

TEST_CASE("Mem rules: the new rules are in the clean list when they ran and found nothing")
{
    Window w = healthy();
    JsonDoc doc(w);
    REQUIRE(doc.root != nullptr);
    bool found[12] = {};
    for (int i = 0; i < doc.size("checked"); i++) {
        std::string id = doc.string("checked." + std::to_string(i));
        for (int r = 0; r < memRuleCount; r++)
            found[r] = found[r] || id == memRuleId((MemRule)r);
    }
    for (int r = 0; r < memRuleCount; r++) {
        INFO(memRuleId((MemRule)r));
        CHECK(found[r] == (r != memRuleG2));
    }
}

TEST_CASE("Mem rules: names")
{
    CHECK(std::string(memRuleId(memRuleG1)) == "G1");
    CHECK(std::string(memRuleId(memRuleK1)) == "K1");
    CHECK(std::string(memSeverityName(memDefinite)) == "definite");
    CHECK(std::string(memVerdictName(memVerdictNotEnough)) == "not enough activity");
    CHECK(std::string(memVerdictName(memVerdictOk)) == "ok");
}
