#include "ir.hpp"

#include "ast.hpp"

#include "core/log.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "driver/driver.h"
#include "driver/options.h"
#include "lang/middle/ir/dump/sexp.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#ifndef TEST_ROOT_DIR
#error "TEST_ROOT_DIR must be defined to the root of the source tree"
#endif

namespace cxy::test {

// ---------------------------------------------------------------------------
// TestCompiler
// ---------------------------------------------------------------------------

// The builtins module is global to the process, so two compilers cannot be
// alive at the same time
static int liveCompilers = 0;

struct TestCompiler::Impl {
    CompileOptions options;
    std::vector<std::string> args;

    MemPool pool = newMemPool();
    StrPool strings;
    FormatState diagnosticsState = newFormatState("  ", true);
    DiagnosticMemoryPrintCtx diagnosticsCtx{};
    Log log;
    CompilerDriver driver = {};

    bool driverReady = false;
    mutable std::string diagnosticsText;

    explicit Impl(const CompileOptions &opts) : options(opts)
    {
        if (liveCompilers++ > 0) {
            liveCompilers--;
            throw std::logic_error("only one TestCompiler can exist at a time");
        }
        strings = newStrPool(&pool);
        log = newLog(&pool, printDiagnosticToMemory, nullptr);
        diagnosticsCtx = {.state = &diagnosticsState, .L = &log};
        log.handlerCtx = &diagnosticsCtx;

        // The options the command line would give
        args = {"cxy",          "dev",         "--no-progress", "--no-color",
                "--dump-stats", "NONE",        "--stdlib",
                std::string(TEST_ROOT_DIR) + "/src/cxy",
                "--last-stage", options.stage};
        if (!options.withBuiltins)
            args.push_back("--without-builtins");
        for (const std::string &arg : options.extraArgs)
            args.push_back(arg);
        args.push_back("test.cxy");
    }

    ~Impl()
    {
        if (driverReady)
            deinitCompilerDriver(&driver);
        freeFormatState(&diagnosticsState);
        freeLog(&log);
        freeStrPool(&strings);
        freeMemPool(&pool);
        liveCompilers--;
    }

