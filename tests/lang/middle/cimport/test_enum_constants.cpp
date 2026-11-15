/**
 * The constants of an unnamed C enum are `int`, whatever type the enum itself
 * has, so they can be passed where C takes an `int`. glibc declares sysconf's
 * `_SC_*` names this way.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

CompileOptions withEnums()
{
    CompileOptions options;
    options.extraArgs = {"--c-header-dir",
                         std::string(TEST_ROOT_DIR) +
                             "/tests/lang/middle/cimport/fixtures"};
    return options;
}

} // namespace

TEST_CASE("Unnamed enum: a constant is passed where C takes an int")
{
    TestCompiler cc(withEnums());
    CHECK(cc.compile(R"(
import "enum_types.h" as lib
func main(): void {
    var shared = lib.mode_value(MODE_SHARED!)
    var exclusive = lib.mode_value(MODE_EXCLUSIVE!)
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Unnamed enum: a constant stored in an i32")
{
    TestCompiler cc(withEnums());
    CHECK(cc.compile(R"(
import "enum_types.h" as lib
func main(): void {
    var mode: i32 = MODE_EXCLUSIVE!
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Unnamed enum: a value that does not fit an int keeps the enum's type")
{
    TestCompiler cc(withEnums());
    CHECK(cc.compile(R"(
import "enum_types.h" as lib
func main(): void {
    var flag = lib.flag_value(BIG_FLAG!)
}
)"));
    INFO(cc.diagnostics());
}
