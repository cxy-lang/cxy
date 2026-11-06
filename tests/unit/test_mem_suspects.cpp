/**
 * Unit Tests: looking into the suspects of `analyze` (src/cxy/utils/mem/suspects.c):
 * the shape of an owner chain, grouping, which objects to sample.
 */

#include "doctest.h"

#include "utils/mem/suspects.h"

#include <string>
#include <vector>

namespace {

std::string node(const std::string &kind,
                 const std::string &type,
                 const std::string &name = "",
                 const std::string &file = "app.cxy",
                 int line = 10)
{
    auto quoted = [](const std::string &s) { return s.empty() ? std::string("null") : "\"" + s + "\""; };
    return "{\"kind\":\"" + kind + "\",\"ptr\":\"0x1000\",\"size\":16,\"name\":" + quoted(name) +
           ",\"refs\":1,\"type\":" + quoted(type) + ",\"file\":" + quoted(file) +
           ",\"line\":" + std::to_string(line) + "}";
}

std::string pathDoc(const std::vector<std::string> &nodes, bool outside = false)
{
    std::string path;
    for (size_t i = 0; i < nodes.size(); i++)
        path += (i ? "," : "") + nodes[i];
    return "{\"found\":true,\"reason\":null,\"depth\":" + std::to_string(nodes.size() - 1) +
           ",\"heldFromOutside\":" + (outside ? "true" : "false") + ",\"path\":[" + path +
           "],\"approximate\":0}";
}

MemShape shapeOf(const std::string &json)
{
    MemShape shape;
    REQUIRE_MESSAGE(memShapeFromPath(json.c_str(), &shape), json);
    return shape;
}

std::string step(const MemShape &s, u32 i)
{
    return s.steps[i];
}

} // namespace

TEST_CASE("Mem suspects: a cache held by a global")
{
    MemShape s = shapeOf(pathDoc({node("global", "", "cache", "", 0),
                                  node("object", "Vector", "", "app.cxy", 12),
                                  node("buffer", "", "", "app.cxy", 13),
                                  node("object", "Node", "", "app.cxy", 30)}));
    CHECK(s.heldBy == memHeldGlobal);
    REQUIRE(s.stepCount == 3);
    CHECK(step(s, 0) == "global cache");
    CHECK(step(s, 1) == "Vector");
    CHECK(step(s, 2) == "buffer app.cxy:13");
}

TEST_CASE("Mem suspects: held from outside the heap")
{
    MemShape s = shapeOf(pathDoc({node("object", "Holder", "", "app.cxy", 5),
                                  node("object", "Node", "", "app.cxy", 30)},
                                 true));
    CHECK(s.heldBy == memHeldOutside);
    REQUIRE(s.stepCount == 1);
    CHECK(step(s, 0) == "Holder");

    // The object itself is what nothing owns
    MemShape alone = shapeOf(pathDoc({node("object", "Node")}, true));
    CHECK(alone.heldBy == memHeldOutside);
    CHECK(alone.stepCount == 0);
}

TEST_CASE("Mem suspects: held only through cycles")
{
    MemShape s = shapeOf(
        "{\"found\":false,\"reason\":\"cycle\",\"depth\":null,\"heldFromOutside\":null,\"path\":[],"
        "\"approximate\":0}");
    CHECK(s.heldBy == memHeldCycle);
    CHECK(s.stepCount == 0);
}

TEST_CASE("Mem suspects: an object without a class is named by its site")
{
    MemShape s = shapeOf(pathDoc({node("global", "", "g", "", 0),
                                  node("object", "", "", "legacy.cxy", 7),
                                  node("object", "Node")}));
    REQUIRE(s.stepCount == 2);
    CHECK(step(s, 1) == "object legacy.cxy:7");
}

