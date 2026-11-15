/**
 * The result of `f() catch { ... }` is a variable that the memory pass drops where
 * the catch ends. When the catch body gives no value that variable is never set, so
 * it has to start out empty: dropping what the stack held crashed.
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

const char *program = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
}
func make(fail: bool): !Node {
    if fail {
        raise Exception("no")
    }
    return Node(1)
}
func caught(): bool {
    var failed = false
    make(true) catch {
        failed = true
    }
    return failed
}
func main(): void {}
)";

} // namespace

TEST_CASE("Catch without a value: the result variable starts empty")
{
    TestCompiler cc(withBuiltins());
    REQUIRE(cc.compile(program));

    // set before the branch that decides between the catch and the success path
    CHECK_IR_MATCHES(cc.function("caught"), R"(
        (Function caught (Ret bool)
            (Block _L_entry
                ...
                (VarDecl __catch_res0 ...)
                (Assign (VarRef __catch_res0) null ...)
                (If ...))
            ...))");
}
