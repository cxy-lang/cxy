/**
 * Lowering of binary operators: `&&` and `||` short-circuit through blocks,
 * `??` picks a branch, and a closure compares by its function pointer.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

TEST_CASE("Lower binary: `a && b` evaluates `b` only when `a` is true")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func check(): bool => true
func andOp(a: bool): bool => a && check()
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("andOp"), R"(
        (Function andOp (Param a bool) (Ret bool)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret bool :returned)
                (VarDecl __cond0 bool :temporary)
                (If (VarRef a) _L_AND0_rhs _L_AND0_false))
            (Block _L_AND0_rhs
                (VarDecl __t0 bool :temporary)
                (Assign (VarRef __t0) (Call (GlobalRef check)))
                (Assign (VarRef __cond0) (VarRef __t0))
                (Goto _L_AND0_merge))
            (Block _L_AND0_false
                (Assign (VarRef __cond0) false)
                (Goto _L_AND0_merge))
            (Block _L_AND0_merge
                (Assign (VarRef __ret) (VarRef __cond0))
                (Goto _L_exit))
            (Block _L_exit ...)))");
}

TEST_CASE("Lower binary: `a || b` evaluates `b` only when `a` is false")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func check(): bool => true
func orOp(a: bool): bool => a || check()
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("orOp"), R"(
        (Function orOp (Param a bool) (Ret bool)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret bool :returned)
                (VarDecl __cond0 bool :temporary)
                (If (VarRef a) _L_OR0_true _L_OR0_rhs))
            (Block _L_OR0_true
                (Assign (VarRef __cond0) true)
                (Goto _L_OR0_merge))
            (Block _L_OR0_rhs
                (VarDecl __t0 bool :temporary)
                (Assign (VarRef __t0) (Call (GlobalRef check)))
                (Assign (VarRef __cond0) (VarRef __t0))
                (Goto _L_OR0_merge))
            (Block _L_OR0_merge
                (Assign (VarRef __ret) (VarRef __cond0))
                (Goto _L_exit))
            (Block _L_exit ...)))");
}

TEST_CASE("Lower binary: `a ?? b` takes the value when the optional is set")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func coalesce(a: i32?, b: i32): i32 => a ?? b
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("coalesce"), R"(
        (Function coalesce (Param a i32?) (Param b i32) (Ret i32)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret i32 :returned)
                (VarDecl __t0 i32 :temporary)
                (If (Member (VarRef a) ok) _L_COALESCE0_then _L_COALESCE0_else))
            (Block _L_COALESCE0_then
                (Assign (VarRef __t0) (Member (VarRef a) val))
                (Goto _L_COALESCE0_merge))
            (Block _L_COALESCE0_else
                (Assign (VarRef __t0) (VarRef b))
                (Goto _L_COALESCE0_merge))
            (Block _L_COALESCE0_merge
                (Assign (VarRef __ret) (VarRef __t0))
                (Goto _L_exit))
            (Block _L_exit ...)))");
}

TEST_CASE("Lower binary: a closure compared with null compares its function pointer")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func isNull(f: func() -> i32): bool => f == null
func notNull(f: func() -> i32): bool => f != null
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("isNull").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __ret) (Binary == (Member (VarRef f) fn) null))
            ...))");
    REQUIRE_IR_MATCHES(cc.function("notNull").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __ret) (Binary != (Member (VarRef f) fn) null))
            ...))");
}

TEST_CASE("Lower binary: other operators stay a single binary node")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func add(a: i32, b: i32): i32 => a + b
func less(a: i32, b: i32): bool => a < b
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("add").findBlock("entry"),
                       "(Block _L_entry ... (Assign (VarRef __ret) (Binary + (VarRef a) (VarRef b))) ...)");
    REQUIRE_IR_MATCHES(cc.function("less").findBlock("entry"),
                       "(Block _L_entry ... (Assign (VarRef __ret) (Binary < (VarRef a) (VarRef b))) ...)");
}
