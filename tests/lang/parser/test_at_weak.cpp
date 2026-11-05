/**
 * Parser Tests: `@weak T`, a weak reference type. It is another spelling of
 * `Weak[T]` from the builtins module.
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

#include <cstring>

using namespace cxy::parser::test;
using namespace cxy::test;

static void quietHandler(const Diagnostic *, void *) {}

static Lexer lexSource(Log *log, const char *src)
{
    return newLexer("test.cxy", src, strlen(src), log);
}

static std::string show(const AstNode *node)
{
    return ASTTestUtils::toString(
        node, CompareOptions::defaults().withIgnoreMetadata().withIgnoreLocation());
}

static size_t count(const std::string &text, const std::string &what)
{
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        n++;
    return n;
}

// The declared type of `w` in `func demo() { var w: <type> = null }`
static const AstNode *declaredType(ParserTestFixture &fixture, const char *type)
{
    std::string source = std::string("func demo() { var w: ") + type + " = null }";
    const AstNode *program = fixture.parseProgram(source);
    const AstNode *body = program->program.decls->funcDecl.body;
    return body->blockStmt.stmts->varDecl.type;
}

static const char *pathName(const AstNode *path)
{
    return nodeIs(path, Path) ? path->path.elements->pathElement.name : nullptr;
}

// The printed tree leaves the generic arguments out, so the structure is read here
TEST_CASE("@weak T keeps its target as the one generic argument")
{
    ParserTestFixture fixture;

    SUBCASE("a plain type")
    {
        const AstNode *type = declaredType(fixture, "@weak Node");
        REQUIRE(nodeIs(type, Path));
        CHECK(type->path.isType);
        CHECK(strcmp(pathName(type), "Weak") == 0);

        const AstNode *args = type->path.elements->pathElement.args;
        REQUIRE(args != nullptr);
        CHECK(args->next == nullptr);
        CHECK(strcmp(pathName(args), "Node") == 0);
    }

    SUBCASE("a type with its own arguments")
    {
        const AstNode *type = declaredType(fixture, "@weak Vector[i32]");
        REQUIRE(nodeIs(type, Path));
        const AstNode *args = type->path.elements->pathElement.args;
        REQUIRE(args != nullptr);
        CHECK(strcmp(pathName(args), "Vector") == 0);
        REQUIRE(args->path.elements->pathElement.args != nullptr);
    }

    SUBCASE("the same tree as written out")
    {
        const AstNode *spelled = declaredType(fixture, "@weak Node");
        const AstNode *written = declaredType(fixture, "Weak[Node]");
        REQUIRE(nodeIs(written, Path));
        CHECK(strcmp(pathName(spelled), pathName(written)) == 0);
        CHECK(strcmp(pathName(spelled->path.elements->pathElement.args),
                     pathName(written->path.elements->pathElement.args)) == 0);
        CHECK(spelled->path.isType == written->path.isType);
    }

    SUBCASE("`?` wraps the weak reference, not its target")
    {
        const AstNode *type = declaredType(fixture, "@weak Node?");
        REQUIRE(nodeIs(type, OptionalType));
        const AstNode *weak = type->optionalType.type;
        REQUIRE(nodeIs(weak, Path));
        CHECK(strcmp(pathName(weak), "Weak") == 0);
        CHECK(strcmp(pathName(weak->path.elements->pathElement.args), "Node") == 0);
    }

    SUBCASE("a weak of a weak")
    {
        const AstNode *type = declaredType(fixture, "@weak @weak Node");
        REQUIRE(nodeIs(type, Path));
        const AstNode *inner = type->path.elements->pathElement.args;
        REQUIRE(nodeIs(inner, Path));
        CHECK(strcmp(pathName(inner), "Weak") == 0);
        CHECK(strcmp(pathName(inner->path.elements->pathElement.args), "Node") == 0);
    }
}

TEST_CASE("@weak lexes as one token")
{
    ParserTestFixture fixture;
    Log log = newLog(nullptr, quietHandler, nullptr);

    SUBCASE("@weak then a type")
    {
        auto lexer = lexSource(&log, "@weak Node");
        CHECK(advanceLexer(&lexer).tag == tokAtWeak);
        CHECK(advanceLexer(&lexer).tag == tokIdent);
        freeLexer(&lexer);
    }

    SUBCASE("@weak before a bracket or paren")
    {
        auto lexer = lexSource(&log, "@weak(Node)");
        CHECK(advanceLexer(&lexer).tag == tokAtWeak);
        CHECK(advanceLexer(&lexer).tag == tokLParen);
        freeLexer(&lexer);
    }

    SUBCASE("a longer attribute name is not @weak")
    {
        auto lexer = lexSource(&log, "@weakly x");
        CHECK(advanceLexer(&lexer).tag == tokAt);
        CHECK(advanceLexer(&lexer).tag == tokIdent);
        freeLexer(&lexer);
    }

    SUBCASE("@copy is unaffected")
    {
        auto lexer = lexSource(&log, "@copy x");
        CHECK(advanceLexer(&lexer).tag == tokAtCopy);
        freeLexer(&lexer);
    }
}

TEST_CASE("@weak T parses to Weak[T]")
{
    ParserTestFixture fixture;

    SUBCASE("a variable type")
    {
        auto ast = fixture.parseProgram("func demo(n: Node) { var w: @weak Node = null }");
        REQUIRE(ast != nullptr);
        const std::string spelled = show(ast);

        auto written = fixture.parseProgram("func demo(n: Node) { var w: Weak[Node] = null }");
        REQUIRE(written != nullptr);
        CHECK(spelled == show(written));
    }

    SUBCASE("a field and a parameter")
    {
        auto ast = fixture.parseProgram(R"(
class Child {
    parent: @weak Node
    func attach(p: @weak Node) {}
}
)");
        REQUIRE(ast != nullptr);
        auto written = fixture.parseProgram(R"(
class Child {
    parent: Weak[Node]
    func attach(p: Weak[Node]) {}
}
)");
        REQUIRE(written != nullptr);
        CHECK(show(ast) == show(written));
    }

    SUBCASE("a return type")
    {
        auto ast = fixture.parseProgram("func f(): @weak Node => null");
        auto written = fixture.parseProgram("func f(): Weak[Node] => null");
        REQUIRE(ast != nullptr);
        REQUIRE(written != nullptr);
        CHECK(show(ast) == show(written));
    }

    SUBCASE("the target is any type, a path with arguments included")
    {
        auto ast = fixture.parseProgram("func demo() { var w: @weak Vector[i32] = null }");
        auto written = fixture.parseProgram("func demo() { var w: Weak[Vector[i32]] = null }");
        REQUIRE(ast != nullptr);
        REQUIRE(written != nullptr);
        CHECK(show(ast) == show(written));
    }

    SUBCASE("`?` makes the weak reference optional, the target stays a plain type")
    {
        auto ast = fixture.parseProgram("func demo() { var w: @weak Node? = null }");
        auto written = fixture.parseProgram("func demo() { var w: Weak[Node]? = null }");
        REQUIRE(ast != nullptr);
        REQUIRE(written != nullptr);
        CHECK(show(ast) == show(written));
    }

    SUBCASE("a weak of a weak is two levels")
    {
        auto ast = fixture.parseProgram("func demo() { var w: @weak @weak Node = null }");
        auto written = fixture.parseProgram("func demo() { var w: Weak[Weak[Node]] = null }");
        REQUIRE(ast != nullptr);
        REQUIRE(written != nullptr);
        CHECK(show(ast) == show(written));
    }

    SUBCASE("it is a type path, not a backend call or an attribute")
    {
        auto ast = fixture.parseProgram("func demo() { var w: @weak Node = null }");
        REQUIRE(ast != nullptr);
        const std::string text = show(ast);
        CHECK(count(text, "(BackendCall") == 0);
        CHECK(count(text, "(Attr") == 0);
    }
}

