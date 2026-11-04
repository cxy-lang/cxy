#include "internal.h"

#include "core/alloc.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lowerArrayExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *arrType = lowerType(ctx, node->type);
    IrNode *elemsFirst = NULL, *elemsLast = NULL;
    u64 count = 0;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        IrNode *elemVal = lowerCompositeElement(visitor, elem);
        if (elemsLast) {
            elemsLast->next = elemVal;
            elemsLast = elemVal;
        }
        else {
            elemsFirst = elemsLast = elemVal;
        }
    }
    node->codegen =
        makeIrArrayConst(ctx->pool, &node->loc, elemsFirst, count, arrType);
}

void lowerTupleExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *tupType = lowerType(ctx, node->type);
    const Type *tupAstType = resolveUnThisUnwrapType(node->type);
    IrNode *elemsFirst = NULL, *elemsLast = NULL;
    u64 count = 0;
    for (AstNode *elem = node->tupleExpr.elements; elem;
         elem = elem->next, count++) {
        IrNode *elemVal = lowerCompositeElement(visitor, elem);
        // `null` or a plain `T` stored in a `T?` member becomes an optional
        const Type *member = typeIs(tupAstType, Tuple) &&
                                     count < tupAstType->tuple.count
                                 ? tupAstType->tuple.members[count]
                                 : NULL;
        if (member && elemVal && isOptionalType(member) &&
            (nodeIs(elem, NullLit) || !isOptionalType(elem->type))) {
            elemVal = makeIrOptionalValue(
                ctx->pool,
                &elem->loc,
                nodeIs(elem, NullLit) ? NULL : elemVal,
                lowerType(ctx, member));
        }
        // see lowerStructExpr, hoisted so the memory pass sees the value's move
        if (elemVal && elemVal->tag == irOptionalValue &&
            elemVal->optionalValue.hasValue && ctx->currentFunc &&
            isIrTypeDestructible(elemVal->type))
            elemVal = flattenIrValue(ctx, elem, elemVal, false);
        if (elemsLast) {
            elemsLast->next = elemVal;
            elemsLast = elemVal;
        }
        else {
            elemsFirst = elemsLast = elemVal;
        }
    }
    node->codegen =
        makeIrStructConst(ctx->pool, &node->loc, elemsFirst, count, tupType);
}

void lowerStructExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *strctType = lowerType(ctx, node->type);
    IrNode *fieldsFirst = NULL, *fieldsLast = NULL;
    u64 count = 0;
    for (AstNode *f = node->structExpr.fields; f; f = f->next) {
        IrNode *fVal = lowerCompositeElement(visitor, f->fieldExpr.value);
        cstring name = f->fieldExpr.name;
        u32 index = UINT32_MAX;
        if (f->fieldExpr.structField && nodeIs(f->fieldExpr.structField, FieldDecl)) {
            index = f->fieldExpr.structField->structField.index;
            if (name == NULL)
                name = f->fieldExpr.structField->structField.name;
        }
        count++;
        // `T` assigned to a `T?` field is wrapped into the optional here
        const Type *fieldAstType = resolveUnThisUnwrapType(f->type);
        const Type *valueAstType = f->fieldExpr.value->type;
        if (fVal && isOptionalType(fieldAstType) &&
            !nodeIs(f->fieldExpr.value, NullLit) &&
            !isOptionalType(valueAstType)) {
            fVal = makeIrOptionalValue(
                ctx->pool, &f->loc, fVal, lowerType(ctx, fieldAstType));
        }
        // an optional holding an owned value gets its own statement, so the
        // memory pass sees the move or copy of the value it wraps
        if (fVal && fVal->tag == irOptionalValue &&
            fVal->optionalValue.hasValue && ctx->currentFunc &&
            isIrTypeDestructible(fVal->type))
            fVal = flattenIrValue(ctx, f->fieldExpr.value, fVal, false);
        const IrType *fType = fVal ? fVal->type : NULL;
        IrNode *fieldNode = makeIrFieldConst(ctx->pool, &f->loc, name, index, fVal, fType);
        if (fieldsLast) {
            fieldsLast->next = fieldNode;
            fieldsLast = fieldNode;
        }
        else {
            fieldsFirst = fieldsLast = fieldNode;
        }
    }
    node->codegen =
        makeIrStructConst(ctx->pool, &node->loc, fieldsFirst, count, strctType);
}

void lowerUnionValueExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *unionIrType = lowerType(ctx, node->type);
    IrNode *innerVal = lowerFlattenedOperand(visitor, node->unionValue.value);
    node->codegen = makeIrUnionValue(
        ctx->pool, &node->loc, node->unionValue.idx, innerVal, unionIrType);
}

