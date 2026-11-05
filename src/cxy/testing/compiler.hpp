/**
 * A handle on the compiler for tests, and for tools that want to look at a
 * compiled program.
 *
 * `TestCompiler` runs the whole compiler in the calling process and keeps the
 * result: the AST and the IR, as far as the last stage it was asked to run.
 * Node wrappers look things up in them and turn any node into an S-expression.
 *
 *   TestCompiler cc;
 *   cc.compile("func f(x: i32): i32 => x + 1");
 *   cc.ir().findFunction("f").sexpr();
 *   cc.ast().findFunction("f").sexpr();
 *
 * Only one `TestCompiler` can exist at a time: the builtins module is global to
 * the process. Everything a wrapper returns is valid until the compiler is
 * destroyed.
 */

#pragma once

#include "lang/frontend/ast.h"
#include "lang/middle/ir/node.h"

#include <memory>
#include <string>
#include <vector>

struct CompilerDriver;

namespace cxy::testing {

class TestCompiler;

struct CompileOptions {
    // The last stage to run, as `--last-stage` takes it: `Lower` (before the
    // memory pass), `MemoryMgmt`, `Codegen`...
    std::string stage = "Lower";
    // The builtins module is off by default to keep the IR small
    bool withBuiltins = false;
    // Where the standard library is. Empty: the one in the source tree.
    std::string stdlibDir;
    // Any other command line option for `cxy dev`
    std::vector<std::string> extraArgs;
    // Keep the C the backend writes, for `generatedC()`. Needs stage `Codegen`.
    bool keepGeneratedC = false;
};

/**
 * An IR node, found in a compiled program. A default constructed wrapper is
 * empty (`!wrapper`), and looking something up in an empty wrapper gives an
 * empty wrapper, so a chain of lookups needs one check at the end.
 */
class IrNodeWrapper {
public:
    IrNodeWrapper() = default;

    explicit operator bool() const { return node_ != nullptr; }
    const IrNode *node() const { return node_; }
    IrTag tag() const { return node_->tag; }

    // The node as an S-expression, computed once. Names the compiler generates
    // are numbered per function (`_m0`, not `_m14`), also for a node that is
    // part of a function.
    const std::string &sexpr() const;

    // Looking up, from a module
    IrNodeWrapper findFunction(const std::string &name) const;
    IrNodeWrapper findGlobal(const std::string &name) const;

    // Looking up, from a function. `prefix` is the start of the name, and
    // `index` picks among those that match, in declaration order, so
    // `findVariable("_m", 1)` is the second hoisted member temporary.
    IrNodeWrapper findBlock(const std::string &label) const;
    IrNodeWrapper findParam(const std::string &name) const;
    IrNodeWrapper findVariable(const std::string &prefix, int index = 0) const;

private:
    friend class TestCompiler;
    IrNodeWrapper(const IrNode *node, const IrNode *function)
        : node_(node), function_(function)
    {
    }

    const IrNode *node_ = nullptr;
    const IrNode *function_ = nullptr; // the function `node_` is part of
    mutable std::string expr_{};
    mutable bool hasExpr_ = false;
};

/**
 * An AST node, found in a compiled program, with the same conventions as
 * `IrNodeWrapper`. The AST is what the stages that ran left, not what the
 * parser produced.
 */
class AstNodeWrapper {
public:
    AstNodeWrapper() = default;

    explicit operator bool() const { return node_ != nullptr; }
    const AstNode *node() const { return node_; }
    AstTag tag() const { return node_->tag; }

    const std::string &sexpr() const;

    // Anywhere below this node. `index` picks among the matches, in source order.
    AstNodeWrapper findFunction(const std::string &name, int index = 0) const;
    AstNodeWrapper findClass(const std::string &name, int index = 0) const;
    AstNodeWrapper findVariable(const std::string &name, int index = 0) const;

private:
    friend class TestCompiler;
    AstNodeWrapper(const TestCompiler *compiler, const AstNode *node)
        : compiler_(compiler), node_(node)
    {
    }

    const TestCompiler *compiler_ = nullptr;
    const AstNode *node_ = nullptr;
    mutable std::string expr_{};
    mutable bool hasExpr_ = false;
};

class TestCompiler {
public:
    explicit TestCompiler(const CompileOptions &options = {});
    ~TestCompiler();
    TestCompiler(const TestCompiler &) = delete;
    TestCompiler &operator=(const TestCompiler &) = delete;

    // Parses `source` and runs the stages up to `options.stage`. False if the
    // compiler reported an error, see `diagnostics()`.
    bool compile(const std::string &source);

    // Everything the compiler reported
    const std::string &diagnostics() const;

    // The program, as far as the last stage that ran: the whole program, and
    // the module of the IR (empty before the IR exists)
    AstNodeWrapper ast() const;
    IrNodeWrapper ir() const;

    // `ir().findFunction(name)`
    IrNodeWrapper function(const std::string &name) const;

    // The C source of the program, with `keepGeneratedC` and stage `Codegen`.
    // Empty otherwise.
    const std::string &generatedC() const;

    // The compiler driver, for a test that needs something not wrapped yet
    CompilerDriver *driver() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cxy::testing
