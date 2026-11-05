/**
 * Bugs found in the memory pass, written as the IR they must produce. A test
 * was skipped while its bug was open. All three are fixed and stay as
 * regression tests. See `docs/stage/memory-pass.md`, sections 1 and 3.2.
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
func make(): Node => Node(1)
func peek(n: &Node): i32 => n.id
var counter = 0
func next(): Node {
    counter = counter + 1
    return Node(counter)
}
)";

CompileOptions memoryStage()
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    return options;
}

bool contains(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

// Fixed by removing the `_r` hoisting from simplify: lowering keeps the object in a local
TEST_CASE("Fixed bug: an owned call result passed to a `&T` parameter is dropped")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(): i32 {
    return peek(make())
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    // `make()` is owned by nobody, so the pass has to drop it
    CHECK(contains(cc.function("demo").sexpr(), "(Drop"));
}

TEST_CASE("Fixed bug: `var r = &make()` keeps the object alive and frees it")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(): i32 {
    var r = &make()
    return r.id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    CHECK(contains(cc.function("demo").sexpr(), "(Drop"));
}

// Fixed by removing the hoisting from simplify (memory-pass.md, 3.2)
TEST_CASE("Fixed bug: the condition of a `while` is evaluated on every iteration")
{
    CompileOptions options;
    options.stage = "Lower";
    TestCompiler cc(options);
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(): i32 {
    var total = 0
    while next().id < 4 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper fn = cc.function("demo");
    REQUIRE(fn);
    // `next()` is called by the condition block, not once before the loop
    CHECK(contains(fn.findBlock("WHILE0_cond").sexpr(), "(GlobalRef next)"));
    CHECK_FALSE(contains(fn.findBlock("entry").sexpr(), "(GlobalRef next)"));
}

// Open. The elaboration does not treat a `@copy` written by the user as a store
// into its destination, so the old value is not dropped first. Fixed by making the
// three copies of the ownership rules agree on which statements store (memory-pass.md)
TEST_CASE("Known bug: `x = @copy r` drops the old value of `x` first" * doctest::skip())
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(r: &Node) {
    var d = make()
    d = @copy r
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    REQUIRE_IR_MATCHES(cc.function("demo").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Drop (VarRef d))
            (Copy (VarRef r) (VarRef d) :user-written)
            ...)
    )");
}
