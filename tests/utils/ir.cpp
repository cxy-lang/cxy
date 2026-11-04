#include "ir.hpp"

#include "ast.hpp"

#include "core/log.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "driver/driver.h"
#include "driver/options.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

#ifndef TEST_ROOT_DIR
#error "TEST_ROOT_DIR must be defined to the root of the source tree"
#endif

namespace cxy::test {

namespace {

std::string readFile(const std::string &path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

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

IrCompileResult compileToIr(const std::string &source, const IrCompileOptions &options)
{
    IrCompileResult result;

    char outPath[] = "/tmp/cxy-irtest-out-XXXXXX";
    int outFd = mkstemp(outPath);
    if (outFd < 0) {
        result.diagnostics = "cannot create a temporary file";
        return result;
    }
    close(outFd);

    // The same options the command line would give
    std::vector<std::string> args = {"cxy",
                                     "dev",
                                     "--no-progress",
                                     "--no-color",
                                     "--dump-stats",
                                     "NONE",
                                     "--stdlib",
                                     std::string(TEST_ROOT_DIR) + "/src/cxy",
                                     "--dump-ast",
                                     "IRSEXP",
                                     "--last-stage",
                                     options.stage,
                                     "-o",
                                     outPath};
    if (!options.withBuiltins)
        args.push_back("--without-builtins");
    for (const std::string &arg : options.extraArgs)
        args.push_back(arg);
    args.push_back("test.cxy"); // the source is given as a string, not read

    std::vector<char *> argv;
    for (std::string &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    int argc = (int)args.size();

    MemPool pool = newMemPool();
    FormatState diagnostics = newFormatState("  ", true);
    Log log = newLog(&pool, printDiagnosticToMemory, nullptr);
    DiagnosticMemoryPrintCtx diagnosticCtx = {.state = &diagnostics, .L = &log};
    log.handlerCtx = &diagnosticCtx;
    StrPool strings = newStrPool(&pool);

    CompilerDriver driver = {};
    driver.cxyBinaryPath = TEST_ROOT_DIR;
    driver.options.strings = &strings;

    bool ok = parseCommandLineOptions(&argc, argv.data(), &strings, &driver.options, &log);
    if (ok)
        ok = initCompilerDriver(&driver, &pool, &strings, &log, argc, argv.data());
    if (ok) {
        ok = compileString(&driver, source.c_str(), source.size(), "test.cxy");
        deinitCompilerDriver(&driver);
    }

    char *text = formatStateToString(&diagnostics);
    result.diagnostics = text ? text : "";
    free(text);
    freeFormatState(&diagnostics);
    freeLog(&log);
    freeStrPool(&strings);
    freeMemPool(&pool);

    result.sexp = readFile(outPath);
    result.ok = ok && !result.sexp.empty();
    unlink(outPath);
    return result;
}

std::string irFunction(const std::string &moduleSexp, const std::string &name)
{
    try {
        SExpr module = parseSerial(moduleSexp);
        for (const SExpr &child : module.children) {
            if (!child.isAtom() && child.children.size() >= 2 &&
                isAtom(child.children[0], "Function") && child.children[1].atom == name)
                return toText(child);
        }
    } catch (const std::exception &) {
    }
    return "";
}

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
