#include "ir.hpp"

#include "ast.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

#ifndef CXY_BINARY
#error "CXY_BINARY must be defined to the path of the cxy compiler"
#endif

namespace cxy::test {

namespace {

std::string quote(const std::string &s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
}

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

    char srcPath[] = "/tmp/cxy-irtest-XXXXXX.cxy";
    char outPath[] = "/tmp/cxy-irtest-out-XXXXXX";
    int srcFd = mkstemps(srcPath, 4);
    int outFd = mkstemp(outPath);
    if (srcFd < 0 || outFd < 0) {
        result.diagnostics = "cannot create temporary files";
        return result;
    }
    close(outFd);
    {
        FILE *f = fdopen(srcFd, "w");
        fputs(source.c_str(), f);
        fclose(f);
    }

    std::string cmd = quote(CXY_BINARY) +
                      " dev --no-progress --no-color --dump-stats NONE"
                      " --stdlib " + quote(std::string(TEST_ROOT_DIR) + "/src/cxy");
    if (!options.withBuiltins)
        cmd += " --without-builtins";
    cmd += " --dump-ast IRSEXP --last-stage " + options.stage;
    for (const std::string &arg : options.extraArgs)
        cmd += " " + quote(arg);
    cmd += " -o " + quote(outPath) + " " + quote(srcPath) + " 2>&1";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        result.diagnostics = "cannot run " + cmd;
    }
    else {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
            result.diagnostics.append(buf, n);
        int status = pclose(pipe);
        result.sexp = readFile(outPath);
        result.ok = status == 0 && !result.sexp.empty();
    }

    unlink(srcPath);
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
