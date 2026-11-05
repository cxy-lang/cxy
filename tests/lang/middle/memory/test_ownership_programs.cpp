/**
 * The ownership verifier on real programs, compiled with `--warnings=Ownership`.
 * A correct program has no finding, a misuse is reported once.
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

bool reports(const TestCompiler &cc, const char *rule)
{
    return cc.diagnostics().find(std::string("warning[") + rule + "]") != std::string::npos;
}

} // namespace

TEST_CASE("Ownership programs: a use after a move is reported")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func misuse(a: Node): i32 {
    take(&&a)
    return peek(a)
}
func main(): void {}
)"));
    CHECK(reports(cc, "UseAfterMove"));
}

TEST_CASE("Ownership programs: moves and copies that are correct are not reported")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func fine(a: Node, b: Node): i32 {
    take(@copy a)
    take(&&b)
    return peek(a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK_FALSE(reports(cc, "UseAfterMove"));
    CHECK_FALSE(reports(cc, "OwnerLeak"));
}

// `move a` comes before the call so the drop flag is cleared first, but the call
// still reads `a.id` as its first argument, before it takes `a`
TEST_CASE("Ownership programs: reading a variable in the statement that moves it is fine")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func both(id: i32, n: Node) {}
func readThenMove(a: Node) {
    both(a.id, &&a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK_FALSE(reports(cc, "UseAfterMove"));
}

// Every temporary the compiler makes for these is consumed or dropped once, O10
TEST_CASE("Ownership programs: temporaries are consumed or dropped exactly once")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func make(): Node => Node(1)
func pass(n: Node): Node => n
func temporaries(): i32 {
    make()
    take(pass(make()))
    var r = &make()
    var h = Holder{n: make()}
    if make().id > 0 {
        return peek(make()) + r.id
    }
    return make().id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK_FALSE(reports(cc, "TemporaryOwnership"));
    CHECK_FALSE(reports(cc, "OwnerLeak"));
}

TEST_CASE("Ownership programs: moving out of a reference into a variable is reported")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaAssign(r: &Node) {
    var o = &&r
}
func main(): void {}
)"));
    CHECK(reports(cc, "MoveOfReference"));
}

TEST_CASE("Ownership programs: moving out of a reference is reported for a borrowing parameter")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaBorrowed(r: &Node): i32 {
    return peek(&&r)
}
func main(): void {}
)"));
    CHECK(reports(cc, "MoveOfReference"));
}

// A move out of a field is followed by the statement that clears it, the clear
// comes before the statement that uses the value
TEST_CASE("Ownership programs: a field moved into a call is cleared, nothing is reported")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaCall() {
    var h = Holder{n: Node(1)}
    take(&&h.n)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK_FALSE(reports(cc, "UnclearedMove"));
}

TEST_CASE("Ownership programs: a field moved into a variable is cleared, nothing is reported")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaAssign() {
    var h = Holder{n: Node(1)}
    var x = &&h.n
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK_FALSE(reports(cc, "UnclearedMove"));
}

TEST_CASE("Ownership programs: an array element moved out is cleared, nothing is reported")
{
    TestCompiler cc(verified());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaElement() {
    var arr = [Node(1), Node(2)]
    var r = &&arr.[1]
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK_FALSE(reports(cc, "UnclearedMove"));
}
