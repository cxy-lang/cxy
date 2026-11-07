/**
 * A method declared in a class without a body is abstract. A class that inherits
 * one must implement it, and a class with an abstract method cannot be created.
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

} // namespace

TEST_CASE("Abstract: a child that implements the method compiles")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
class Base {
    virtual func cancel(): bool
    virtual func note(): void
}
class Child: Base {
    func `init`() {}
    @override
    func cancel(): bool => true
    @override
    func note(): void {}
}
func main(): void {
    var c = Child()
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Abstract: a child that does not implement the method is an error")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(R"(
class Base {
    virtual func cancel(): bool
}
class Forgot: Base {
    func `init`() {}
}
func main(): void {}
)"));
    CHECK(mentions(cc, "must implement the abstract method `cancel`"));
    CHECK(mentions(cc, "abstract method declared here"));
}

TEST_CASE("Abstract: it is an error for a method that returns nothing too")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(R"(
class Base {
    virtual func tag(): void
}
class Forgot: Base {
    func `init`() {}
}
func main(): void {}
)"));
    CHECK(mentions(cc, "must implement the abstract method `tag`"));
}

TEST_CASE("Abstract: only the methods left out are reported")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(R"(
class Base {
    virtual func a(): bool
    virtual func b(): bool
}
class Partial: Base {
    func `init`() {}
    @override
    func a(): bool => true
}
func main(): void {}
)"));
    CHECK(mentions(cc, "`b`"));
    CHECK_FALSE(mentions(cc, "`a`"));
}

TEST_CASE("Abstract: an implementation is inherited by the classes below it")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
class Base {
    virtual func cancel(): bool
}
class Middle: Base {
    func `init`() {}
    @override
    func cancel(): bool => true
}
class Leaf: Middle {
    func `init`() { super() }
}
func main(): void {
    var l = Leaf()
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Abstract: a method with a body in the base need not be overridden")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
class Base {
    func `init`() {}
    virtual func ping(): bool { return true }
}
class Child: Base {
    func `init`() { super() }
}
func main(): void {
    var c = Child()
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Abstract: a class with an abstract method cannot be created")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(R"(
class Base {
    func `init`() {}
    virtual func cancel(): bool
}
func main(): void {
    var b = Base()
}
)"));
    CHECK(mentions(cc, "abstract type"));
}
