#include "compiler.hpp"

#include "ast_find.h"

#include "core/log.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "driver/driver.h"
#include "driver/options.h"
#include "lang/backend/c/codegen.h"
#include "lang/middle/dump/sexp.h"
#include "lang/middle/ir/dump/sexp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>

#ifndef CXY_TESTING_STDLIB_DIR
#error "CXY_TESTING_STDLIB_DIR must be defined to the stdlib of the source tree"
#endif

namespace cxy::testing {

// ---------------------------------------------------------------------------
// IrNodeWrapper
// ---------------------------------------------------------------------------

namespace {

bool nameIs(const IrNode *fn, const std::string &name)
{
    return (fn->function.display && name == fn->function.display) ||
           (fn->function.name && name == fn->function.name);
}

std::string irSexp(const IrNode *node, const IrNode *function)
{
    IrSexpOptions options{};
    options.canonicalNames = true;
    options.function = function;

    FormatState state = newFormatState("  ", true);
    dumpIrToSexpState(node, &state, &options);
    char *text = formatStateToString(&state);
    std::string result = text ? text : "";
    free(text);
    freeFormatState(&state);
    return result;
}

} // namespace

const std::string &IrNodeWrapper::sexpr() const
{
    if (!hasExpr_) {
        expr_ = node_ ? irSexp(node_, function_) : "";
        hasExpr_ = true;
    }
    return expr_;
}

IrNodeWrapper IrNodeWrapper::findFunction(const std::string &name) const
{
    if (node_ == nullptr || node_->tag != irModule)
        return {};
    for (const IrNode *fn = node_->module.functions; fn; fn = fn->next) {
        if (fn->tag == irFunction && nameIs(fn, name))
            return IrNodeWrapper(fn, fn);
    }
    return {};
}

IrNodeWrapper IrNodeWrapper::findGlobal(const std::string &name) const
{
    if (node_ == nullptr || node_->tag != irModule)
        return {};
    for (const IrNode *g = node_->module.globals; g; g = g->next) {
        if (g->globalVar.name && name == g->globalVar.name)
            return IrNodeWrapper(g, nullptr);
    }
    return {};
}

IrNodeWrapper IrNodeWrapper::findBlock(const std::string &label) const
{
    if (node_ == nullptr || node_->tag != irFunction)
        return {};
    for (const IrNode *bb = node_->function.firstBlock; bb; bb = bb->next) {
        const char *blockName = bb->basicBlock.name;
        if (blockName && (label == blockName || ("_L_" + label) == blockName))
            return IrNodeWrapper(bb, node_);
    }
    return {};
}

IrNodeWrapper IrNodeWrapper::findParam(const std::string &name) const
{
    if (node_ == nullptr || node_->tag != irFunction)
        return {};
    for (const IrNode *p = node_->function.params; p; p = p->next) {
        if (p->funcParam.name && name == p->funcParam.name)
            return IrNodeWrapper(p, node_);
    }
    return {};
}

IrNodeWrapper IrNodeWrapper::findVariable(const std::string &prefix, int index) const
{
    if (node_ == nullptr)
        return {};

    // A function is searched block by block, a block on its own
    const IrNode *first = nullptr;
    bool oneBlock = false;
    if (node_->tag == irFunction)
        first = node_->function.firstBlock;
    else if (node_->tag == irBasicBlock)
        first = node_, oneBlock = true;
    else
        return {};

    int seen = 0;
    for (const IrNode *bb = first; bb; bb = oneBlock ? nullptr : bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
            if (s->tag != irVarDecl || s->varDecl.name == nullptr ||
                strncmp(s->varDecl.name, prefix.c_str(), prefix.size()) != 0)
                continue;
            if (seen++ == index)
                return IrNodeWrapper(s, function_);
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// TestCompiler
// ---------------------------------------------------------------------------

// The builtins module is global to the process, so two compilers cannot be alive
// at the same time
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
    // `<outputBase>.c` is where the backend writes the program
    std::string outputBase;
    mutable std::string generatedText;

    explicit Impl(const CompileOptions &opts) : options(opts)
    {
        if (liveCompilers++ > 0) {
            liveCompilers--;
            throw std::logic_error("only one TestCompiler can exist at a time");
        }
        strings = newStrPool(&pool);
        log = newLog(&pool, printDiagnosticToMemory, nullptr);
        diagnosticsCtx.state = &diagnosticsState;
        diagnosticsCtx.L = &log;
        log.handlerCtx = &diagnosticsCtx;

        // The options the command line would give
        args = {"cxy",
                "dev",
                "--no-progress",
                "--no-color",
                "--dump-stats",
                "NONE",
                "--stdlib",
                options.stdlibDir.empty() ? CXY_TESTING_STDLIB_DIR : options.stdlibDir,
                "--last-stage",
                options.stage};
        if (!options.withBuiltins)
            args.push_back("--without-builtins");
        for (const std::string &arg : options.extraArgs)
            args.push_back(arg);
        if (options.keepGeneratedC) {
            outputBase = "/tmp/cxy-testing-" + std::to_string(getpid());
            args.push_back("-o");
            args.push_back(outputBase);
        }
        args.push_back("test.cxy");
    }

    ~Impl()
    {
        if (!outputBase.empty())
            std::remove((outputBase + ".c").c_str());
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

        driver.cxyBinaryPath = CXY_TESTING_STDLIB_DIR;
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

const std::string &TestCompiler::generatedC() const
{
    impl_->generatedText.clear();
    if (impl_->outputBase.empty())
        return impl_->generatedText;
    // The backend keeps the file open until the driver goes, what it wrote may still be buffered
    if (impl_->driver.backend != nullptr && ((CBackend *)impl_->driver.backend)->output != nullptr)
        fflush(((CBackend *)impl_->driver.backend)->output);
    FILE *f = fopen((impl_->outputBase + ".c").c_str(), "r");
    if (f == nullptr)
        return impl_->generatedText;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        impl_->generatedText.append(buf, n);
    fclose(f);
    return impl_->generatedText;
}

const std::string &TestCompiler::diagnostics() const
{
    char *text = formatStateToString(&impl_->diagnosticsState);
    impl_->diagnosticsText = text ? text : "";
    free(text);
    return impl_->diagnosticsText;
}

AstNodeWrapper TestCompiler::ast() const
{
    AstNode *metadata = impl_->driver.mainMetadata;
    return metadata ? AstNodeWrapper(this, metadata->metadata.node) : AstNodeWrapper();
}

IrNodeWrapper TestCompiler::ir() const
{
    AstNode *metadata = impl_->driver.mainMetadata;
    return metadata && metadata->metadata.ir ? IrNodeWrapper(metadata->metadata.ir, nullptr)
                                             : IrNodeWrapper();
}

IrNodeWrapper TestCompiler::function(const std::string &name) const
{
    return ir().findFunction(name);
}

CompilerDriver *TestCompiler::driver() const { return &impl_->driver; }

// ---------------------------------------------------------------------------
// AstNodeWrapper
// ---------------------------------------------------------------------------

const std::string &AstNodeWrapper::sexpr() const
{
    if (!hasExpr_) {
        expr_.clear();
        if (node_ != nullptr) {
            FormatState state = newFormatState("  ", true);
            dumpAstToSexpState(compiler_->driver(), const_cast<AstNode *>(node_), &state);
            char *text = formatStateToString(&state);
            expr_ = text ? text : "";
            free(text);
            freeFormatState(&state);
        }
        hasExpr_ = true;
    }
    return expr_;
}

AstNodeWrapper AstNodeWrapper::findFunction(const std::string &name, int index) const
{
    return AstNodeWrapper(compiler_, testingFindAstDecl(node_, astFuncDecl, name.c_str(), index));
}

AstNodeWrapper AstNodeWrapper::findClass(const std::string &name, int index) const
{
    return AstNodeWrapper(compiler_, testingFindAstDecl(node_, astClassDecl, name.c_str(), index));
}

AstNodeWrapper AstNodeWrapper::findVariable(const std::string &name, int index) const
{
    return AstNodeWrapper(compiler_, testingFindAstDecl(node_, astVarDecl, name.c_str(), index));
}

} // namespace cxy::testing
