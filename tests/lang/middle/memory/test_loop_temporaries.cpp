/**
 * A temporary of a `while` condition lives across iterations: it is declared
 * once before the loop, dropped before it is assigned again and at the end. The
 * ownership verifier finds nothing wrong with that.
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
var counter = 0
func next(): Node {
    counter = counter + 1
    return Node(counter)
}
func adder(n: i32): func(i32) -> i32 {
    return (x: i32) => x + n
}
)";

CompileOptions verified(const char *stage)
{
    CompileOptions options;
    options.stage = stage;
    options.extraArgs = {"--warnings=Ownership"};
    return options;
}

} // namespace

TEST_CASE("Loop temporaries: the object of a `while` condition is declared before the loop")
{
    TestCompiler cc(verified("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total = 0
    while next().id < 4 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper fn = cc.function("loop");
    REQUIRE_IR_MATCHES(fn.findBlock("entry"), R"(
        (Block _L_entry
            ...
            (VarDecl __t0 ^Node :class-pending :synthetic)
            (Goto _L_WHILE0_cond))
    )");
    REQUIRE_IR_MATCHES(fn.findBlock("WHILE0_cond"), R"(
        (Block _L_WHILE0_cond
            (Assign (VarRef __t0) (Call (GlobalRef next) :owned-position))
            (If (Binary < (Member (VarRef __t0) id) 4) _L_WHILE0_body _L_WHILE0_exit))
    )");
}

TEST_CASE("Loop temporaries: the verifier accepts a `while` condition that owns an object")
{
    TestCompiler cc(verified("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total = 0
    while next().id < 4 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Loop temporaries: the verifier accepts a closure callee in a `while` condition")
{
    TestCompiler cc(verified("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total = 0
    while adder(1)(total) < 3 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}

TEST_CASE("Loop temporaries: a temporary of the body is still declared in the body")
{
    TestCompiler cc(verified("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total: i32 = 0
    while total < 3 {
        total = total + next().id
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("loop").findBlock("WHILE0_body"), R"(
        (Block _L_WHILE0_body
            (ScopeEnter 1)
            (VarDecl __t0 ^Node :class-pending :synthetic)
            ...)
    )");
}
