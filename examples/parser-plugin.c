//
// Created by Carter Mbotho on 2025-05-14.
//

#include <cxy/core/log.h>
#include <cxy/strings.h>

#include <cxy/ast.h>
#include <cxy/flag.h>
#include <cxy/plugin.h>

/**
 * sum(a, b, c, ...) - sums all integer literal arguments at parse time
 * and returns a single integer literal with the result.
 */
static AstNode *parserSum(CxyPluginContext *ctx,
                          const AstNode *node,
                          AstNode *args)
{
    if (args == NULL) {
        logError(
            ctx->L, &node->loc, "sum: expected at least one argument", NULL);
        return NULL;
    }

    // Start with the first argument as the accumulator, then left-fold
    // remaining arguments into binary addition expressions:
    //   sum(a, b, c) => (a + b) + c
    AstNode *result = args;
    for (AstNode *arg = args->next; arg; arg = arg->next) {
        result = makeBinaryExpr(
            ctx->pool, &node->loc, flgNone, result, opAdd, arg, NULL, NULL);
    }

    return result;
}

/**
 * trace - logs when applied to an entity via attribute syntax
 * Example: @parser::trace
 * Returns the entity unchanged
 */
static AstNode *parserTrace(CxyPluginContext *ctx,
                            const AstNode *node,
                            AstNode *args)
{
    const char *nodeType = "unknown";
    const char *nodeName = NULL;
    
    // Extract name based on node type
    switch (node->tag) {
    case astStructDecl:
        nodeType = "struct";
        nodeName = node->structDecl.name;
        break;
    case astClassDecl:
        nodeType = "class";
        nodeName = node->classDecl.name;
        break;
    case astFuncDecl:
        nodeType = "function";
        nodeName = node->funcDecl.name;
        break;
    case astVarDecl:
        nodeType = "variable";
        nodeName = node->varDecl.names->ident.value;
        break;
    case astFieldDecl:
        nodeType = "field";
        nodeName = node->structField.name;
        break;
    case astEnumOptionDecl:
        nodeType = "enum option";
        nodeName = node->enumOption.name;
        break;
    default:
        nodeType = "entity";
        break;
    }
    
    if (nodeName) {
        logNote(ctx->L,
                &node->loc,
                "[TRACE] {s} '{s}'",
                (const FormatArg[]){{.s = nodeType}, {.s = nodeName}});
    } else {
        logNote(ctx->L,
                &node->loc,
                "[TRACE] {s}",
                (const FormatArg[]){{.s = nodeType}});
    }
    
    // Return the entity unchanged (cast away const)
    return (AstNode *)node;
}

bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    // Declare this as a parser-level plugin — actions run during parsing,
    // before type information is available.
    cxyPluginInitialize(ctx, NULL, pipParser);

    logNote(ctx->L,
            loc,
            "parser-plugin loaded {s}",
            (const FormatArg[]){{.s = S_Redirect ? "(redirect)" : "(static)"}});

    cxyPluginRegisterAction(ctx,
                            loc,
                            (CxyPluginAction[]){
                                {.name = "sum", .fn = parserSum},
                                {.name = "trace", .fn = parserTrace}
                            },
                            2);

    return true;
}