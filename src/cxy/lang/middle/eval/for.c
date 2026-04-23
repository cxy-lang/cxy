//
// Created by Carter Mbotho on 2024-01-09.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/iterable.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

static AstNode *getEvaluatedBody(const AstNode *loop, AstNode *body)
{
    if (!nodeIs(body, BlockStmt) || findAttribute(loop, S_consistent))
        return body;
    return body->blockStmt.stmts;
}

static bool evalForStmtWithIterator(AstVisitor *visitor,
                                    AstNode *node,
                                    AstNodeList *nodes)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *range = node->forStmt.range, *variable = node->forStmt.var;
    bool isVariadic = false;

    // Handle variadic identifier - resolve to actual parameter list
    if (nodeIs(range, Identifier) && hasFlag(range, Variadic)) {
        csAssert0(range->ident.resolvesTo);
        range = range->ident.resolvesTo;
        isVariadic = true;

        // Empty variadic - nothing to iterate
        if (typeIs(range->type, Void))
            return true;
    }

    // Create iterator from evaluated range expression
    ComptimeIterator it = newComptimeIterator(ctx->pool, ctx->types, range);

    if (it.kind == citInvalid) {
        logError(ctx->L,
                 &range->loc,
                 "`#for` loop range expression is not comptime iterable",
                 NULL);
        node->tag = astError;
        return false;
    }

    // Iterate using the iterator API
    while (comptimeIteratorHasNext(&it) && ctx->jmpFlags != jmpBreak) {
        ctx->jmpFlags = jmpNone;

        // Get next element
        AstNode *element = comptimeIteratorNext(&it);
        if (element == NULL)
            break;

        // Handle variadic parameters specially - wrap in resolved identifier
        if (isVariadic) {
            variable->varDecl.init = makeResolvedIdentifier(ctx->pool,
                                                            &element->loc,
                                                            element->_name,
                                                            0,
                                                            element,
                                                            NULL,
                                                            element->type);
        }
        else {
            variable->varDecl.init = element;
        }

        // Check condition if present
        if (node->forStmt.cond) {
            AstNode *cond = deepCloneAstNode(ctx->pool, node->forStmt.cond);
            cond->parentScope = node->parentScope;
            if (!evaluate(visitor, cond) || !evalBooleanCast(ctx, cond)) {
                node->tag = astError;
                ctx->jmpFlags = jmpNone;
                return false;
            }
            if (!cond->boolLiteral.value)
                continue;
        }

        // Evaluate body
        AstNode *body = deepCloneAstNode(ctx->pool, node->forStmt.body);
        body->parentScope = node->parentScope;

        const Type *type = evalType(ctx, body);
        if (type == NULL || typeIs(type, Error)) {
            node->tag = astError;
            ctx->jmpFlags = jmpNone;
            return false;
        }

        // Collect evaluated body statements
        body = getEvaluatedBody(node, body);
        while (body) {
            AstNode *tmp = body;
            body = body->next;
            if (isNoopNodeAfterEval(tmp))
                continue;
            tmp->parentScope = node->parentScope;
            tmp->next = NULL;
            insertAstNode(nodes, tmp);
        }
    }

    ctx->jmpFlags = jmpNone;
    return true;
}

void evalForStmt(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    ctx->jmpFlags = jmpNone;

    // Evaluate the range expression
    if (!evaluate(visitor, node->forStmt.range)) {
        node->tag = astError;
        return;
    }

    // Use unified iterator-based evaluation
    AstNodeList nodes = {NULL};
    if (!evalForStmtWithIterator(visitor, node, &nodes))
        return;

    // Replace this node with evaluated results
    if (nodes.first != NULL) {
        nodes.last->next = node->next;
        node->next = nodes.first;
    }
    node->tag = astNoop;
}
