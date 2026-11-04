#include "ir.hpp"

#include "ast.hpp"

namespace cxy::test {

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

IrMatchResult notFound()
{
    IrMatchResult result;
    result.message = "the node was not found";
    return result;
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

IrMatchResult matchIr(const cxy::testing::IrNodeWrapper &actual, const std::string &pattern)
{
    return actual ? matchIr(actual.sexpr(), pattern) : notFound();
}

IrMatchResult matchIr(const cxy::testing::AstNodeWrapper &actual, const std::string &pattern)
{
    return actual ? matchIr(actual.sexpr(), pattern) : notFound();
}

} // namespace cxy::test