    bool init()
    {
        std::vector<char *> argv;
        for (std::string &arg : args)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        int argc = (int)args.size();

        driver.cxyBinaryPath = TEST_ROOT_DIR;
        driver.options.strings = &strings;
        if (!parseCommandLineOptions(&argc, argv.data(), &strings, &driver.options, &log))
            return false;
        driverReady = initCompilerDriver(&driver, &pool, &strings, &log, argc, argv.data());
        return driverReady;
    }
};

TestCompiler::TestCompiler(const CompileOptions &options)
    : impl_(std::make_unique<Impl>(options))
{
}

TestCompiler::~TestCompiler() = default;

bool TestCompiler::compile(const std::string &source)
{
    if (!impl_->init())
        return false;
    return compileString(&impl_->driver, source.c_str(), source.size(), "test.cxy");
}

const std::string &TestCompiler::diagnostics() const
{
    char *text = formatStateToString(&impl_->diagnosticsState);
    impl_->diagnosticsText = text ? text : "";
    free(text);
    return impl_->diagnosticsText;
}

AstNode *TestCompiler::program() const
{
    AstNode *metadata = impl_->driver.mainMetadata;
    return metadata ? metadata->metadata.node : nullptr;
}

IrNode *TestCompiler::irModule() const
{
    AstNode *metadata = impl_->driver.mainMetadata;
    return metadata ? metadata->metadata.ir : nullptr;
}

IrNode *TestCompiler::lookupFunction(const std::string &name) const
{
    IrNode *module = irModule();
    if (module == nullptr)
        return nullptr;
    for (IrNode *fn = module->module.functions; fn; fn = fn->next) {
        if (fn->tag != irFunction)
            continue;
        if ((fn->function.display && name == fn->function.display) ||
            (fn->function.name && name == fn->function.name))
            return fn;
    }
    return nullptr;
}

IrNode *TestCompiler::lookupGlobal(const std::string &name) const
{
    IrNode *module = irModule();
    if (module == nullptr)
        return nullptr;
    for (IrNode *g = module->module.globals; g; g = g->next) {
        if (g->globalVar.name && name == g->globalVar.name)
            return g;
    }
    return nullptr;
}

std::string TestCompiler::sexp(const IrNode *node) const
{
    if (node == nullptr)
        return "";
    IrSexpOptions options = {.canonicalNames = true};
    FormatState state = newFormatState("  ", true);
    dumpIrToSexpState(node, &state, &options);
    char *text = formatStateToString(&state);
    std::string result = text ? text : "";
    free(text);
    freeFormatState(&state);
    return result;
}

std::string TestCompiler::function(const std::string &name) const
{
    return sexp(lookupFunction(name));
}

std::string TestCompiler::module() const { return sexp(irModule()); }

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

namespace {

std::string toText(const SExpr &e)
{
    if (e.isAtom())
        return e.atom;
    std::string out = "(";
    for (size_t i = 0; i < e.children.size(); i++) {
        if (i)
            out += " ";
        out += toText(e.children[i]);
    }
    return out + ")";
}

bool isAtom(const SExpr &e, const char *text)
{
    return e.isAtom() && e.atom == text;
}

bool matchNode(const SExpr &pattern, const SExpr &actual);

bool matchList(const std::vector<SExpr> &p,
               size_t pi,
               const std::vector<SExpr> &a,
               size_t ai)
{
    for (; pi < p.size(); pi++, ai++) {
        if (isAtom(p[pi], "...")) {
            if (pi + 1 == p.size())
                return true;
            for (size_t k = ai; k <= a.size(); k++) {
                if (matchList(p, pi + 1, a, k))
                    return true;
            }
            return false;
        }
        if (ai >= a.size() || !matchNode(p[pi], a[ai]))
            return false;
    }
    return ai == a.size();
}

bool matchNode(const SExpr &pattern, const SExpr &actual)
{
    if (isAtom(pattern, "_"))
        return true;
    if (pattern.isAtom() || actual.isAtom())
        return pattern.isAtom() && actual.isAtom() && pattern.atom == actual.atom;
    return matchList(pattern.children, 0, actual.children, 0);
}

// The member of a `(Module ...)` named like `pattern`, or null
const SExpr *findMember(const SExpr &module, const SExpr &pattern)
{
    if (pattern.children.size() < 2 || !pattern.children[1].isAtom())
        return nullptr;
    for (const SExpr &child : module.children) {
        if (!child.isAtom() && child.children.size() >= 2 &&
            child.children[0].atom == pattern.children[0].atom &&
            child.children[1].atom == pattern.children[1].atom)
            return &child;
    }
    return nullptr;
}

} // namespace

IrMatchResult matchIr(const std::string &actualText, const std::string &patternText)
{
    IrMatchResult result;
    SExpr actual, pattern;
    try {
        actual = parseSerial(actualText);
        pattern = parseSerial(patternText);
    } catch (const std::exception &e) {
        result.message = std::string("cannot parse: ") + e.what() +
                         "\nactual: " + actualText + "\npattern: " + patternText;
        return result;
    }

    const SExpr *target = &actual;
    bool selectsMember = !pattern.isAtom() && !pattern.children.empty() &&
                         (isAtom(pattern.children[0], "Function") ||
                          isAtom(pattern.children[0], "Global"));
    bool actualIsModule = !actual.isAtom() && !actual.children.empty() &&
                          isAtom(actual.children[0], "Module");
    if (selectsMember && actualIsModule) {
        target = findMember(actual, pattern);
        if (target == nullptr) {
            result.message = "the module has no " + toText(pattern.children[0]) +
                             " named " + toText(pattern.children[1]);
            return result;
        }
    }

    result.matched = matchNode(pattern, *target);
    if (!result.matched) {
        result.message = "pattern: " + toText(pattern) + "\nactual:  " + toText(*target);
    }
    return result;
}

} // namespace cxy::test
