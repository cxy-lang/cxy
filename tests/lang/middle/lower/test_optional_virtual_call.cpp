/**
 * A virtual method called through `&.` on an optional receiver is dispatched through
 * the vtable like any other virtual call. It used to be a direct call of the base
 * method, so an override was never reached.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
class Base {
    virtual func ping(): void {}
    virtual func tag(): void
    func plain(): void {}
}
class Derived: Base {
    func `init`() {}
    @override
    func ping(): void {}
    @override
    func tag(): void {}
}
)";

CompileOptions lowered()
{
    CompileOptions options;
    options.stage = "Lower";
    return options;
}

// A direct call names the method, a virtual one goes through the vtable
bool callsDirectly(const std::string &ir, const char *method)
{
    return ir.find(std::string("(GlobalRef Base_") + method + ")") != std::string::npos;
}

bool usesVtable(const std::string &ir)
{
    return ir.find("vtable") != std::string::npos;
}

} // namespace

TEST_CASE("Optional virtual call: through an optional reference")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaRef(b: &Base?): void { b&.ping() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("viaRef").sexpr();
    CHECK(usesVtable(ir));
    CHECK_FALSE(callsDirectly(ir, "ping"));
}

TEST_CASE("Optional virtual call: through an optional owner")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaOwner(b: Base?): void { b&.ping() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("viaOwner").sexpr();
    CHECK(usesVtable(ir));
    CHECK_FALSE(callsDirectly(ir, "ping"));
}

TEST_CASE("Optional virtual call: a method with no body in the base")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func abstractCall(b: &Base?): void { b&.tag() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("abstractCall").sexpr();
    CHECK(usesVtable(ir));
    CHECK_FALSE(callsDirectly(ir, "tag"));
}

TEST_CASE("Optional virtual call: a method that is not virtual stays direct")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func notVirtual(b: &Base?): void { b&.plain() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("notVirtual").sexpr();
    CHECK(callsDirectly(ir, "plain"));
    CHECK_FALSE(usesVtable(ir));
}

TEST_CASE("Optional virtual call: the null safe form dispatches through the vtable too")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func safeRef(b: &Base?): void { b?.ping() }
func safeOwner(b: Base?): void { b?.ping() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    for (const char *name : {"safeRef", "safeOwner"}) {
        INFO(name);
        const std::string ir = cc.function(name).sexpr();
        CHECK(usesVtable(ir));
        CHECK_FALSE(callsDirectly(ir, "ping"));
    }
}

TEST_CASE("Virtual call: a plain receiver is unchanged")
{
    TestCompiler cc(lowered());
    REQUIRE(cc.compile(std::string(program) + R"(
func viaPlain(b: &Base): void { b.ping() }
func main(): void {}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("viaPlain").sexpr();
    CHECK(usesVtable(ir));
    CHECK_FALSE(callsDirectly(ir, "ping"));
}
