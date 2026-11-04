#include "ast_find.h"

#include "lang/frontend/visitor.h"

#include <string.h>

typedef struct FindContext {
    AstTag tag;
    const char *name;
    int index;
    int seen;
    const AstNode *found;
} FindContext;

static void visitDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    FindContext *ctx = getConstAstVisitorContext(visitor);
    if (ctx->found == NULL && node->tag == ctx->tag) {
        const char *name = getDeclarationName(node);
        if (name != NULL && strcmp(name, ctx->name) == 0 && ctx->seen++ == ctx->index)
            ctx->found = node;
    }
    astConstVisitFallbackVisitAll(visitor, node);
}

const AstNode *testingFindAstDecl(const AstNode *root,
                                  AstTag tag,
                                  const char *name,
                                  int index)
{
    if (root == NULL)
        return NULL;

    FindContext ctx = {.tag = tag, .name = name, .index = index};
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&ctx, {
        [astFuncDecl] = visitDecl,
        [astClassDecl] = visitDecl,
        [astVarDecl] = visitDecl,
    });
    // clang-format on
    visitor.fallback = astConstVisitFallbackVisitAll;
    astConstVisit(&visitor, root);
    return ctx.found;
}
