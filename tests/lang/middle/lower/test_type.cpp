/**
 * Lowering of types: what each kind of type is called in the IR.
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
struct Point {
    x: i32
    y: i32
}
enum Color { Red, Green }
type Num = i32 | f64
)";

} // namespace

TEST_CASE("Lower type: primitives, pointers, strings and arrays")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func types(a: bool, b: u8, c: i64, d: f32, e: f64, f: ^i32, g: string,
           h: [i32], i: [i32, 3]): void {}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("types"), R"(
        (Function types
            (Param a bool) (Param b u8) (Param c i64) (Param d f32) (Param e f64)
            (Param f ^i32) (Param g ^char) (Param h [i32]) (Param i "[i32, 3]")
            (Ret void) ...))");
}

TEST_CASE("Lower type: classes are handles, references are `&`, structs are values")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func types(a: Node, b: &Node, c: Point, d: Color): void {}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("types"), R"(
        (Function types (Param a ^Node) (Param b &Node) (Param c Point) (Param d Color)
            (Ret void) ...))");
}

TEST_CASE("Lower type: tuples, optionals, unions and functions")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func types(a: (i32, f64), b: i32?, c: Node?, d: Num, e: func(i32) -> i32): void {}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("types"), R"IR(
        (Function types
            (Param a "(i32, f64)") (Param b i32?) (Param c ^Node?)
            (Param d "i32 | f64") (Param e _)
            (Ret void) ...))IR");
}

TEST_CASE("Lower type: the result of a `!T` function is a result type")
{
    CompileOptions options;
    options.withBuiltins = true;
    TestCompiler cc(options);
    REQUIRE(cc.compile(R"(
func ok(): !i32 => 1
func none(): !void {}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("ok"), "(Function ok (Ret !i32) ...)");
    REQUIRE_IR_MATCHES(cc.function("none"), "(Function none (Ret !void) ...)");
}
