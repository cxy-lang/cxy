#include "lower_internal.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/middle/mangle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void appendNormalizedName(FormatState *state, const AstNode *node)
{
    if (node == NULL)
        return;

    switch (node->tag) {
    case astIdentifier:
        format(state, "{s}", (FormatArg[]){{.s = node->ident.value}});
        break;
    case astPath: {
        for (const AstNode *elem = node->path.elements; elem;
             elem = elem->next) {
            if (elem != node->path.elements)
                format(state, "_", NULL);
            format(state, "{s}", (FormatArg[]){{.s = elem->pathElement.name}});
        }
        break;
    }
    case astPathElem:
        format(state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
        break;
    case astMemberExpr:
        appendNormalizedName(state, node->memberExpr.target);
        format(state, "_", NULL);
        appendNormalizedName(state, node->memberExpr.member);
        break;
    case astTypeRef:
        if (node->type && node->type->name)
            format(state, "{s}", (FormatArg[]){{.s = node->type->name}});
        else
            format(state, "{s}", (FormatArg[]){{.s = node->_name}});
        break;
    default:
        if (node->_name)
            format(state, "{s}", (FormatArg[]){{.s = node->_name}});
        break;
    }
}

cstring makeScopedSymbolName(LowerContext *ctx,
                             const AstNode *decl,
                             cstring baseName)
{
    if (decl && (hasFlag(decl, Main) || hasFlag(decl, Extern)))
        return baseName;

    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    cstring ns = (decl && decl->type && decl->type->ns) ? decl->type->ns : modName;
    if (ns == NULL || ns == S_main || baseName == NULL)
        return baseName;

    if (baseName == ns || strncmp(baseName, ns, strlen(ns)) == 0)
        return baseName;

    return makeStringf(ctx->strings, "%s_%s", ns, baseName);
}

// Compiler generated names use '$' (not writable in source), C can't have it
static cstring toCSymbolName(LowerContext *ctx, cstring name)
{
    if (name == NULL || strchr(name, '$') == NULL)
        return name;
    FormatState state = newFormatState(NULL, true);
    for (const char *p = name; *p; p++) {
        if (*p == '$')
            format(&state, "__", NULL);
        else
            append(&state, p, 1);
    }
    char *str = formatStateToString(&state);
    cstring res = makeString(ctx->strings, str);
    free(str);
    freeFormatState(&state);
    return res;
}

static cstring getFunctionSymbolNameRaw(LowerContext *ctx,
                                        const AstNode *funcDecl);

cstring getFunctionSymbolName(LowerContext *ctx, const AstNode *funcDecl)
{
    if (funcDecl && funcDecl->ir && funcDecl->ir->tag == irFunction)
        return funcDecl->ir->function.name;
    return toCSymbolName(ctx, getFunctionSymbolNameRaw(ctx, funcDecl));
}

static cstring getFunctionSymbolNameRaw(LowerContext *ctx,
                                        const AstNode *funcDecl)
{
    if (funcDecl == NULL)
        return NULL;

    cstring funcName = getDeclarationName(funcDecl);
    if (hasFlag(funcDecl, Main) || funcName == S_main)
        return S__main;

    AstNode *parent = funcDecl->parentScope;
    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    cstring ns = (funcDecl->type && funcDecl->type->ns)
                     ? funcDecl->type->ns
                     : ((parent && parent->type && parent->type->ns) ? parent->type->ns : modName);
    if (parent && isClassOrStructAstNode(parent)) {
        cstring typeName = getDeclarationName(parent);
        if (ns == NULL || ns == S_main)
            return makeStringf(ctx->strings, "%s_%s", typeName, funcName);
        return makeStringf(
            ctx->strings, "%s_%s_%s", ns, typeName, funcName);
    }

    return makeScopedSymbolName(ctx, funcDecl, funcName);
}

cstring getNormalizedName(LowerContext *ctx, const AstNode *node)
{
    if (node == NULL)
        return NULL;
    FormatState state = newFormatState(NULL, true);
    appendNormalizedName(&state, node);
    char *str = formatStateToString(&state);
    cstring res = (str && str[0]) ? makeString(ctx->strings, str) : NULL;
    free(str);
    freeFormatState(&state);
    return toCSymbolName(ctx, res);
}
