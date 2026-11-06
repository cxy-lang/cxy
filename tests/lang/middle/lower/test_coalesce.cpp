/**
 * `a ?? b` evaluates `a` once. A left side that is not a place (a call, the payload
 * of a `!T` result) is stored in a temporary first: the condition and the value of
 * the `then` path read that temporary, not the result's payload twice.
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
func some(): !Node? => Node(1)
)";

CompileOptions lowered()
{
    CompileOptions options;
    options.stage = "Lower";
    options.withBuiltins = true; // `!T` needs the exception type
    return options;
}

} // namespace

TEST_CASE("Coalesce: a call on the left is evaluated once into a temporary")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func orNull(): !void {
    var y: Node? = some() ?? null
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // the payload is taken by one assignment, the condition reads what it took
    CHECK_IR_MATCHES(cc.function("orNull"), R"(
        (Function orNull (Ret !void)
            (Block _L_entry ...)
            (Block _L_call_err_0 ...)
            (Block _L_call_cont_1
                ...
                (Assign (VarRef _) (Member (Member (VarRef __call_res_0) val) 0 ...))
                ...
                (If (Member (VarRef _) ok) _L_COALESCE0_then _L_COALESCE0_else))
            ...))");
}

TEST_CASE("Coalesce: the payload of the result is read once")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func orNull(): !void {
    var y: Node? = some() ?? null
}
func orValue(): !Node {
    return some() ?? Node(2)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    for (const char *name : {"orNull", "orValue"}) {
        INFO(name);
        const std::string ir = cc.function(name).sexpr();
        size_t reads = 0;
        for (size_t at = ir.find("(Member (VarRef __call_res_0) val) 0"); at != std::string::npos;
             at = ir.find("(Member (VarRef __call_res_0) val) 0", at + 1))
            reads++;
        CHECK(reads == 1);
    }
}

TEST_CASE("Coalesce: a variable on the left is read in place, as before")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func orValue(y: Node?): Node {
    return y ?? Node(2)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    CHECK_IR_MATCHES(cc.function("orValue"), R"(
        (Function orValue (Param y ^Node?) (Ret ^Node)
            (Block _L_entry
                ...
                (If (Member (VarRef y) ok) _L_COALESCE0_then _L_COALESCE0_else))
            (Block _L_COALESCE0_then
                ...
                (Assign (VarRef _) (Member (VarRef y) val ...))
                ...)
            ...))");
}

TEST_CASE("Coalesce: an optional left side with a null right side is an optional")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func orNull(y: Node?): Node? {
    return y ?? null
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // the value of the `then` path is the optional itself, not its payload
    CHECK_IR_MATCHES(cc.function("orNull"), R"(
        (Function orNull (Param y ^Node?) (Ret ^Node?)
            (Block _L_entry ...)
            (Block _L_COALESCE0_then
                ...
                (Assign (VarRef _) (VarRef y ...))
                ...)
            ...))");
}
