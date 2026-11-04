/**
 * IR S-expression dump, the test compiler and the IR matcher.
 *
 * Patterns are written first, then the compiler is made to produce them.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *nodeProgram = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
)";

CompileOptions stage(const char *name)
{
    CompileOptions options;
    options.stage = name;
    return options;
}

} // namespace

TEST_CASE("IR matcher: atoms, lists and wildcards")
{
    const std::string actual = "(Function f (Param x i32) (Ret i32) (Block a (Return 1)))";

    SUBCASE("exact") {
        CHECK(matchIr(actual, "(Function f (Param x i32) (Ret i32) (Block a (Return 1)))").matched);
    }
    SUBCASE("whitespace does not matter") {
        CHECK(matchIr(actual, R"(
            (Function f
                (Param x i32)
                (Ret i32)
                (Block a (Return 1))))").matched);
    }
    SUBCASE("_ matches one node") {
        CHECK(matchIr(actual, "(Function f _ (Ret _) (Block a _))").matched);
        CHECK_FALSE(matchIr(actual, "(Function f _ (Ret _))").matched);
    }
    SUBCASE("... matches any number of siblings") {
        CHECK(matchIr(actual, "(Function f ...)").matched);
        CHECK(matchIr(actual, "(Function f (Param x i32) ... (Block a (Return 1)))").matched);
        CHECK(matchIr(actual, "(Function f ... (Ret i32) ...)").matched);
        CHECK_FALSE(matchIr(actual, "(Function f ... (Ret i64) ...)").matched);
    }
    SUBCASE("a different atom does not match") {
        CHECK_FALSE(matchIr(actual, "(Function g ...)").matched);
    }
}

TEST_CASE("IR matcher: members of a module")
{
    const std::string module =
        "(Module m (Global g i32 1) (Function a (Ret void)) (Function b (Ret i32)))";

    CHECK(matchIr(module, "(Function b (Ret i32))").matched);
    CHECK(matchIr(module, "(Function a ...)").matched);
    CHECK(matchIr(module, "(Global g ...)").matched);
    CHECK_FALSE(matchIr(module, "(Function c ...)").matched);
    CHECK_FALSE(matchIr(module, "(Function b (Ret void))").matched);
}

TEST_CASE("Test compiler: the compiled program is kept")
{
    TestCompiler cc;
    REQUIRE(cc.compile("func add(a: i32, b: i32): i32 { return a + b }\nfunc main(): void {}"));
    INFO(cc.diagnostics());

    CHECK(cc.ast());
    REQUIRE(cc.ir());
    CHECK(cc.ir().tag() == irModule);

    IrNodeWrapper add = cc.ir().findFunction("add");
    REQUIRE(add);
    CHECK(add.tag() == irFunction);
    CHECK(add.sexpr() == cc.function("add").sexpr());
    CHECK_FALSE(cc.ir().findFunction("missing"));
    CHECK_FALSE(cc.function("missing"));
}

TEST_CASE("IR wrapper: a lookup in an empty wrapper is empty")
{
    TestCompiler cc;
    REQUIRE(cc.compile("func main(): void {}"));
    IrNodeWrapper missing = cc.function("missing");
    CHECK_FALSE(missing);
    CHECK_FALSE(missing.findBlock("entry"));
    CHECK_FALSE(missing.findVariable("x"));
    CHECK(missing.sexpr().empty());
    CHECK_FALSE(matchIr(missing, "(Function missing ...)").matched);
}

TEST_CASE("IR wrapper: variables, blocks and parameters of a function")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(nodeProgram) + R"(
func make(): Node => Node(1)
func demo(flag: bool): i32 {
    var a = Node(1)
    if flag {
        var b = make()
    }
    return make().id + make().id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper demo = cc.function("demo");
    REQUIRE(demo);

    SUBCASE("a variable by name") {
        REQUIRE_IR_MATCHES(demo.findVariable("a"), "(VarDecl a ^Node)");
        CHECK_FALSE(demo.findVariable("nothing"));
    }
    SUBCASE("a variable by prefix and index") {
        // the two hoisted member temporaries, in declaration order
        REQUIRE_IR_MATCHES(demo.findVariable("_m", 0), "(VarDecl _m0 ^Node)");
        REQUIRE_IR_MATCHES(demo.findVariable("_m", 1), "(VarDecl _m1 ^Node)");
        CHECK_FALSE(demo.findVariable("_m", 2));
    }
    SUBCASE("a variable in a nested block") {
        REQUIRE_IR_MATCHES(demo.findVariable("b"), "(VarDecl b ^Node)");
    }
    SUBCASE("a block, with or without the generated prefix") {
        CHECK(demo.findBlock("_L_entry"));
        CHECK(demo.findBlock("entry"));
        CHECK(demo.findBlock("IF0_then"));
        CHECK_FALSE(demo.findBlock("nothing"));
        REQUIRE_IR_MATCHES(demo.findBlock("IF0_then"), "(Block _L_IF0_then ...)");
    }
    SUBCASE("a variable is searched in one block only when asked from a block") {
        CHECK_FALSE(demo.findBlock("entry").findVariable("b"));
        CHECK(demo.findBlock("IF0_then").findVariable("b"));
    }
    SUBCASE("a parameter") {
        REQUIRE_IR_MATCHES(demo.findParam("flag"), "(Param flag bool)");
    }
    SUBCASE("a node dumped on its own has the names of its function") {
        const std::string whole = demo.sexpr();
        // the second temporary is `_m1` in the whole function and on its own
        CHECK(whole.find(demo.findVariable("_m", 1).sexpr()) != std::string::npos);
    }
}

TEST_CASE("IR wrapper: globals of a module")
{
    TestCompiler cc;
    REQUIRE(cc.compile("var counter: i32 = 7\nfunc main(): void {}"));
    INFO(cc.diagnostics());
    REQUIRE(cc.ir().findGlobal("counter"));
    REQUIRE_IR_MATCHES(cc.ir().findGlobal("counter"), "(Global counter i32 7)");
    CHECK_FALSE(cc.ir().findGlobal("missing"));
}

TEST_CASE("AST wrapper: declarations of a compiled program")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(nodeProgram) + R"(
func demo(): i32 {
    var total = 3
    return total
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    AstNodeWrapper demo = cc.ast().findFunction("demo");
    REQUIRE(demo);
    CHECK(demo.tag() == astFuncDecl);
    CHECK_FALSE(cc.ast().findFunction("missing"));
    CHECK(cc.ast().findClass("Node"));

    AstNodeWrapper total = demo.findVariable("total");
    REQUIRE(total);
    CHECK(total.tag() == astVarDecl);
    CHECK_FALSE(demo.findVariable("nothing"));
    // `total` is below `demo`, `demo` is not below `total`
    CHECK_FALSE(total.findFunction("demo"));
    CHECK(total.sexpr().find("total") != std::string::npos);
    CHECK(demo.sexpr().find("total") != std::string::npos);
}

TEST_CASE("Test compiler: errors are reported, not thrown")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile("func main(): void { var x: i32 = \"text\" }"));
    CHECK_FALSE(cc.diagnostics().empty());
}

TEST_CASE("Test compiler: only one can exist at a time")
{
    TestCompiler first;
    CHECK_THROWS(TestCompiler{});
}

TEST_CASE("IR dump: a plain function")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func add(a: i32, b: i32): i32 {
    return a + b
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    REQUIRE_IR_MATCHES(cc.function("add"), R"(
        (Function add (Param a i32) (Param b i32) (Ret i32)
            (Block _L_entry
                (ScopeEnter 0)
                (VarDecl __ret i32 :returned)
                (Assign (VarRef __ret) (Binary + (VarRef a) (VarRef b)))
                (Goto _L_exit))
            (Block _L_exit
                (ScopeExit 0)
                (Return (VarRef __ret)))))");
}

TEST_CASE("IR dump: names generated by the compiler are numbered per function")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(nodeProgram) + R"(
func make(): Node => Node(1)
func first(): i32 => make().id
func second(): i32 => make().id
func main(): void {}
)"));
    INFO(cc.diagnostics());

    // Both functions hoist the same temporary, whatever the global counter says
    const std::string first = cc.function("first").sexpr();
    const std::string second = cc.function("second").sexpr();
    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(second.empty());
    CHECK(first.find("_m0") != std::string::npos);
    CHECK(second.find("_m0") != std::string::npos);
    CHECK(first.find("_m1") == std::string::npos);
}

TEST_CASE("IR dump: before and after the memory pass")
{
    const std::string source = std::string(nodeProgram) + R"(
func take(n: Node): i32 => n.id
func demo(flag: bool) {
    var a = Node(1)
    if flag {
        take(&&a)
    }
}
func main(): void {}
)";

    SUBCASE("lowered, the move is still an operand and nothing is dropped") {
        TestCompiler cc(stage("Lower"));
        REQUIRE(cc.compile(source));
        INFO(cc.diagnostics());
        REQUIRE_IR_MATCHES(cc.function("demo"), R"(
            (Function demo (Param flag bool) (Ret void)
                (Block _L_entry ...)
                (Block _L_IF0_then
                    ...
                    (Call (GlobalRef take) (Move (VarRef a)))
                    ...)
                ...))");
        REQUIRE_IR_DOES_NOT_MATCH(cc.function("demo"), "(Function demo ... (Drop _) ...)");
    }

    SUBCASE("after the memory pass the conditional move has a drop flag") {
        TestCompiler cc(stage("MemoryMgmt"));
        REQUIRE(cc.compile(source));
        INFO(cc.diagnostics());
        REQUIRE_IR_MATCHES(cc.function("demo"), R"(
            (Function demo (Param flag bool) (Ret void)
                (Block _L_entry
                    ...
                    (Assign (Member (VarRef __drop_flags) a) 1)
                    ...)
                (Block _L_IF0_then
                    ...
                    (Assign (Member (VarRef __drop_flags) a) 0)
                    (Call (GlobalRef take) (VarRef a))
                    ...)
                ...
                (Block _L_exit
                    ...
                    (DropIf (VarRef a) (Member (VarRef __drop_flags) a))
                    ...)))");
    }
}

// The compiler keeps no state between compilations in a process: the builtins
// module, the interned strings and the compile-time member tables belong to one
// compilation and are reset when it ends.
TEST_CASE("Test compiler: compiling twice in one process gives the same IR")
{
    const std::string source = std::string(nodeProgram) + R"(
func take(n: Node): i32 => n.id
func demo(): i32 {
    var a = Node(1)
    return take(a)
}
func main(): void {}
)";

    std::string first, second;
    {
        TestCompiler cc(stage("MemoryMgmt"));
        REQUIRE(cc.compile(source));
        first = cc.ir().sexpr();
    }
    {
        // something different in between, with the builtins module this time
        CompileOptions withBuiltins;
        withBuiltins.withBuiltins = true;
        TestCompiler cc(withBuiltins);
        REQUIRE(cc.compile("func main(): void { var s = \"x\".S }"));
    }
    {
        TestCompiler cc(stage("MemoryMgmt"));
        REQUIRE(cc.compile(source));
        second = cc.ir().sexpr();
    }
    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
}
