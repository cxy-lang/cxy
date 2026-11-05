/**
 * A member of an rvalue (`make().id`): lowering gives the object a temporary,
 * evaluated in order, and the memory pass drops it. A `while` condition is
 * evaluated inside the loop, on every iteration.
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
var counter = 0
func make(): Node => Node(1)
func next(): Node {
    counter = counter + 1
    return Node(counter)
}
)";

CompileOptions stage(const char *name)
{
    CompileOptions options;
    options.stage = name;
    return options;
}

} // namespace

TEST_CASE("Lower member: the object of `make().id` is a temporary")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func first(): i32 => make().id
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("first"), R"(
        (Function first (Ret i32)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret i32 :returned)
                (VarDecl __t0 ^Node :synthetic)
                (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
                (Assign (VarRef __ret) (Member (VarRef __t0) id))
                (Goto _L_exit))
            (Block _L_exit ...)))");
}

TEST_CASE("Lower member: two rvalues in one expression are evaluated left to right")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func both(): i32 => next().id + make().id
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("both").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl __t0 ^Node :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef next) :owned-position))
            (VarDecl __t1 ^Node :synthetic)
            (Assign (VarRef __t1) (Call (GlobalRef make) :owned-position))
            (Assign (VarRef __ret)
                (Binary + (Member (VarRef __t0) id) (Member (VarRef __t1) id)))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower member: the temporary is dropped at the end of the function")
{
    TestCompiler cc(stage("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func first(): i32 => make().id
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("first").findBlock("exit"), R"(
        (Block _L_exit
            (Drop (VarRef __t0))
            (ScopeExit 0)
            (Return (VarRef __ret)))
    )");
}

TEST_CASE("Lower member: a `while` condition calls the function on every iteration")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total = 0
    while next().id < 4 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper fn = cc.function("loop");
    REQUIRE_IR_MATCHES(fn.findBlock("WHILE0_cond"), R"(
        (Block _L_WHILE0_cond
            (Assign (VarRef __t0) (Call (GlobalRef next) :owned-position))
            (If (Binary < (Member (VarRef __t0) id) 4) _L_WHILE0_body _L_WHILE0_exit))
    )");
    // nothing of the condition runs before the loop
    REQUIRE_IR_DOES_NOT_MATCH(fn.findBlock("entry"),
                              "(Block _L_entry ... (Call (GlobalRef next)) ...)");
}

TEST_CASE("Lower member: the temporary of a `while` condition is dropped on every iteration")
{
    TestCompiler cc(stage("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total = 0
    while next().id < 4 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // the previous iteration's object goes before the next call, and the
    // last one when the loop ends
    REQUIRE_IR_MATCHES(cc.function("loop").findBlock("WHILE0_cond"), R"(
        (Block _L_WHILE0_cond
            (DropIf (VarRef __t0) _)
            (Assign (VarRef __t0) (Call (GlobalRef next) :owned-position))
            ...)
    )");
}
