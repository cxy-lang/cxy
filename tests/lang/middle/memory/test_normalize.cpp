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

// The class and the flags normalize writes on a temporary are not a rewrite of the
// code, the statements are what a rewrite changes
std::string withoutTemporaryMarkers(std::string text)
{
    for (const char *marker : {" :class-in-flight", " :class-statement", " :class-extended",
                               " :class-result", " :temporary", " :ephemeral"}) {
        for (size_t at; (at = text.find(marker)) != std::string::npos;)
            text.erase(at, strlen(marker));
    }
    return text;
}

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
            (Move (VarRef b) :user-written :synthetic)
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
            (Move (VarRef a) :user-written :synthetic)
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
            (VarDecl __mv0 ^Node :temporary :class-in-flight :synthetic)
            (Assign (VarRef __mv0) (Member (VarRef b) n :owned-position) :unmanaged :implicit :synthetic)
            (Assign (Member (VarRef b) n) null :unmanaged :synthetic)
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
            (VarDecl __copy0 ^Node :temporary :class-in-flight :synthetic)
            (Copy (VarRef a :owned-position) (VarRef __copy0) :synthetic)
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

TEST_CASE("Normalize N3: a temporary lowering made for an owned argument is not copied")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
func make(): Node => Node(1)
func fresh() {
    take(make())
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "fresh");

    REQUIRE_IR_MATCHES(cc.function("fresh").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __t0 ^Node :temporary :class-in-flight :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (Call (GlobalRef take) (VarRef __t0 :owned-position))
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
            (Call (GlobalRef peek) (Cast &Node (Cast &Node (VarRef a :user-written) :user-written)))
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
    REQUIRE(withoutTemporaryMarkers(cc.function("plain").sexpr()) == withoutTemporaryMarkers(before));
}

TEST_CASE("Normalize N5: lowering already names a call or literal nested in an owned operand")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(program) + R"(
struct Holder { n: Node }
func make(): Node => Node(1)
func pass(n: Node): Node => n
func nested() {
    take(pass(make()))
}
func literal() {
    var h = Holder{n: pass(make())}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // every owned operand is a variable, so there is nothing for normalize to name
    REQUIRE_IR_MATCHES(cc.function("nested").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __t0 ^Node :synthetic)
            (Assign (VarRef __t0) (Call (GlobalRef make) :owned-position))
            (VarDecl __t1 ^Node :synthetic)
            (Assign (VarRef __t1) (Call (GlobalRef pass) (VarRef __t0 :owned-position) :owned-position))
            (Call (GlobalRef take) (VarRef __t1 :owned-position))
            (Goto _L_exit))
    )");
    REQUIRE_IR_MATCHES(cc.function("literal").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (Assign (VarRef h) (Struct Holder (Field n (VarRef __t1 :owned-position)) :owned-position))
            ...)
    )");

    std::string before = cc.function("nested").sexpr() + cc.function("literal").sexpr();
    normalize(cc, "nested");
    normalize(cc, "literal");
    REQUIRE(withoutTemporaryMarkers(cc.function("nested").sexpr() + cc.function("literal").sexpr()) ==
            withoutTemporaryMarkers(before));
}

namespace {

const char *classProgram = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
func make(): Node => Node(1)
func take(n: Node) {}
func peek(n: &Node): i32 => n.id
func inFlight() { take(make()) }
func readOnly(): i32 => make().id
func borrowedArg(): i32 => peek(make())
func discarded() { make() }
func inCondition(): i32 {
    if make().id > 0 { return 1 }
    return 0
}
func extended(): i32 {
    var r = &make()
    return r.id
}
func main(): void {}
)";

// The class of the first temporary of a function, as the dump prints it
std::string tempDecl(TestCompiler &cc, const char *function)
{
    normalize(cc, function);
    std::string text = cc.function(function).findBlock("entry").sexpr();
    size_t at = text.find("(VarDecl __t0");
    return at == std::string::npos ? "" : text.substr(at, text.find(')', at) - at + 1);
}

} // namespace

TEST_CASE("Normalize classes: a temporary with one owned use is in flight")
{
    TestCompiler cc;
    REQUIRE(cc.compile(classProgram));
    REQUIRE(tempDecl(cc, "inFlight") == "(VarDecl __t0 ^Node :temporary :class-in-flight :synthetic)");
}

TEST_CASE("Normalize classes: a temporary that is only read is a statement temporary")
{
    TestCompiler cc;
    REQUIRE(cc.compile(classProgram));
    CHECK(tempDecl(cc, "readOnly") == "(VarDecl __t0 ^Node :ephemeral :class-statement :synthetic)");
    CHECK(tempDecl(cc, "borrowedArg") == "(VarDecl __t0 ^Node :ephemeral :class-statement :synthetic)");
    CHECK(tempDecl(cc, "inCondition") == "(VarDecl __t0 ^Node :ephemeral :class-statement :synthetic)");
}

TEST_CASE("Normalize classes: a discarded value has no use, it is a statement temporary")
{
    TestCompiler cc;
    REQUIRE(cc.compile(classProgram));
    REQUIRE(tempDecl(cc, "discarded") == "(VarDecl __t0 ^Node :ephemeral :class-statement :synthetic)");
}

TEST_CASE("Normalize classes: a temporary a reference is bound to is extended")
{
    TestCompiler cc;
    REQUIRE(cc.compile(classProgram));
    REQUIRE(tempDecl(cc, "extended") == "(VarDecl __t0 ^Node :class-extended :synthetic)");
}

TEST_CASE("Normalize classes: a temporary read before its one owned use is in flight")
{
    CompileOptions options;
    options.withBuiltins = true;
    TestCompiler cc(options);
    REQUIRE(cc.compile(R"(
exception TestError(msg: string) => msg
func fails(x: i32): !i32 {
    if (x < 0) {
        raise TestError("negative")
    }
    return x
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "fails");

    // `push` reads the exception, the result slot takes it
    REQUIRE_IR_MATCHES(cc.function("fails").findBlock("IF0_then"), R"(
        (Block _L_IF0_then
            (ScopeEnter 1)
            (VarDecl __t0 ^TestError :temporary :class-in-flight :synthetic)
            ...)
    )");
}

TEST_CASE("Normalize classes: the result of a `!T` call is a result temporary")
{
    CompileOptions options;
    options.withBuiltins = true;
    TestCompiler cc(options);
    REQUIRE(cc.compile(R"(
exception TestError(msg: string) => msg
func mayFail(x: i32): !i32 {
    if (x < 0) {
        raise TestError("negative")
    }
    return x
}
func catchDefault(): i32 => mayFail(1) catch 0
func main(): void {}
)"));
    INFO(cc.diagnostics());
    normalize(cc, "catchDefault");

    REQUIRE_IR_MATCHES(cc.function("catchDefault").findBlock("entry"), R"(
        (Block _L_entry
            ...
            (VarDecl __t0 !i32 :temporary :class-result :synthetic)
            ...)
    )");
}
