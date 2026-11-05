/**
 * Calling a closure reads `.fn` and `.env` from the callee, so a callee that
 * is not a variable is evaluated once, into a local, before the call.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
func adder(n: i32): func(i32) -> i32 {
    return (x: i32) => x + n
}
)";

CompileOptions stage(const char *name)
{
    CompileOptions options;
    options.stage = name;
    return options;
}

} // namespace

TEST_CASE("Lower closure call: a closure variable is read directly")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaVar(f: func(i32) -> i32): i32 => f(1)
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaVar").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (Assign (VarRef __ret) (Call (Member (VarRef f) fn) (Member (VarRef f) env) 1))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower closure call: the closure returned by a call is evaluated once")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaCall(): i32 => adder(2)(3)
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaCall").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl __t0 _ :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef adder) _ :owned-position))
            (Assign (VarRef __ret)
                (Call (Member (VarRef __t0) fn) (Member (VarRef __t0) env) 3))
            (Goto _L_exit))
    )");
}

// A lambda that does not escape has no environment to free, so its local is `:temporary`
TEST_CASE("Lower closure call: a lambda called in place is evaluated once")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(R"(
func viaLambda(): i32 => ((x: i32) => x + 1)(4)
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaLambda").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl __t0 _ :synthetic)
            (Assign (VarRef __t0) (Closure null (GlobalRef _)))
            (Assign (VarRef __ret)
                (Call (Member (VarRef __t0) fn) (Member (VarRef __t0) env) 4))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower closure call: the closure of a call result is dropped after the call")
{
    TestCompiler cc(stage("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaCall(): i32 => adder(2)(3)
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaCall").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __ret) (Call (Member (VarRef __t0) fn) ...))
            (Drop (VarRef __t0))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower closure call: the callee of a `while` condition is evaluated on every iteration")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loopCall(): i32 {
    var total = 0
    while adder(1)(total) < 3 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper fn = cc.function("loopCall");
    REQUIRE_IR_MATCHES(fn.findBlock("WHILE0_cond"), R"(
        (Block _L_WHILE0_cond
            (Assign (VarRef __t0) (Call (GlobalRef adder) _ :owned-position))
            ...
            (If _ _L_WHILE0_body _L_WHILE0_exit))
    )");
    REQUIRE_IR_DOES_NOT_MATCH(fn.findBlock("entry"),
                              "(Block _L_entry ... (Call (GlobalRef adder) ...) ...)");
}
