/**
 * A temporary that is only read, or that nobody uses, lives until the end of the
 * statement that made it. The drop comes right after the last statement that
 * reads it, a branch condition is evaluated into a value first. A temporary a
 * reference is bound to lives as long as the reference.
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
func make(): Node => Node(1)
var counter = 0
func next(): Node {
    counter = counter + 1
    return Node(counter)
}
func peek(n: &Node): i32 => n.id
)";

CompileOptions memoryStage()
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    return options;
}

} // namespace

TEST_CASE("Statement temporaries: a read-only temporary is dropped right after the statement")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func readOnly(): i32 => make().id
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("readOnly"), R"(
        (Function readOnly (Ret i32)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret i32 :returned)
                (VarDecl __t0 ^Node :ephemeral :class-statement :synthetic)
                (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
                (Assign (VarRef __ret) (Member (VarRef __t0) id))
                (Drop (VarRef __t0) :synthetic)
                (Goto _L_exit))
            (Block _L_exit
                (ScopeExit 0)
                (Return (VarRef __ret))))
    )");
}

TEST_CASE("Statement temporaries: an argument for a `&T` parameter is dropped after the call")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func borrowed(): i32 => peek(make())
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("borrowed").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __ret) (Call (GlobalRef peek) ...))
            (Drop (VarRef __t0) :synthetic)
            (Goto _L_exit))
    )");
    REQUIRE_IR_MATCHES(cc.function("borrowed").findBlock("exit"),
                       "(Block _L_exit (ScopeExit 0) (Return (VarRef __ret)))");
}

TEST_CASE("Statement temporaries: a branch condition is evaluated before the temporary is dropped")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func inCondition(): i32 {
    if make().id > 0 {
        return 1
    }
    return 0
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("inCondition").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl __t0 ^Node :ephemeral :class-statement :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (VarDecl __v0 bool :synthetic)
            (Assign (VarRef __v0) (Binary > (Member (VarRef __t0) id) 0) :synthetic)
            (Drop (VarRef __t0) :synthetic)
            (If (VarRef __v0) _L_IF0_then _L_IF0_end))
    )");
    REQUIRE_IR_MATCHES(cc.function("inCondition").findBlock("exit"),
                       "(Block _L_exit (ScopeExit 0) (Return (VarRef __ret)))");
}

TEST_CASE("Statement temporaries: a `while` condition drops its temporary on every iteration")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func loop(): i32 {
    var total = 0
    while next().id < 3 {
        total = total + 1
    }
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("loop").findBlock("WHILE0_cond"), R"(
        (Block _L_WHILE0_cond
            (Assign (VarRef __t0) (Call (GlobalRef next) :owned-position))
            (VarDecl __v0 bool :synthetic)
            (Assign (VarRef __v0) (Binary < (Member (VarRef __t0) id) 3) :synthetic)
            (Drop (VarRef __t0) :synthetic)
            (If (VarRef __v0) _L_WHILE0_body _))
    )");
}

TEST_CASE("Statement temporaries: two temporaries of one statement are dropped in reverse order")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func two(): i32 => make().id + next().id
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("two").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __ret) (Binary + ...))
            (Drop (VarRef __t1) :synthetic)
            (Drop (VarRef __t0) :synthetic)
            (Goto _L_exit))
    )");
}

TEST_CASE("Statement temporaries: a temporary a reference is bound to lives to the end of the scope")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func extended(): i32 {
    var r = &make()
    return r.id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("extended").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef __t0)) (ScopeExit 0) (Return (VarRef __ret)))");
}
