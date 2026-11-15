/**
 * `T{}` zero initializes a C union. A union is not a struct, so naming a field
 * is an error: only one of its fields can be live and C decides which.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

CompileOptions withUnions()
{
    CompileOptions options;
    options.stage = "Codegen";
    options.keepGeneratedC = true;
    options.extraArgs = {"--c-header-dir",
                         std::string(TEST_ROOT_DIR) +
                             "/tests/lang/middle/cimport/fixtures"};
    return options;
}

bool mentions(const TestCompiler &cc, const char *text)
{
    return cc.diagnostics().find(text) != std::string::npos;
}

} // namespace

TEST_CASE("Union init: `T{}` compiles and the typedef and its user are one type")
{
    TestCompiler cc(withUnions());
    REQUIRE(cc.compile(R"(
import "union_types.h" as lib
func main(): void {
    var lock = lib.guard_t{}
    lib.lock_take(ptrof lock, 1)
}
)"));
    INFO(cc.diagnostics());
    // the union is zeroed by an empty compound literal
    CHECK(cc.generatedC().find("){};") != std::string::npos);
}

TEST_CASE("Union init: `T{}` through the other typedef")
{
    TestCompiler cc(withUnions());
    REQUIRE(cc.compile(R"(
import "union_types.h" as lib
func main(): void {
    var lock = lib.lock_t{}
    lib.lock_take(ptrof lock, 1)
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Union init: naming a field is an error")
{
    TestCompiler cc(withUnions());
    CHECK_FALSE(cc.compile(R"(
import "union_types.h" as lib
func main(): void {
    var lock = lib.guard_t{align: 1}
}
)"));
    CHECK(mentions(cc, "union"));
    CHECK(mentions(cc, "cannot"));
}
