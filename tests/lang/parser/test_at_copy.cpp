/**
 * Parser Tests: `@copy` expression
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"


#include <cstring>

using namespace cxy::parser::test;
using namespace cxy::test;

static void quietHandler(const Diagnostic *, void *) {}

static Lexer lexSource(Log *log, const char *src) {
    return newLexer("test.cxy", src, strlen(src), log);
}

static std::string show(const AstNode *node) {
    return ASTTestUtils::toString(
        node, CompareOptions::defaults().withIgnoreMetadata().withIgnoreLocation());
}

TEST_CASE("@copy lexes as one token") {
    ParserTestFixture fixture;
    Log log = newLog(nullptr, quietHandler, nullptr);

    SUBCASE("@copy then an identifier") {
        auto lexer = lexSource(&log, "@copy x");
        CHECK(advanceLexer(&lexer).tag == tokAtCopy);
        CHECK(advanceLexer(&lexer).tag == tokIdent);
        freeLexer(&lexer);
    }

    SUBCASE("@copy before parentheses") {
        auto lexer = lexSource(&log, "@copy(x)");
        CHECK(advanceLexer(&lexer).tag == tokAtCopy);
        CHECK(advanceLexer(&lexer).tag == tokLParen);
        freeLexer(&lexer);
    }

    SUBCASE("a longer attribute name is not @copy") {
        auto lexer = lexSource(&log, "@copyright x");
        CHECK(advanceLexer(&lexer).tag == tokAt);
        CHECK(advanceLexer(&lexer).tag == tokIdent);
        freeLexer(&lexer);
    }

    SUBCASE("a variable named copy is unaffected") {
        auto lexer = lexSource(&log, "copy");
        CHECK(advanceLexer(&lexer).tag == tokIdent);
        freeLexer(&lexer);
    }
}

static size_t count(const std::string &text, const std::string &what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        n++;
    return n;
}

// `@copy` is a backend call node, not a macro call
TEST_CASE("@copy parses to a backend call") {
    ParserTestFixture fixture;

    SUBCASE("variable") {
        auto ast = fixture.parseProgram("func demo() { var b = @copy a }");
        REQUIRE(ast != nullptr);
        std::string text = show(ast);
        CHECK(count(text, "(BackendCall") == 1);
        CHECK(count(text, "MacroCallExpr") == 0);
    }

    SUBCASE("takes the whole member chain") {
        auto ast = fixture.parseProgram("func demo() { var b = @copy a.b.c }");
        REQUIRE(ast != nullptr);
        std::string text = show(ast);
        CHECK(count(text, "(BackendCall") == 1);
        // The whole `a.b.c` chain is the operand, not just `a`
        CHECK(count(text, "(PathElem") == 3);
        CHECK(count(text, "(BackendCall") == 1);
    }

    SUBCASE("as a call argument") {
        auto ast = fixture.parseProgram("func demo() { take(@copy a, @copy b) }");
        REQUIRE(ast != nullptr);
        CHECK(count(show(ast), "(BackendCall") == 2);
    }

    SUBCASE("prefix operand") {
        auto ast = fixture.parseProgram("func demo() { var b = @copy *a }");
        REQUIRE(ast != nullptr);
        std::string text = show(ast);
        CHECK(count(text, "(BackendCall") == 1);
        CHECK(count(text, "UnaryExpr") == 1);
    }
}
