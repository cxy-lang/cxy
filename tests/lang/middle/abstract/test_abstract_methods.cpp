/**
 * A method declared in a class without a body is abstract. A class that inherits
 * one and does not implement it is abstract too, so it can never be created. A
 * class below it that implements what is left is a normal class again.
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

const char *chain = R"(
class Base {
    virtual func cancel(): bool
}
class Forgot: Base {
    func `init`() {}
}
class Implements: Forgot {
    func `init`() { super() }
    func cancel(): bool => false
}
)";

} // namespace

TEST_CASE("Abstract: a child that implements the method compiles and can be created")
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

TEST_CASE("Abstract: a child that does not implement the method is abstract, not an error")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(chain) + R"(
func main(): void {}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Abstract: a class that left a method out cannot be created")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(std::string(chain) + R"(
func main(): void {
    var f = Forgot()
}
)"));
    CHECK(mentions(cc, "abstract type"));
    CHECK(mentions(cc, "`cancel`"));
}

TEST_CASE("Abstract: it holds for a method that returns nothing too")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(R"(
class Base {
    virtual func tag(): void
}
class Forgot: Base {
    func `init`() {}
}
func main(): void {
    var f = Forgot()
}
)"));
    CHECK(mentions(cc, "`tag`"));
}

TEST_CASE("Abstract: the class below that implements it can be created")
{
    TestCompiler cc;
    REQUIRE(cc.compile(std::string(chain) + R"(
func ask(b: &Base): bool => b.cancel()
func main(): void {
    var i = Implements()
    var answer = ask(&i)
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Abstract: what is left is named, and only what is left")
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
func main(): void {
    var p = Partial()
}
)"));
    CHECK(mentions(cc, "`b`"));
    CHECK_FALSE(mentions(cc, "`a`"));
}

TEST_CASE("Abstract: the methods are implemented across the levels")
{
    TestCompiler cc;
    REQUIRE(cc.compile(R"(
class Base {
    virtual func a(): bool
    virtual func b(): bool
}
class Middle: Base {
    func `init`() {}
    @override
    func a(): bool => true
}
class Leaf: Middle {
    func `init`() { super() }
    @override
    func b(): bool => true
}
func main(): void {
    var l = Leaf()
}
)"));
    INFO(cc.diagnostics());
}

TEST_CASE("Abstract: the middle of those levels still cannot be created")
{
    TestCompiler cc;
    CHECK_FALSE(cc.compile(R"(
class Base {
    virtual func a(): bool
    virtual func b(): bool
}
class Middle: Base {
    func `init`() {}
    @override
    func a(): bool => true
}
func main(): void {
    var m = Middle()
}
)"));
    CHECK(mentions(cc, "`b`"));
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

TEST_CASE("Abstract: the class that declares the method cannot be created")
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
    CHECK(mentions(cc, "`cancel`"));
}
