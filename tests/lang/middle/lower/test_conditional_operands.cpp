/**
 * An operand that is evaluated only on some paths must not run before the branch:
 * the body or the otherwise of `a ? b : c`, the right of `&&`, `||` and `??`.
 * An `f"..."` builds its string with statements, they belong to the operand.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

CompileOptions lowered()
{
    CompileOptions options;
    options.stage = "Lower";
    options.withBuiltins = true; // `f"..."` builds a `String`
    return options;
}

// Where the string builder is made, and where the first branch is
size_t builderAt(const std::string &ir)
{
    return ir.find("op__initI_ECover");
}

size_t branchAt(const std::string &ir)
{
    return ir.find("(If ");
}

} // namespace

TEST_CASE("Conditional operands: an f-string in a ternary is built on its path only")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
func body(flag: bool): String {
    return flag ? f"yes {1}" : "no".S
}
func otherwise(flag: bool): String {
    return flag ? "yes".S : f"no {1}"
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    for (const char *name : {"body", "otherwise"}) {
        INFO(name);
        const std::string ir = cc.function(name).sexpr();
        REQUIRE(branchAt(ir) != std::string::npos);
        REQUIRE(builderAt(ir) != std::string::npos);
        CHECK(branchAt(ir) < builderAt(ir));
    }
}

TEST_CASE("Conditional operands: an f-string on the right of &&, ||, ?? and catch is built on its path only")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
func both(flag: bool): bool {
    return flag && f"x{2}".size() > 0
}
func either(flag: bool): bool {
    return flag || f"x{2}".size() > 0
}
func fallback(y: String?): String {
    return y ?? f"none {3}"
}
func name(): !String => "a".S
func recovered(): String {
    return name() catch f"err {1}"
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    for (const char *name : {"both", "either", "fallback", "recovered"}) {
        INFO(name);
        const std::string ir = cc.function(name).sexpr();
        REQUIRE(branchAt(ir) != std::string::npos);
        REQUIRE(builderAt(ir) != std::string::npos);
        CHECK(branchAt(ir) < builderAt(ir));
    }
}

TEST_CASE("Conditional operands: an f-string that is always evaluated is still built first")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(R"(
func plain(): String {
    return f"yes {1}"
}
func inCondition(flag: bool): String {
    return f"yes {1}".size() > 0 ? "a".S : "b".S
}
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("inCondition").sexpr();
    REQUIRE(branchAt(ir) != std::string::npos);
    REQUIRE(builderAt(ir) != std::string::npos);
    CHECK(builderAt(ir) < branchAt(ir));
}
