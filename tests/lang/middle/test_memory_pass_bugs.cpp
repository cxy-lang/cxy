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

// A `@copy` written by the user is a store: the old value of the destination is
// dropped first, like for an assignment.
TEST_CASE("Fixed bug: `x = @copy r` drops the old value of `x` first")
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

// `x = &&x.next` read `x.next` after the old `x` was dropped. The value is taken
// out first, then the old value is dropped, then it is stored.
TEST_CASE("Fixed bug: `x = &&x.next` reads the field before dropping `x`")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(R"(
class Link {
    next: Link
    func `init`() {}
    func `deinit`() {}
}
func pop(head: Link) {
    var h = &&head
    h = &&h.next
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    REQUIRE_IR_MATCHES(cc.function("pop").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __mv0) (Member (VarRef h) next ...) :unmanaged)
            (Assign (Member (VarRef h) next) null :unmanaged)
            (Drop (VarRef h))
            (Assign (VarRef h) (VarRef __mv0 :owned-position :moved))
            ...)
    )");
}

// The value of an expression statement is dropped right after it is made. The
// analysis did not know, so the scope exit dropped it a second time and the
// program panicked with a double drop.
TEST_CASE("Fixed bug: a discarded owned call result is dropped once")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo() {
    make()
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("demo").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __t0 ^Node :ephemeral)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (Drop (VarRef __t0))
            (Goto _L_exit))
    )");
    REQUIRE_IR_MATCHES(cc.function("demo").findBlock("exit"),
                       "(Block _L_exit (ScopeExit 0) (Return))");
}
