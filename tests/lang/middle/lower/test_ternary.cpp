/**
 * `c ? a : b` reaches lowering as an `if` that assigns one result variable
 * (simplify rewrites it), so only the chosen branch is evaluated.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

TEST_CASE("Lower ternary: both branches assign one result variable")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func pick(c: bool, a: i32, b: i32): i32 => c ? a : b
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("pick"), R"(
        (Function pick (Param c bool) (Param a i32) (Param b i32) (Ret i32)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret i32 :returned)
                (VarDecl _if0 i32)
                (If (VarRef c) _L_IF0_then _L_IF0_else))
            (Block _L_IF0_then
                (ScopeEnter 1)
                (Assign (VarRef _if0) (VarRef a))
                (ScopeExit 1)
                (Goto _L_IF0_end))
            (Block _L_IF0_else
                (ScopeEnter 2)
                (Assign (VarRef _if0) (VarRef b))
                (ScopeExit 2)
                (Goto _L_IF0_end))
            (Block _L_IF0_end
                (Assign (VarRef __ret) (VarRef _if0))
                (Goto _L_exit))
            (Block _L_exit ...)))");
}

TEST_CASE("Lower ternary: a call in a branch runs only when that branch is chosen")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func left(): i32 => 1
func right(): i32 => 2
func pick(c: bool): i32 => c ? left() : right()
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper fn = cc.function("pick");
    REQUIRE_IR_MATCHES(fn.findBlock("IF0_then"),
                       "(Block _L_IF0_then ... (Assign (VarRef _if0) (Call (GlobalRef left))) ...)");
    REQUIRE_IR_MATCHES(fn.findBlock("IF0_else"),
                       "(Block _L_IF0_else ... (Assign (VarRef _if0) (Call (GlobalRef right))) ...)");
    REQUIRE_IR_DOES_NOT_MATCH(fn.findBlock("entry"),
                              "(Block _L_entry ... (Call _ ...) ...)");
}
