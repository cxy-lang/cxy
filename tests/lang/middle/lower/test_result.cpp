/**
 * Lowering of the `!T` protocol: `raise` and `return` write the tag and the
 * payload of the result slot, `catch` branches on the tag.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
exception TestError(msg: string) => msg
func mayFail(x: i32): !i32 {
    if (x < 0) {
        raise TestError("negative")
    }
    return x * 2
}
)";

CompileOptions withBuiltins()
{
    CompileOptions options;
    options.withBuiltins = true;
    return options;
}

} // namespace

TEST_CASE("Lower result: `raise` sets the tag, stores the exception and unwinds")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(std::string(program) + "func main(): void {}"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("mayFail").findBlock("IF0_then"), R"(
        (Block _L_IF0_then
            (ScopeEnter 1)
            (VarDecl __t0 ^TestError :temporary)
            (Assign (VarRef __t0) (Call (GlobalRef TestError_op__initCover) _ :owned-position))
            (Call (GlobalRef builtins_Exception_push) (VarRef __t0) ...)
            (Assign (Member (VarRef __ret) tag) 1)
            (Assign (Member (Member (VarRef __ret) val) 1) (VarRef __t0 :owned-position))
            (Goto _L_UNWIND1_return))
    )");
}

TEST_CASE("Lower result: `return x` sets the tag to 0 and stores the value")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(std::string(program) + "func main(): void {}"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("mayFail").findBlock("IF0_end"), R"(
        (Block _L_IF0_end
            (Assign (Member (VarRef __ret) tag) 0)
            (Assign (Member (Member (VarRef __ret) val) 0) (Binary * (VarRef x) 2))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower result: a bare `return` in a `!void` function sets the tag to 0")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(R"(
func voidReturn(flag: bool): !void {
    if flag { return }
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("voidReturn").findBlock("IF0_then"), R"(
        (Block _L_IF0_then
            (ScopeEnter 1)
            (Assign (Member (VarRef __ret) tag) 0)
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower result: `catch` with a default value")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(std::string(program) + R"(
func catchDefault(): i32 => mayFail(1) catch 0
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("catchDefault"), R"(
        (Function catchDefault (Ret i32)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret i32 :returned)
                (VarDecl __t0 !i32 :temporary)
                (Assign (VarRef __t0) (Call (GlobalRef mayFail) _ :owned-position))
                (VarDecl __catch_res0 i32 :temporary)
                (If (Binary == (Member (VarRef __t0) tag) 1) _L_catch_0 _L_cont_1))
            (Block _L_catch_0
                (Assign (VarRef __catch_res0) 0)
                (Drop (Member (Member (VarRef __t0) val) 1))
                (Assign (Member (VarRef __t0) tag) 0 :unmanaged)
                (Goto _L_merge_1))
            (Block _L_cont_1
                (Assign (VarRef __catch_res0) (Member (Member (VarRef __t0) val) 0) :unmanaged)
                (Assign (Member (VarRef __t0) tag) 0 :unmanaged)
                (Goto _L_merge_1))
            (Block _L_merge_1
                (Assign (VarRef __ret) (VarRef __catch_res0))
                (Goto _L_exit))
            (Block _L_exit ...)))");
}

TEST_CASE("Lower result: `catch` with a block runs the block in its own scope")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(std::string(program) + R"(
func catchBlock(): i32 {
    var r = 0
    mayFail(-1) catch {
        r = 42
    }
    return r
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // The block ends with the exception dropped and the result defused
    REQUIRE_IR_MATCHES(cc.function("catchBlock").findBlock("catch_0"), R"(
        (Block _L_catch_0
            (ScopeEnter 1)
            (Assign (VarRef r) 42)
            (ScopeExit 1)
            (Drop (Member (Member (VarRef __t0) val) 1))
            (Assign (Member (VarRef __t0) tag) 0 :unmanaged)
            (Goto _L_merge_1))
    )");
}

TEST_CASE("Lower result: calling a `!T` function checks the tag and propagates the error")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(std::string(program) + R"(
func propagate(x: i32): !i32 {
    var v = mayFail(x)
    return v
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("propagate").sexpr();
    REQUIRE_FALSE(text.empty());
    // the error goes to the caller through the result slot, with a trace push
    CHECK(text.find("(Assign (Member (VarRef __ret) tag) 1)") != std::string::npos);
    CHECK(text.find("builtins_Exception_push") != std::string::npos);
}
