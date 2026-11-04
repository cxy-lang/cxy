/**
 * Parser Tests: `@copy` expression
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

#include "lang/frontend/strings.h"

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

TEST_CASE("@copy parses like __copy!") {
    ParserTestFixture fixture;
    // The parser builds the `__copy` callee from an interned constant
    cxy::test::MemPoolWrapper stringsPool;
    StrPool strings = newStrPool(stringsPool.get());
    internCommonStrings(&strings);

    SUBCASE("variable") {
        auto atCopy = fixture.parseProgram("func demo() { var b = @copy a }");
        auto macro = fixture.parseProgram("func demo() { var b = __copy!(a) }");
        REQUIRE(atCopy != nullptr);
        REQUIRE(macro != nullptr);
        CHECK(show(atCopy) == show(macro));
    }

    SUBCASE("takes the whole member chain") {
        auto atCopy = fixture.parseProgram("func demo() { var b = @copy a.b.c }");
        auto macro = fixture.parseProgram("func demo() { var b = __copy!(a.b.c) }");
        REQUIRE(atCopy != nullptr);
        REQUIRE(macro != nullptr);
        CHECK(show(atCopy) == show(macro));
    }

    SUBCASE("as a call argument") {
        auto atCopy = fixture.parseProgram("func demo() { take(@copy a) }");
        auto macro = fixture.parseProgram("func demo() { take(__copy!(a)) }");
        REQUIRE(atCopy != nullptr);
        REQUIRE(macro != nullptr);
        CHECK(show(atCopy) == show(macro));
    }
}
