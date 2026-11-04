/**
 * Matching IR and AST S-expressions against patterns.
 *
 * Compile a program with `cxy::testing::TestCompiler` (src/cxy/testing), look up
 * the node you care about, and match its S-expression against an expected
 * pattern. Write the pattern first, then make the compiler produce it.
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

#include "testing/compiler.hpp"

#include <string>

namespace cxy::test {

struct IrMatchResult {
    bool matched = false;
    std::string message; // why it failed, with both sides
};

// `actual` is matched against `pattern`. A `(Function name ...)` or
// `(Global name ...)` pattern is matched against that member of a `(Module ...)`.
IrMatchResult matchIr(const std::string &actual, const std::string &pattern);

// A node that was not found never matches
IrMatchResult matchIr(const cxy::testing::IrNodeWrapper &actual, const std::string &pattern);
IrMatchResult matchIr(const cxy::testing::AstNodeWrapper &actual, const std::string &pattern);

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

// The same matching, for an AST node of a compiled program
#define REQUIRE_NODE_MATCHES REQUIRE_IR_MATCHES
#define CHECK_NODE_MATCHES CHECK_IR_MATCHES
