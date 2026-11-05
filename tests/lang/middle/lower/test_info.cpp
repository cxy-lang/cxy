/**
 * What lowering knows about ownership, as it shows in the lowered IR.
 *
 * `:owned-position` is on an operand that a consumer takes: an argument for a
 * non-reference parameter, an assignment source, an element of a literal, a
 * payload. `:user-written` is on a `&&`, `@copy` or `&` from the source.
 * `:glue` is on a function the compiler generated (copy and destructor).
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
struct Holder { n: Node }
func take(n: Node) {}
func peek(n: &Node) {}
func take2(x: i32): i32 => x
)";

CompileOptions lowered()
{
    CompileOptions options;
    options.stage = "Lower";
    return options;
}

} // namespace

TEST_CASE("IR info: an argument is owned only when the parameter is not a reference")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(a: Node) {
    take(a)
    peek(&a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("demo"), R"(
        (Function demo (Param a ^Node) (Ret void)
            (Block _L_entry
                ...
                (Call (GlobalRef take) (VarRef a :owned-position))
                (Call (GlobalRef peek) (Cast &Node (VarRef a) :user-written))
                ...)
            ...))");
}

TEST_CASE("IR info: moves and copies the user wrote")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(a: Node, r: &Node) {
    var b = a
    var c = &&b
    var d = @copy r
    take(&&c)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("demo"), R"(
        (Function demo (Param a ^Node) (Param r &Node) (Ret void)
            (Block _L_entry
                ...
                (Assign (VarRef b) (VarRef a :owned-position))
                (VarDecl c ^Node)
                (Assign (VarRef c) (Move (VarRef b) :owned-position :user-written))
                (VarDecl d ^Node)
                (Copy (VarRef r) (VarRef d) :user-written)
                (Call (GlobalRef take) (Move (VarRef c) :owned-position :user-written))
                ...)
            ...))");
}

TEST_CASE("IR info: a move the compiler wrote is not user written")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(a: Node): Node {
    var b = a
    return b
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    // `return b` hands the local over, and there is no `&&` in the source
    CHECK(cc.function("demo").sexpr().find(":user-written") == std::string::npos);
    REQUIRE_IR_MATCHES(cc.function("demo"), R"(
        (Function demo (Param a ^Node) (Ret ^Node)
            (Block _L_entry
                ...
                (Assign (VarRef __ret) (VarRef b :owned-position))
                ...)
            ...))");
}

TEST_CASE("IR info: elements of literals and payloads are owned")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(a: Node, d: Node) {
    var h = Holder{n: a}
    var arr = [a, d]
    var o: Node? = a
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("demo"), R"(
        (Function demo (Param a ^Node) (Param d ^Node) (Ret void)
            (Block _L_entry
                ...
                (Assign (VarRef h)
                    (Struct Holder (Field n (VarRef a :owned-position)) :owned-position))
                ...
                (Assign (VarRef arr)
                    (Array (VarRef a :owned-position) (VarRef d :owned-position)
                           :owned-position))
                ...
                (Assign (VarRef o) (Some (VarRef a :owned-position) :owned-position))
                ...)
            ...))");
}

TEST_CASE("IR info: a value that owns nothing carries no information")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(x: i32): i32 {
    var y = x
    var z = take2(y)
    return z
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    const std::string text = cc.function("demo").sexpr();
    REQUIRE_FALSE(text.empty());
    CHECK(text.find(":owned-position") == std::string::npos);
    CHECK(text.find(":user-written") == std::string::npos);
}

TEST_CASE("IR info: generated copy and destructor functions are glue")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func demo(a: Node) { var h = Holder{n: a}; var h2 = h }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    SUBCASE("a struct's copy and destructor") {
        REQUIRE_IR_MATCHES(cc.function("main.Holder.op__copy"),
                           "(Function main.Holder.op__copy (Param this ^Holder) (Ret Holder) :glue ...)");
        REQUIRE_IR_MATCHES(cc.function("main.Holder.op__destructor"),
                           "(Function main.Holder.op__destructor (Param this ^Holder) (Ret void) :glue ...)");
    }
    SUBCASE("a class destructor, but not the `deinit` the user wrote") {
        REQUIRE_IR_MATCHES(cc.function("main.Node.op__destructor"),
                           "(Function main.Node.op__destructor (Param this &Node) (Ret void) :glue ...)");
        REQUIRE_IR_DOES_NOT_MATCH(cc.function("main.Node.op__deinit"),
                                  "(Function main.Node.op__deinit (Param this &Node) (Ret void) :glue ...)");
    }
    SUBCASE("a function the user wrote") {
        REQUIRE_IR_DOES_NOT_MATCH(cc.function("demo"), "(Function demo ... :glue ...)");
    }
    SUBCASE("the copies inside glue are not user written") {
        CHECK(cc.function("main.Holder.op__copy").sexpr().find(":user-written") ==
              std::string::npos);
    }
}

TEST_CASE("IR info: a temporary lowering creates for an operand is synthetic")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func make(): Node => Node(1)
func demo(): i32 {
    var kept = make()
    take(make())
    return kept.id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // the user's `kept` is not
    REQUIRE_IR_MATCHES(cc.function("demo"), R"(
        (Function demo (Ret i32)
            (Block _L_entry
                ...
                (VarDecl kept ^Node)
                ...
                (VarDecl __t0 ^Node :temporary :synthetic)
                ...)
            ...))");
}
