/**
 * What `normalizeFunction` rewrites in lowered IR, before the ownership analysis
 * runs: the IR is compiled up to lowering, normalized by hand, and compared.
 */

#include "doctest.h"
#include "utils/ir.hpp"

#include "lang/middle/memory/normalize.h"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
func take(n: Node) {}
func peek(n: &Node) {}
)";

void normalize(TestCompiler &cc, const char *function)
{
    CompilerDriver *driver = cc.driver();
    IrNode *module = const_cast<IrNode *>(cc.ir().node());
    MemMgmtContext ctx = {};
    ctx.driver = driver;
    ctx.pool = driver->pool;
    ctx.strings = driver->strings;
    ctx.types = module->module.types;
    ctx.module = module;
    normalizeFunction(&ctx, const_cast<IrNode *>(cc.function(function).node()));
}

} // namespace

TEST_CASE("Normalize N1: `&&x` in a call becomes a `move x` statement and the operand is `x`")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func args(a: Node, b: Node) {
    take(a)
    take(&&b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "args");

    REQUIRE_IR_MATCHES(cc.function("args").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (Call (GlobalRef take) (VarRef a :owned-position))
            (Move (VarRef b))
            (Call (GlobalRef take) (VarRef b :owned-position :moved))
            (Goto _L_exit))
    )");
}

TEST_CASE("Normalize N1: `x = &&y` becomes a `move y` statement before the assignment")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func assign(a: Node) {
    var x = Node(1)
    x = &&a
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "assign");

    REQUIRE_IR_MATCHES(cc.function("assign").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl x _)
            (Assign (VarRef x) (Call ...))
            (Move (VarRef a))
            (Assign (VarRef x) (VarRef a :owned-position :moved))
            (Goto _L_exit))
    )");
}

TEST_CASE("Normalize N4: `&&x` passed to a `&T` parameter is a borrow, nothing is moved")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func borrowed(a: Node) {
    peek(&&a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "borrowed");

    REQUIRE_IR_MATCHES(cc.function("borrowed").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (Call (GlobalRef peek) (Cast &Node (Cast &Node (VarRef a) :user-written)))
            (Goto _L_exit))
    )");
}

TEST_CASE("Normalize: a function with no moves is unchanged")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func plain(a: Node) {
    take(a)
    peek(a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    std::string before = cc.function("plain").sexpr();
    normalize(cc, "plain");
    REQUIRE(cc.function("plain").sexpr() == before);
}
