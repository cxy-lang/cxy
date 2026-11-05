/**
 * Retain and release pairs (design 8, plan phase 7b). `var b = a` retains the
 * object and the scope exit releases it again. When `b` is only read and `a` keeps
 * its own count the whole time, `b` is an alias and neither is needed.
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
struct Holder { n: Node }
func take(n: Node) {}
func peek(n: &Node): i32 => n.id
)";

CompileOptions verified()
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    options.extraArgs = {"--warnings=Ownership"};
    return options;
}

size_t count(const std::string &text, const std::string &part)
{
    size_t n = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
        n++;
    return n;
}

} // namespace

TEST_CASE("Pairs: a copy that is only read is an alias")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func aliasRead(a: Node): i32 {
    var b = a
    return peek(b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("aliasRead").sexpr();
    CHECK(count(text, "(Copy") == 0);
    CHECK(count(text, "(Drop (VarRef b))") == 0);
    CHECK(count(text, "(Drop (VarRef a))") == 1);
    CHECK(count(text, "(Assign (VarRef b) (VarRef a") == 1);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: a copy read in a loop body is an alias on every iteration")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func inLoop(a: Node, n: i32): i32 {
    var total: i32 = 0
    var i: i32 = 0
    while i < n {
        var b = a
        total = total + peek(b)
        i = i + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("inLoop").sexpr();
    CHECK(count(text, "(Copy") == 0);
    CHECK(count(text, "(Drop (VarRef b))") == 0);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: a copy that is itself copied from stays an alias, the new owner has its own count")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func copiedFrom(a: Node) {
    var b = a
    take(b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("copiedFrom").sexpr();
    // only `take(b)` retains, for its own owner
    CHECK(count(text, "(Copy") == 1);
    CHECK(count(text, "(Drop (VarRef b))") == 0);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: kept when the source is assigned while the copy is alive")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func reassigned(a: Node): i32 {
    var b = a
    a = Node(2)
    return peek(b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // `b` is the only thing that keeps the first object alive
    const std::string text = cc.function("reassigned").sexpr();
    CHECK(count(text, "(Copy (VarRef a") == 1);
    CHECK(count(text, "(Drop (VarRef b))") == 1);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: kept when the source is moved while the copy is alive")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func movedAway(a: Node): i32 {
    var b = a
    take(&&a)
    return peek(b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("movedAway").sexpr();
    CHECK(count(text, "(Copy (VarRef a") == 1);
    CHECK(count(text, "(Drop (VarRef b))") == 1);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: kept when the copy is moved away, it has an owner of its own")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func copyMoved(a: Node, flag: bool) {
    var b = a
    if flag {
        take(&&b)
    }
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("copyMoved").sexpr();
    CHECK(count(text, "(Copy (VarRef a") == 1);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: a copy of a copy is still one alias of the source")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func chain(a: Node): i32 {
    var b = a
    var c = b
    return peek(c)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("chain").sexpr();
    CHECK(count(text, "(Copy") == 0);
    CHECK(count(text, "(Drop (VarRef a))") == 1);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: a copy the user wrote with `@copy` is kept, it asks for an owner")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func userCopy(a: Node): i32 {
    var b = @copy a
    return peek(b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("userCopy").sexpr();
    CHECK(count(text, "(Copy (VarRef a") == 1);
    CHECK(count(text, "(Drop (VarRef b))") == 1);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Pairs: aggregates are left alone, their copy calls glue")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func structCopy(h: Holder): i32 {
    var g = h
    return peek(g.n)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("structCopy").sexpr();
    CHECK(count(text, "(Copy (VarRef h") == 1);
    CHECK(count(text, "(Drop (VarRef g))") == 1);
}
