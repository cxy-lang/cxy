/**
 * `:synthetic` says a variable was made by the compiler, not written by the user.
 * It is the one fact the ownership verifier reads, so every variable the compiler
 * makes must carry it, however it was made.
 */

#include "doctest.h"
#include "utils/ir.hpp"

#include <regex>

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
exception TestError(msg: string) => msg
func make(): Node => Node(1)
func peek(n: &Node): i32 => n.id
func adder(n: i32): func(i32) -> i32 {
    return (x: i32) => x + n
}
func coalesce(a: i32?, b: i32): i32 => a ?? b
func both(a: bool, b: bool): bool => a && b || a
func mayFail(x: i32): !i32 {
    if (x < 0) {
        raise TestError("negative")
    }
    return x * 2
}
func caught(): i32 => mayFail(1) catch 0
func rvalues(): i32 {
    make()
    var r = &make()
    return make().id + peek(make()) + r.id + adder(1)(2) + coalesce(null, 3)
}
func main(): void {}
)";

struct Declared {
    std::string name;
    std::string markers;
};

std::vector<Declared> variablesOf(const std::string &text)
{
    std::vector<Declared> found;
    std::regex decl(R"re(\(VarDecl (\S+) ((?:"[^"]*"|[^()"])*)\))re");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), decl);
         it != std::sregex_iterator();
         ++it)
        found.push_back({(*it)[1], (*it)[2]});
    return found;
}

bool has(const Declared &var, const char *marker)
{
    return var.markers.find(marker) != std::string::npos;
}

} // namespace

TEST_CASE("Synthetic: every variable the compiler makes is synthetic and says what it is")
{
    CompileOptions options;
    options.withBuiltins = true;
    TestCompiler cc(options);
    REQUIRE(cc.compile(program));
    INFO(cc.diagnostics());

    auto vars = variablesOf(cc.ir().sexpr());
    REQUIRE(vars.size() > 20);
    for (const Declared &var : vars) {
        INFO(var.name << " " << var.markers);
        // the compiler names its variables `__x`, and a class belongs to its variables
        if (var.name.rfind("__", 0) == 0 || has(var, ":class-"))
            CHECK(has(var, ":synthetic"));
        // lowering leaves nothing without a class unless it owns nothing
        if (var.name.rfind("__ret", 0) == 0)
            CHECK(has(var, ":class-return-slot"));
        if (var.name.rfind("__obj", 0) == 0 || var.name.rfind("__cond", 0) == 0 ||
            var.name.rfind("__catch_res", 0) == 0)
            CHECK(has(var, ":class-transient"));
    }
}

// `__obj` is the object a constructor builds, sema calls it temporary. It is
// transient: the return takes it, so nothing drops it.
TEST_CASE("Synthetic: the object a constructor builds is transient and is not dropped")
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    TestCompiler cc(options);
    REQUIRE(cc.compile(R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
func demo(): i32 {
    var a = Node(1)
    return a.id
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    IrNodeWrapper cover = cc.function("main.Node.op__initCover");
    REQUIRE(cover);
    REQUIRE_IR_MATCHES(cover.findVariable("__obj"),
                       "(VarDecl __obj0 ^Node :class-transient :synthetic)");
    CHECK(cover.sexpr().find("(Drop (VarRef __obj0)") == std::string::npos);
}

TEST_CASE("Synthetic: a variable the user wrote is not synthetic")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
func demo(): i32 {
    var total = 1
    var other = total + 2
    return other
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    for (const Declared &var : variablesOf(cc.function("demo").sexpr())) {
        if (var.name == "total" || var.name == "other") {
            INFO(var.name << " " << var.markers);
            CHECK_FALSE(has(var, ":synthetic"));
        }
    }
}
