/**
 * The C the backend writes for a `&T`: one that is loaded from a field or returned
 * by a call is checked before it is used (`__mm_ref`, a no-op in release builds),
 * one that is a parameter or a local is not, the caller vouches for it.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
class View {
    src: &Node
    func `init`(n: &Node) { this.src = n }
    func get(): &Node => src
    func fromField(): i32 => src.id
    func throughCall(): i32 => get().id
    func ignore() { get() }
}
func fromParam(n: &Node): i32 => n.id
func main(): void {
    var n = Node(1)
    var v = View(&n)
    v.ignore()
    var total = v.fromField() + v.throughCall() + fromParam(&n)
}
)";

CompileOptions generated(std::vector<std::string> extra = {})
{
    CompileOptions options;
    options.stage = "Codegen";
    options.keepGeneratedC = true;
    options.extraArgs = std::move(extra);
    return options;
}

// The C of one function, from its signature to the closing brace at column 0
std::string functionBody(const std::string &c, const std::string &name)
{
    // The definition is the occurrence followed by `{` before any `;`: a call or a
    // declaration ends with `;` first
    for (size_t at = c.find(name + "("); at != std::string::npos; at = c.find(name + "(", at + 1)) {
        size_t open = c.find_first_of("{;", at);
        if (open == std::string::npos || c[open] != '{')
            continue;
        size_t close = c.find("\n}", open);
        return close == std::string::npos ? std::string{} : c.substr(open, close - open);
    }
    return {};
}

bool contains(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("Borrowed loads: a `&T` field read through is checked")
{
    TestCompiler cc(generated());
    REQUIRE(cc.compile(program));
    INFO(cc.diagnostics());

    const std::string body = functionBody(cc.generatedC(), "View_fromField");
    REQUIRE_FALSE(body.empty());
    CHECK(contains(body, "__mm_ref(this->src,"));
}

TEST_CASE("Borrowed loads: a `&T` a call returns is checked before it is used")
{
    TestCompiler cc(generated());
    REQUIRE(cc.compile(program));

    const std::string body = functionBody(cc.generatedC(), "View_throughCall");
    REQUIRE_FALSE(body.empty());
    CHECK(contains(body, "__mm_ref(View_get("));
}

TEST_CASE("Borrowed loads: a call whose result is discarded is not checked")
{
    TestCompiler cc(generated());
    REQUIRE(cc.compile(program));

    const std::string body = functionBody(cc.generatedC(), "View_ignore");
    REQUIRE_FALSE(body.empty());
    CHECK(contains(body, "View_get("));
    CHECK_FALSE(contains(body, "__mm_ref"));
}

TEST_CASE("Borrowed loads: a `&T` parameter is not checked, the caller vouches for it")
{
    TestCompiler cc(generated());
    REQUIRE(cc.compile(program));

    const std::string body = functionBody(cc.generatedC(), "fromParam");

    REQUIRE_FALSE(body.empty());
    CHECK_FALSE(contains(body, "__mm_ref"));
}

TEST_CASE("Borrowed loads: the visit glue reports a `&T` field as a borrow, not an edge")
{
    TestCompiler cc(generated({"--memory-trace"}));
    REQUIRE(cc.compile(program));
    INFO(cc.diagnostics());

    const std::string body = functionBody(cc.generatedC(), "__visit_View");
    REQUIRE_FALSE(body.empty());
    CHECK(contains(body, "__visit_borrow(v, obj, o->src)"));
    CHECK_FALSE(contains(body, "__visit_edge"));
}

TEST_CASE("Borrowed loads: without memory trace there is no visit glue")
{
    TestCompiler cc(generated());
    REQUIRE(cc.compile(program));
    CHECK_FALSE(contains(cc.generatedC(), "__visit_borrow(v,"));
}
