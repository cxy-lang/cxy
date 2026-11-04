/**
 * IR validation utilities.
 *
 * Compile a program with the compiler library, in the test process, take its IR
 * as an S-expression, and match it against an expected pattern. Write the
 * pattern first, then make the compiler produce it.
 *
 *   auto ir = compileToIr("func f(x: i32): i32 => x + 1");
 *   REQUIRE(ir.ok);
 *   REQUIRE_IR_MATCHES(irFunction(ir.sexp, "f"), R"(
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

#include <string>
#include <vector>

namespace cxy::test {

struct IrCompileOptions {
    // Stop after this stage: Lower (before the memory pass) or MemoryMgmt
    std::string stage = "Lower";
    // The stdlib and the builtins module are off by default to keep the IR small
    bool withBuiltins = false;
    std::vector<std::string> extraArgs;
};

struct IrCompileResult {
    bool ok = false;
    std::string sexp;        // the IR of the main module
    std::string diagnostics; // what the compiler printed
};

IrCompileResult compileToIr(const std::string &source,
                            const IrCompileOptions &options = {});

// The `(Function name ...)` of a module dump, or an empty string
std::string irFunction(const std::string &moduleSexp, const std::string &name);

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
