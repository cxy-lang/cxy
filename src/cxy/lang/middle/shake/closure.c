//
// Created by Carter Mbotho on 2024-01-19.
//

#include "shake.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/visitor.h"

void shakeClosureExpr(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    astVisitManyNodes(visitor, node->closureExpr.captures);
    astVisitManyNodes(visitor, node->closureExpr.params);
    astVisit(visitor, node->closureExpr.ret);

    if (node->closureExpr.body && !nodeIs(node->closureExpr.body, BlockStmt)) {
        node->closureExpr.body = makeAstNode(
            ctx->pool,
            &node->closureExpr.body->loc,
            &(AstNode){
                .tag = astBlockStmt,
                .blockStmt = {.stmts = makeAstNode(
                                  ctx->pool,
                                  &node->closureExpr.body->loc,
                                  &(AstNode){.tag = astReturnStmt,
                                             .returnStmt.expr =
                                                 node->closureExpr.body})}});
    }
    astVisit(visitor, node->closureExpr.body);
}
