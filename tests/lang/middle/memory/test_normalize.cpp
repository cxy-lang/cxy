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
func args(b: Node) {
    take(&&b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "args");

    REQUIRE_IR_MATCHES(cc.function("args").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
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

TEST_CASE("Normalize N2: `&&p.f` is read into a temporary and the field is emptied")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
class Box {
    n: Node
    func `init`() {}
    func `deinit`() {}
}
func fromField(b: &Box) {
    take(&&b.n)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "fromField");

    REQUIRE_IR_MATCHES(cc.function("fromField").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __mv0 ^Node :temporary)
            (Assign (VarRef __mv0) (Member (VarRef b) n :owned-position) :unmanaged)
            (Assign (Member (VarRef b) n) null :unmanaged)
            (Call (GlobalRef take) (VarRef __mv0 :owned-position :moved))
            (Goto _L_exit))
    )");
}

TEST_CASE("Normalize N3: a place passed to an owned parameter is copied into a temporary")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func owned(a: Node) {
    take(a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "owned");

    REQUIRE_IR_MATCHES(cc.function("owned").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __copy0 ^Node :temporary)
            (Copy (VarRef a :owned-position) (VarRef __copy0))
            (Call (GlobalRef take) (VarRef __copy0 :owned-position :moved))
            (Goto _L_exit))
    )");
}

TEST_CASE("Normalize N3b: `x = place` becomes a copy into `x`, with no temporary")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func store(a: Node) {
    var x = Node(1)
    x = a
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "store");

    REQUIRE_IR_MATCHES(cc.function("store").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl x _)
            (Assign (VarRef x) (Call ...))
            (Copy (VarRef a :owned-position) (VarRef x))
            (Goto _L_exit))
    )");
}

TEST_CASE("Normalize N3: a moved operand is not copied")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func moved(a: Node) {
    var x = Node(1)
    x = &&a
    take(&&x)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "moved");

    CHECK_FALSE(cc.function("moved").findBlock("entry").sexpr().find("__copy") != std::string::npos);
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
    take(Node(1))
    peek(a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    std::string before = cc.function("plain").sexpr();
    normalize(cc, "plain");
    REQUIRE(cc.function("plain").sexpr() == before);
}
