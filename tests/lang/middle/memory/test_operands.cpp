/**
 * What the memory pass does with an operand that is handed over: a move takes
 * the variable out of play, a place is copied, a borrow neither moves nor
 * copies. The operands are found by the `:owned-position` bit lowering sets.
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
type Either = Node | i32
func take(n: Node) {}
func peek(n: &Node) {}
)";

CompileOptions memoryStage()
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    return options;
}

bool contains(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("Memory operands: a place passed to an owned parameter is copied, a move is not")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func args(a: Node, b: Node) {
    take(a)
    take(&&b)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("args").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __copy0 ^Node :temporary :class-in-flight :synthetic)
            (Copy (VarRef a :owned-position) (VarRef __copy0))
            (Call (GlobalRef take) (VarRef __copy0 :owned-position :moved))
            (Call (GlobalRef take) (VarRef b :owned-position :moved))
            (Goto _L_exit))
    )");
    // `a` is still owned at the end, `b` went to `take`
    REQUIRE_IR_MATCHES(cc.function("args").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef a)) (ScopeExit 0) (Return))");
}

TEST_CASE("Memory operands: `&&x` passed to a `&T` parameter borrows, `x` is still owned")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func borrowed(a: Node) {
    peek(&&a)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // no copy, and no move: the owner drops it
    REQUIRE_IR_MATCHES(cc.function("borrowed").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef a)) (ScopeExit 0) (Return))");
}

TEST_CASE("Memory operands: a moved struct field takes the variable out of play")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func structField(a: Node) {
    var h = Holder{n: &&a}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("structField").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef h)) (ScopeExit 0) (Return))");
}

TEST_CASE("Memory operands: a place in a struct field is copied")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func structCopy(a: Node) {
    var h = Holder{n: a}
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("structCopy").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl h Holder)
            (VarDecl __copy0 ^Node :temporary :class-in-flight :synthetic)
            (Copy (VarRef a :owned-position) (VarRef __copy0))
            (Assign (VarRef h) (Struct Holder (Field n (VarRef __copy0 :owned-position :moved)) :owned-position))
            (Goto _L_exit))
    )");
    REQUIRE_IR_MATCHES(cc.function("structCopy").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef h)) (Drop (VarRef a)) (ScopeExit 0) (Return))");
}

TEST_CASE("Memory operands: payloads of an optional, an array and a union")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func optional(a: Node) {
    var o: Node? = &&a
}
func arrayElem(a: Node, b: Node) {
    var arr = [&&a, b]
}
func variant(a: Node) {
    var u: Either = &&a
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // moved payloads: only the container is dropped
    REQUIRE_IR_MATCHES(cc.function("optional").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef o)) (ScopeExit 0) (Return))");
    REQUIRE_IR_MATCHES(cc.function("variant").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef u)) (ScopeExit 0) (Return))");
    // `a` moved into the array, `b` copied into it, so `b` is still owned
    REQUIRE_IR_MATCHES(cc.function("arrayElem").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef arr)) (Drop (VarRef b)) (ScopeExit 0) (Return))");
}

TEST_CASE("Memory operands: a move that is the whole right side")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func assignMove(a: Node) {
    var b = &&a
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("assignMove").findBlock("exit"),
                       "(Block _L_exit (Drop (VarRef b)) (ScopeExit 0) (Return))");
}

TEST_CASE("Memory operands: moving a field out of an object empties it first")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func memberMove(h: Holder) {
    take(&&h.n)
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("memberMove").findBlock("entry"), R"(
        (Block _L_entry
            (ScopeEnter 0)
            (VarDecl __mv0 ^Node :temporary :class-in-flight :synthetic)
            (Assign (VarRef __mv0) (Member (VarRef h) n ...) :unmanaged)
            (Assign (Member (VarRef h) n) null :unmanaged)
            (Call (GlobalRef take) (VarRef __mv0 :owned-position :moved))
            (Goto _L_exit))
    )");
}

TEST_CASE("Memory operands: a move of a value that owns nothing is stripped too")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(R"(
func use(n: i32): i32 => n
func scalar(x: i32): i32 => use(&&x)
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // no `Move` node is left for the backend
    CHECK_FALSE(contains(cc.function("scalar").sexpr(), "(Move"));
}
