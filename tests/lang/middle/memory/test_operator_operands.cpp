/**
 * An rvalue that is an operand of an overloaded operator (`!!make()`, `~make()`,
 * `a == make()` when `==` has several overloads) is not hoisted by sema into a
 * variable ahead of the statement. Lowering evaluates it where it is written, so it
 * runs only when the code around it does (the right side of `&&`), and the memory
 * pass drops it with its statement.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
class Flag {
    on: bool
    func `init`(on: bool) { this.on = on }
    func `deinit`() {}
    func `!!`() => on
    func `~`() => !on
}
class Name {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
    func `==`(other: &Name): bool => id == other.id
    func `==`(other: i32): bool => id == other
}
func make(): Flag => Flag(true)
func makeName(): Name => Name(1)
)";

CompileOptions memoryStage()
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    return options;
}

bool mentions(const IrNodeWrapper &node, const char *text)
{
    return node.sexpr().find(text) != std::string::npos;
}

} // namespace

TEST_CASE("Operator operands: no variable is made ahead of the statement")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func gated(x: bool): bool { return x && !!make() }
func inverted(): bool {
    var c = ~make()
    return c
}
func compared(a: &Name, x: bool): bool { return x && a == makeName() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    for (const char *name : {"gated", "inverted", "compared"}) {
        INFO(name);
        CHECK_FALSE(mentions(cc.function(name), "_rv"));
    }
}

TEST_CASE("Operator operands: the right side of && runs only when the left side lets it")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func gated(x: bool): bool { return x && !!make() }
func compared(a: &Name, x: bool): bool { return x && a == makeName() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper gated = cc.function("gated");
    REQUIRE_IR_MATCHES(gated.findBlock("AND0_rhs"),
                       "(Block _L_AND0_rhs ... (Assign (VarRef __t0) (Call (GlobalRef make) "
                       ":owned-position)) ...)");
    REQUIRE_IR_DOES_NOT_MATCH(
        gated.findBlock("entry"),
        "(Block _L_entry ... (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position)) ...)");

    IrNodeWrapper compared = cc.function("compared");
    REQUIRE_IR_MATCHES(compared.findBlock("AND0_rhs"),
                       "(Block _L_AND0_rhs ... (Assign (VarRef __t0) (Call (GlobalRef makeName) "
                       ":owned-position)) ...)");
    REQUIRE_IR_DOES_NOT_MATCH(
        compared.findBlock("entry"),
        "(Block _L_entry ... (Assign (VarRef __t0) (Call (GlobalRef makeName) :owned-position)) ...)");
}

TEST_CASE("Operator operands: the object an operand made is dropped, only where it was made")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func gated(x: bool): bool { return x && !!make() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper gated = cc.function("gated");
    REQUIRE_IR_MATCHES(gated.findBlock("AND0_rhs"), R"(
        (Block _L_AND0_rhs
            (VarDecl __t0 ^Flag :class-statement :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (VarDecl __t1 bool :synthetic)
            (Assign (VarRef __t1) (Call (GlobalRef Flag_op__truthy) (Cast &Flag (VarRef __t0))))
            (Drop (VarRef __t0) :synthetic)
            ...)
    )");
    // the path that skips the right side has nothing to drop
    REQUIRE_IR_DOES_NOT_MATCH(gated.findBlock("AND0_false"),
                              "(Block _L_AND0_false ... (Drop ...) ...)");
}

TEST_CASE("Operator operands: a unary operator drops its operand after the call")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func inverted(): bool {
    var c = ~make()
    return c
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("inverted").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret bool :class-return-slot :synthetic)
            (VarDecl c bool)
            (VarDecl __t0 ^Flag :class-statement :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (Assign (VarRef c) (Call (GlobalRef Flag_op__bnot) (Cast &Flag (VarRef __t0))))
            (Drop (VarRef __t0) :synthetic)
            ...)
    )");
}

TEST_CASE("Operator operands: an overloaded == with an rvalue argument drops it")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func compared(a: &Name): bool => a == makeName()
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("compared").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (VarDecl __t0 ^Name :class-statement :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef makeName) :owned-position))
            ...
            (Drop (VarRef __t0) :synthetic)
            ...)
    )");
}
