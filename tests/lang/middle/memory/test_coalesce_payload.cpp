/**
 * `some() ?? null` (`a ? *a : null`) where `some` returns `!T?`. The payload of the result is taken
 * once, and what is taken is not dropped before the `then` path hands it on.
 */

#include "doctest.h"
#include "utils/ir.hpp"

using namespace cxy::test;
using namespace cxy::testing;

namespace {

const char *program = R"(
class Node {
    id: i32
    func `init`(id: i32) { this.id = id }
    func `deinit`() {}
}
func some(): !Node? => Node(1)
func orNull(): !void {
    var y: Node = some() ?? null
}
func main(): void {}
)";

CompileOptions memoryStage()
{
    CompileOptions options;
    options.stage = "MemoryMgmt";
    options.withBuiltins = true;
    options.extraArgs = {"--warnings=Ownership"};
    return options;
}

// The text of a block of a dump, up to the next block
std::string blockOf(const std::string &ir, const std::string &label)
{
    size_t at = ir.find("(Block " + label);
    if (at == std::string::npos)
        return "";
    size_t end = ir.find("(Block ", at + 1);
    return ir.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

} // namespace

TEST_CASE("Coalesce payload: nothing is dropped before the branch")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(program));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("orNull").sexpr();
    const std::string cont = blockOf(ir, "_L_call_cont_1");
    REQUIRE_FALSE(cont.empty());
    const size_t branch = cont.find("(If ");
    REQUIRE(branch != std::string::npos);
    // a drop of the payload here frees what the `then` path is about to hand on
    CHECK(cont.substr(0, branch).find("(Drop") == std::string::npos);
}

// `a ?? null` is `t = a; t.ok ? take(t.val) : null`: the payload moves out of `t`, so
// there is no retain on the `then` path, and `t` has nothing left to drop
TEST_CASE("Coalesce payload: the then path takes the payload, it does not copy it")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(program));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("orNull").sexpr();
    const std::string then = blockOf(ir, "_L_COALESCE0_then");
    REQUIRE_FALSE(then.empty());
    CHECK(then.find("(Copy") == std::string::npos);
    CHECK(then.find("(Drop") == std::string::npos);
    // what was taken is not tracked: no drop flag, no drop at exit
    CHECK(ir.find("__drop_flags) __t") == std::string::npos);
    CHECK(ir.find("(DropIf (VarRef __t") == std::string::npos);
    CHECK(ir.find("(Drop (VarRef __t") == std::string::npos);
}

TEST_CASE("Coalesce payload: a variable on the left is still copied, it keeps its value")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(std::string(program) + R"(
func keep(y: Node?): Node {
    return y ?? null
}
)"));
    INFO(cc.diagnostics());

    const std::string ir = cc.function("keep").sexpr();
    CHECK(blockOf(ir, "_L_COALESCE0_then").find("(Copy") != std::string::npos);
}

TEST_CASE("Coalesce payload: the value reaches the variable, and the verifier is content")
{
    TestCompiler cc(memoryStage());
    REQUIRE(cc.compile(program));

    const std::string ir = cc.function("orNull").sexpr();
    // the `then` path never reads a result payload that was taken already
    CHECK(blockOf(ir, "_L_COALESCE0_then").find("__call_res_0") == std::string::npos);
    CHECK(cc.diagnostics().find("warning[") == std::string::npos);
}
