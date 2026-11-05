/**
 * Borrowing an rvalue: `peek(make())` and `var r = &make()`. The object lives
 * in a local that is dropped with its scope, a place is borrowed as it is.
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
func peek(n: &Node): i32 => n.id
func same(n: &Node): &Node => n
)";

CompileOptions stage(const char *name)
{
    CompileOptions options;
    options.stage = name;
    return options;
}

} // namespace

TEST_CASE("Lower reference: an rvalue passed to a `&T` parameter lives in a local")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaCall(): i32 {
    return peek(make())
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // a plain local, not the unowned `:temporary` kind
    REQUIRE_IR_MATCHES(cc.function("viaCall").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl __t0 ^Node :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (Assign (VarRef __ret) (Call (GlobalRef peek) (Cast &Node (Cast &Node (VarRef __t0)))))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower reference: the local of a borrowed rvalue is dropped after the call")
{
    TestCompiler cc(stage("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaCall(): i32 {
    return peek(make())
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaCall").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef __ret) (Call (GlobalRef peek) ...))
            (Drop (VarRef __t0) :synthetic)
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower reference: `var r = &make()` keeps the object alive for the scope")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaRef(): i32 {
    var r = &make()
    return r.id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaRef").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (VarDecl r &Node)
            (VarDecl __t0 ^Node :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (Assign (VarRef r) (Cast &Node (VarRef __t0) :user-written))
            (Assign (VarRef __ret) (Member (VarRef r) id))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower reference: the object behind `var r = &make()` is dropped")
{
    TestCompiler cc(stage("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaRef(): i32 {
    var r = &make()
    return r.id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaRef").findBlock("exit"), R"(
        (Block _L_exit
            (Drop (VarRef __t0))
            (ScopeExit 0)
            (Return (VarRef __ret)))
    )");
}

TEST_CASE("Lower reference: a place is borrowed as it is")
{
    TestCompiler cc(stage("Lower"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaPlace(a: Node): i32 {
    return peek(&a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("viaPlace").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __ret i32 :returned)
            (Assign (VarRef __ret) (Call (GlobalRef peek) (Cast &Node (VarRef a) :user-written)))
            (Goto _L_exit))
    )");
}

TEST_CASE("Lower reference: a call that returns a reference owns nothing to drop")
{
    TestCompiler cc(stage("MemoryMgmt"));
    REQUIRE(cc.compile(std::string(program) + R"(
func viaSame(a: Node): i32 {
    return peek(same(&a))
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // only the parameter `a` is dropped at the end
    const std::string text = cc.function("viaSame").sexpr();
    REQUIRE_FALSE(text.empty());
    size_t drops = 0;
    for (size_t at = text.find("(Drop"); at != std::string::npos; at = text.find("(Drop", at + 1))
        drops++;
    CHECK(drops == 1);
}