TEST_CASE("Mem suspects: a long chain keeps its ends")
{
    std::vector<std::string> nodes = {node("global", "", "root", "", 0)};
    for (int i = 1; i <= 11; i++)
        nodes.push_back(node("object", "T" + std::to_string(i)));
    nodes.push_back(node("object", "Node")); // the object asked about
    MemShape s = shapeOf(pathDoc(nodes));
    // 12 owners: the first four, `...`, the last three
    REQUIRE(s.stepCount == 8);
    CHECK(step(s, 0) == "global root");
    CHECK(step(s, 1) == "T1");
    CHECK(step(s, 3) == "T3");
    CHECK(step(s, 4) == "...");
    CHECK(step(s, 5) == "T9");
    CHECK(step(s, 7) == "T11");

    // exactly the limit is not shortened
    nodes.resize(MEM_SHAPE_MAX + 1);
    MemShape full = shapeOf(pathDoc(nodes));
    CHECK(full.stepCount == (u32)MEM_SHAPE_MAX);
    CHECK(step(full, 7) == "T7");
}

TEST_CASE("Mem suspects: what is not a path gives no shape")
{
    MemShape s;
    CHECK_FALSE(memShapeFromPath(nullptr, &s));
    CHECK_FALSE(memShapeFromPath("", &s));
    CHECK_FALSE(memShapeFromPath("not json", &s));
    CHECK_FALSE(memShapeFromPath("{\"error\":\"0x10 is not the start of a live object\"}", &s));
    CHECK_FALSE(memShapeFromPath("{\"found\":true}", &s)); // no path
    CHECK_FALSE(memShapeFromPath("[]", &s));
}

TEST_CASE("Mem suspects: shapes are equal when owner, chain and way of being held match")
{
    MemShape a = shapeOf(pathDoc({node("global", "", "cache", "", 0), node("object", "Vector"), node("object", "Node")}));
    MemShape same = shapeOf(pathDoc({node("global", "", "cache", "", 0), node("object", "Vector"), node("object", "Other")}));
    MemShape otherRoot = shapeOf(pathDoc({node("global", "", "other", "", 0), node("object", "Vector"), node("object", "Node")}));
    MemShape shorter = shapeOf(pathDoc({node("global", "", "cache", "", 0), node("object", "Node")}));
    MemShape outside = shapeOf(pathDoc({node("object", "Vector"), node("object", "Node")}, true));
    CHECK(memShapeEqual(&a, &same)); // the object asked about is not part of it
    CHECK_FALSE(memShapeEqual(&a, &otherRoot));
    CHECK_FALSE(memShapeEqual(&a, &shorter));
    CHECK_FALSE(memShapeEqual(&a, &outside));
}

TEST_CASE("Mem suspects: the largest group of shapes wins")
{
    MemShape a = shapeOf(pathDoc({node("global", "", "a", "", 0), node("object", "Node")}));
    MemShape b = shapeOf(pathDoc({node("global", "", "b", "", 0), node("object", "Node")}));
    u32 covered = 0;

    std::vector<MemShape> mixed = {a, b, a, a, b};
    CHECK(memGroupShapes(mixed.data(), (u32)mixed.size(), &covered) == 0);
    CHECK(covered == 3);

    std::vector<MemShape> later = {b, a, a, a};
    CHECK(memGroupShapes(later.data(), (u32)later.size(), &covered) == 1);
    CHECK(covered == 3);

    // A tie goes to the group seen first
    std::vector<MemShape> tie = {b, a, a, b};
    CHECK(memGroupShapes(tie.data(), (u32)tie.size(), &covered) == 0);
    CHECK(covered == 2);

    std::vector<MemShape> one = {a};
    CHECK(memGroupShapes(one.data(), 1, &covered) == 0);
    CHECK(covered == 1);
}

TEST_CASE("Mem suspects: samples are spread over the whole list")
{
    u32 picked[8];
    REQUIRE(memPickSamples(100, 8, picked) == 8);
    const u32 expected[] = {0, 12, 25, 37, 50, 62, 75, 87};
    for (int i = 0; i < 8; i++)
        CHECK(picked[i] == expected[i]);

    // fewer objects than wanted: all of them, once each
    REQUIRE(memPickSamples(3, 8, picked) == 3);
    CHECK(picked[0] == 0);
    CHECK(picked[1] == 1);
    CHECK(picked[2] == 2);

    CHECK(memPickSamples(0, 8, picked) == 0);
    REQUIRE(memPickSamples(10, 1, picked) == 1);
    CHECK(picked[0] == 0);
    CHECK(memPickSamples(10, 0, picked) == 0);
}
