/**
 * Lowering turns an integer, float, character or pointer condition into an
 * explicit comparison with zero. A bool, an optional and a closure already are
 * conditions and are not touched.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

CompileOptions lowered()
{
    CompileOptions options;
    options.stage = "Lower";
    return options;
}

} // namespace

TEST_CASE("IR conditions: `if` compares a non-bool value with zero")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
func ints(n: i32): i32 {
    if n { return 1 }
    return 0
}
func floats(f: f64): i32 {
    if f { return 1 }
    return 0
}
func chars(c: char): i32 {
    if c { return 1 }
    return 0
}
func ptrs(p: ^i32): i32 {
    if p { return 1 }
    return 0
}
func bools(b: bool): i32 {
    if b { return 1 }
    return 0
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("ints").findBlock("entry"),
                       "(Block _L_entry ... (If (Binary != (VarRef n) 0) _ _) ...)");
    REQUIRE_IR_MATCHES(cc.function("floats").findBlock("entry"),
                       "(Block _L_entry ... (If (Binary != (VarRef f) 0) _ _) ...)");
    REQUIRE_IR_MATCHES(cc.function("chars").findBlock("entry"),
                       "(Block _L_entry ... (If (Binary != (VarRef c) (Char 0)) _ _) ...)");
    REQUIRE_IR_MATCHES(cc.function("ptrs").findBlock("entry"),
                       "(Block _L_entry ... (If (Binary != (VarRef p) null) _ _) ...)");
    REQUIRE_IR_MATCHES(cc.function("bools").findBlock("entry"),
                       "(Block _L_entry ... (If (VarRef b) _ _) ...)");
}

TEST_CASE("IR conditions: `while` compares once, not twice")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
func loops(n: i32): i32 {
    while n { n = n - 1 }
    return n
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("loops").findBlock("WHILE0_cond"),
                       "(Block _L_WHILE0_cond (If (Binary != (VarRef n) 0) _ _))");
}

TEST_CASE("IR conditions: both sides of `&&` are conditions")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
func both(n: i32, p: ^i32): bool => n && p
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string text = cc.function("both").sexpr();
    CHECK(text.find("(If (Binary != (VarRef n) 0)") != std::string::npos);
    CHECK(text.find("(Binary != (VarRef p) null)") != std::string::npos);
}
