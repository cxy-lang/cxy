//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"

static AstNode *getArrayDimension(AstNode *node)
{
    if (node == NULL)
        return node;
    const Type *type = node->type;
    switch (node->tag) {
    case astCastExpr:
        return node->castExpr.expr;
    case astTypedExpr:
        return node->typedExpr.expr;
    default:
        return node;
    }
}

void checkArrayType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *element = checkType(visitor, node->arrayType.elementType);
    if (typeIs(element, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    u64 size = UINT64_MAX;
    const Type *dim_ = checkType(visitor, node->arrayType.dim);
    if (typeIs(dim_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    if (typeIs(dim_, Literal)) {
        if (!nodeIs(dim_->literal.value, IntegerLit)) {
            logError(ctx->L,
                &node->loc,
                "expecting array dimension to be constant integral type "
                "at compile time",
                NULL);
            node->type = ERROR_TYPE(ctx);
            return;
        }
        clearAstBody(node->arrayType.dim);
        node->arrayType.dim->tag = astIntegerLit;
        node->arrayType.dim->intLiteral.value =
            dim_->literal.value->intLiteral.value;
    }

    AstNode *dim = getArrayDimension(node->arrayType.dim);
    if (dim == NULL) {
        node->type = makeArrayType(ctx->types, element, size);
        return;
    }

    if (!nodeIs(dim, IntegerLit)) {
        logError(ctx->L,
                 &node->loc,
                 "expecting array dimension to be constant integral type "
                 "at compile time",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return;
    }
    size = integerLiteralValue(dim);
    node->type = makeArrayType(ctx->types, element, size);
}

void checkArrayExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    const Type *elementType = NULL;
    bool isLiteral = true;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        const Type *type = checkType(visitor, elem);
        isLiteral = isLiteral && isLiteralExpr(elem);
        if (elementType == NULL) {
            elementType = type;
            continue;
        }

        if (!isTypeAssignableFrom(elementType, type)) {
            logError(ctx->L,
                     &elem->loc,
                     "inconsistent array types in array, expecting '{t}', "
                     "got '{t}'",
                     (FormatArg[]){{.t = elementType}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        // TODO maybe cast
        elem->type = elementType;
    }

    node->arrayExpr.isLiteral = isLiteral;
    if (elementType == NULL) {
        node->type = makeArrayType(ctx->types, makeAutoType(ctx->types), 0);
    }
    else {
        node->type = makeArrayType(ctx->types, elementType, count);
    }
}
