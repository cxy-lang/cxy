/**
 * A class that overrides a virtual method without `virtual` closes it: a method of
 * the same name further down is a new method. It is an error when it says
 * `@override`, and a warning when it does not.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

bool mentions(const TestCompiler &cc, const char *text)
{
    return cc.diagnostics().find(text) != std::string::npos;
}

const char *closed = R"(
class Base {
    func `init`() {}
    virtual func who(): i32 => 1
}
class Middle: Base {
    func `init`() { super() }
    func who(): i32 => 2
}
)";

} // namespace

TEST_CASE("Closed override: @override on a closed method is an error")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(std::string(closed) + R"(
class Leaf: Middle {
    func `init`() { super() }
    @override
    func who(): i32 => 3
}
func main(): void {}
)"));
    CHECK(mentions(cc, "cannot override"));
    CHECK(mentions(cc, "overridden here without `virtual`"));
}

TEST_CASE("Closed override: without @override it is a warning and compiles")
{
    TestCompiler cc;
    CHECK(cc.compile(std::string(closed) + R"(
class Leaf: Middle {
    func `init`() { super() }
    func who(): i32 => 3
}
func main(): void {}
)"));
    CHECK(mentions(cc, "is not an override"));
    CHECK_FALSE(mentions(cc, "cannot override"));
}

TEST_CASE("Closed override: `virtual` in the middle keeps the chain open")
{
    TestCompiler cc;
    CHECK(cc.compile(R"(
class Base {
    func `init`() {}
    virtual func who(): i32 => 1
}
class Middle: Base {
    func `init`() { super() }
    virtual func who(): i32 => 2
}
class Leaf: Middle {
    func `init`() { super() }
    @override
    func who(): i32 => 3
}
func main(): void {}
)"));
    CHECK_FALSE(mentions(cc, "not an override"));
    CHECK_FALSE(mentions(cc, "cannot override"));
}

TEST_CASE("Closed override: a method the middle class did not touch can be overridden")
{
    TestCompiler cc;
    CHECK(cc.compile(R"(
class Base {
    func `init`() {}
    virtual func who(): i32 => 1
    virtual func other(): i32 => 10
}
class Middle: Base {
    func `init`() { super() }
    func who(): i32 => 2
}
class Leaf: Middle {
    func `init`() { super() }
    @override
    func other(): i32 => 30
}
func main(): void {}
)"));
    CHECK_FALSE(mentions(cc, "not an override"));
    CHECK_FALSE(mentions(cc, "cannot override"));
}

TEST_CASE("Closed override: a plain method redeclared further down is not reported")
{
    TestCompiler cc;
    CHECK(cc.compile(R"(
class Base {
    func `init`() {}
    virtual func who(): i32 => 1
    func plain(): i32 => 5
}
class Middle: Base {
    func `init`() { super() }
    func plain(): i32 => 6
}
class Leaf: Middle {
    func `init`() { super() }
    func plain(): i32 => 7
}
func main(): void {}
)"));
    CHECK_FALSE(mentions(cc, "not an override"));
    CHECK_FALSE(mentions(cc, "cannot override"));
}

TEST_CASE("Closed override: a class with a virtual method of its own is checked too")
{
    TestCompiler cc;
    CHECK(cc.compile(std::string(closed) + R"(
class Leaf: Middle {
    func `init`() { super() }
    func who(): i32 => 3
    virtual func extra(): i32 => 4
}
func main(): void {}
)"));
    CHECK(mentions(cc, "is not an override"));
}

TEST_CASE("Closed override: @override is an error in a class with a virtual method of its own")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(std::string(closed) + R"(
class Leaf: Middle {
    func `init`() { super() }
    @override
    func who(): i32 => 3
    virtual func extra(): i32 => 4
}
func main(): void {}
)"));
    CHECK(mentions(cc, "cannot override"));
}
