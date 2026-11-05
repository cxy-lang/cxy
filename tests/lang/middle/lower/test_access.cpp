/**
 * Lowering of member and element access.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

TEST_CASE("Lower access: index of an array, a slice and a pointer")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func plain(a: [i32, 3], i: i32): i32 => a.[i]
func sliceIdx(s: [i32], i: i32): i32 => s.[i]
func ptrIdx(p: ^i32, i: i32): i32 => p.[i]
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("plain").findBlock("entry"),
                       "(Block _L_entry ... (Assign (VarRef __ret) (Index (VarRef a) (VarRef i))) ...)");
    // a slice is indexed through its pointer
    REQUIRE_IR_MATCHES(cc.function("sliceIdx").findBlock("entry"),
                       "(Block _L_entry ... (Assign (VarRef __ret) (Index (Member (VarRef s) ptr) (VarRef i))) ...)");
    REQUIRE_IR_MATCHES(cc.function("ptrIdx").findBlock("entry"),
                       "(Block _L_entry ... (Assign (VarRef __ret) (Index (VarRef p) (VarRef i))) ...)");
}

TEST_CASE("Lower access: a pure index expression stays in the index")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func exprIdx(a: [i32, 3], i: i32): i32 => a.[i + 1]
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("exprIdx").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (Assign (VarRef __ret) (Index (VarRef a) (Binary + (VarRef i) 1)))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower access: a call as the index is evaluated once, before the access")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func pick(): i32 => 1
func callIdx(a: [i32, 3]): i32 => a.[pick()]
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("callIdx").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl __t0 i32 :temporary)
            (Assign (VarRef __t0) (Call (GlobalRef pick)))
            (Assign (VarRef __ret) (Index (VarRef a) (VarRef __t0)))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower access: the index of a `while` condition is evaluated on every iteration")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func pick(): i32 => 1
func loopIdx(a: [i32, 3]): i32 {
    var total = 0
    while a.[pick()] < 3 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper fn = cc.function("loopIdx");
    REQUIRE_IR_MATCHES(fn.findBlock("WHILE0_cond"), R"(
        (Block _L_WHILE0_cond
            (Assign (VarRef __t0) (Call (GlobalRef pick)))
            (If (Binary < (Index (VarRef a) (VarRef __t0)) 3) _L_WHILE0_body _L_WHILE0_exit))
    )");
    REQUIRE_IR_DOES_NOT_MATCH(fn.findBlock("entry"),
                              "(Block _L_entry ... (Call (GlobalRef pick)) ...)");
}
