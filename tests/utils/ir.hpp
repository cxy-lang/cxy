/**
 * IR validation utilities.
 *
 * `TestCompiler` runs the compiler in the test process and keeps the compiled
 * program. A test looks up the node it cares about, turns it into an
 * S-expression, and matches it against an expected pattern. Write the pattern
 * first, then make the compiler produce it.
 *
 *   TestCompiler cc;
 *   REQUIRE(cc.compile("func f(x: i32): i32 => x + 1"));
 *   REQUIRE_IR_MATCHES(cc.function("f"), R"(
 *       (Function f (Param x i32) (Ret i32)
 *           (Block _L_entry ...)
 *           ...))");
 *
 * Pattern syntax, on top of plain S-expressions:
 *   _      matches any single node (an atom or a whole list)
 *   ...    inside a list, matches any number of siblings, including none
 */

#pragma once

#include "doctest.h"

#include "lang/frontend/ast.h"
#include "lang/middle/ir/node.h"

#include <memory>
#include <string>
#include <vector>

namespace cxy::test {

struct CompileOptions {
    // The last stage to run: `Lower` (before the memory pass) or `MemoryMgmt`
    std::string stage = "Lower";
    // The builtins module is off by default to keep the IR small
    bool withBuiltins = false;
    std::vector<std::string> extraArgs;
};

// The compiler, its memory and the result of one compilation. Everything it
// returns stays valid until the object is destroyed.
class TestCompiler {
public:
    explicit TestCompiler(const CompileOptions &options = {});
    ~TestCompiler();
    TestCompiler(const TestCompiler &) = delete;
    TestCompiler &operator=(const TestCompiler &) = delete;

    // Parses `source` and runs the stages up to `options.stage`
    bool compile(const std::string &source);

    // What the compiler reported
    const std::string &diagnostics() const;

    // The AST and the IR as the last stage that ran left them
    AstNode *program() const;
    IrNode *irModule() const;

    IrNode *lookupFunction(const std::string &name) const;
    IrNode *lookupGlobal(const std::string &name) const;

    // The S-expression of any IR node. Names the compiler generates are numbered
    // per function, so the text does not depend on a global counter.
    std::string sexp(const IrNode *node) const;
    std::string function(const std::string &name) const; // "" if there is none
    std::string module() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct IrMatchResult {
    bool matched = false;
    std::string message; // why it failed, with both sides
};

// `actual` is matched against `pattern`. A `(Function name ...)` or
// `(Global name ...)` pattern is matched against that member of a `(Module ...)`.
IrMatchResult matchIr(const std::string &actual, const std::string &pattern);

} // namespace cxy::test

#define REQUIRE_IR_MATCHES(actual, pattern)                                    \
    do {                                                                       \
        auto ir_match_result = cxy::test::matchIr((actual), (pattern));        \
        INFO(ir_match_result.message);                                         \
        REQUIRE(ir_match_result.matched);                                      \
    } while (0)

#define CHECK_IR_MATCHES(actual, pattern)                                      \
    do {                                                                       \
        auto ir_match_result = cxy::test::matchIr((actual), (pattern));        \
        INFO(ir_match_result.message);                                         \
        CHECK(ir_match_result.matched);                                        \
    } while (0)

#define REQUIRE_IR_DOES_NOT_MATCH(actual, pattern)                             \
    do {                                                                       \
        auto ir_match_result = cxy::test::matchIr((actual), (pattern));        \
        INFO("expected no match, but it matched");                             \
        REQUIRE_FALSE(ir_match_result.matched);                                \
    } while (0)
