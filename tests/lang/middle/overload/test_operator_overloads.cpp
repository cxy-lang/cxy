/**
 * An operator with several overloads picks the one that matches its operand. The
 * operand is wrapped in a reference only when that overload takes a reference. The
 * overload a lookup by name returns first is arbitrary (it depends on how the C
 * library sorts equal names), so the position of the reference overload must not
 * matter.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

CompileOptions withBuiltins()
{
    CompileOptions options;
    options.withBuiltins = true;
    return options;
}

std::string program(const char *first, const char *second, const char *third)
{
    return std::string(R"(
class K {
    n: i32
    func `init`(n: i32) { this.n = n }
)") + "    const func `==`(other: " + first + "): bool => true\n" +
           "    const func `==`(other: " + second + "): bool => true\n" +
           "    const func `==`(other: " + third + "): bool => true\n" +
           R"(}
func compare(a: &K, v: strview): bool => a == v
func main(): void {}
)";
}

} // namespace

TEST_CASE("Operator overloads: the reference overload is declared first")
{
    TestCompiler cc(withBuiltins());
    CHECK(cc.compile(program("&const K", "const string", "strview")));
    INFO(cc.diagnostics());
}

TEST_CASE("Operator overloads: the reference overload is declared second")
{
    TestCompiler cc(withBuiltins());
    CHECK(cc.compile(program("const string", "&const K", "strview")));
    INFO(cc.diagnostics());
}

TEST_CASE("Operator overloads: the reference overload is declared last")
{
    TestCompiler cc(withBuiltins());
    CHECK(cc.compile(program("const string", "strview", "&const K")));
    INFO(cc.diagnostics());
}

TEST_CASE("Operator overloads: a value is still referenced for a reference overload")
{
    TestCompiler cc(withBuiltins());
    CHECK(cc.compile(R"(
class K {
    n: i32
    func `init`(n: i32) { this.n = n }
    const func `==`(other: &const K): bool => n == other.n
}
func same(): bool {
    var a = K(1)
    var b = K(1)
    return a == b
}
func main(): void {}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Operator overloads: String compared with a strview parameter")
{
    TestCompiler cc(withBuiltins());
    CHECK(cc.compile(R"(
func same(a: &String, v: strview): bool => a == v
func main(): void {}
)"));
    INFO(cc.diagnostics());
}
