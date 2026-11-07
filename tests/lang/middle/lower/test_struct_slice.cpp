/**
 * An array stored in a slice field of a struct literal is converted to a slice, the
 * way a call argument is: a slice temporary gets the pointer and the length. Without
 * it the field was built from the array itself, which is invalid C for a literal and
 * a slice of length 0 for a variable.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
struct Slices {
    items: [i32] = []
    port: i64 = 80
}
)";

CompileOptions lowered()
{
    CompileOptions options;
    options.stage = "Lower";
    return options;
}

} // namespace

TEST_CASE("Struct slice field: an array literal becomes a slice first")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func literal(): void {
    var s = Slices{items: [1`i32, 2`i32]}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    CHECK_IR_MATCHES(cc.function("literal"), R"(
        (Function literal (Ret void)
            (Block _L_entry
                ...
                (Assign (Member (VarRef _) ptr) (VarRef _))
                (Assign (Member (VarRef _) len) 2)
                (Assign (VarRef s) (Struct Slices (Field items (VarRef _)) ...))
                ...)
            ...))");
}

TEST_CASE("Struct slice field: an array variable becomes a slice first")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func variable(): void {
    var arr = [1`i32, 2`i32, 3`i32]
    var s = Slices{items: arr}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    CHECK_IR_MATCHES(cc.function("variable"), R"(
        (Function variable (Ret void)
            (Block _L_entry
                ...
                (Assign (Member (VarRef _) ptr) (VarRef arr))
                (Assign (Member (VarRef _) len) 3)
                (Assign (VarRef s) (Struct Slices (Field items (VarRef _)) ...))
                ...)
            ...))");
}

TEST_CASE("Struct slice field: what is already a slice is left as it is")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func other(x: [i32]): void {
    var s = Slices{items: x}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    CHECK_IR_MATCHES(cc.function("other"), R"(
        (Function other (Param x [i32]) (Ret void)
            (Block _L_entry
                ...
                (Assign (VarRef s) (Struct Slices (Field items (VarRef x)) ...))
                ...)
            ...))");
}

TEST_CASE("Struct slice field: other fields are lowered as before")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
struct Pair {
    a: i32
    b: i64
}
func plain(): void {
    var p = Pair{a: 1, b: 2}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    CHECK_IR_MATCHES(cc.function("plain"), R"(
        (Function plain (Ret void)
            (Block _L_entry
                ...
                (Assign (VarRef p) (Struct Pair (Field a 1) (Field b _)))
                ...)
            ...))");
}
