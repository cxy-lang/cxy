#include "internal.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
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

static inline bool isIrLValueOrConst(const IrNode *node)
{
    if (node == NULL)
        return false;
    switch (node->tag) {
    case irVarRef:
    case irGlobalRef:
    case irMember:
    case irIndex:
    case irDeref:
    case irAddrOf:
    case irCast:
    case irMove:
    case irNullConst:
    case irBoolConst:
    case irCharConst:
    case irIntConst:
    case irFloatConst:
    case irStringConst:
        return true;
    case irAlias:
        return isIrLValueOrConst(node->alias.expr);
    default:
        return false;
    }
}

bool isIrPureValueExpr(const IrNode *node)
{
    if (node == NULL)
        return false;
    if (isIrLValueOrConst(node))
        return true;
    if (node->tag == irUnary) {
        switch (node->unary.op) {
        case opPreInc:
        case opPostInc:
        case opPreDec:
        case opPostDec:
        case opDelete:
            return false;
        default:
            return isIrPureValueExpr(node->unary.operand);
        }
    }
    if (node->tag == irAlloca)
        return true;
    if (node->tag == irBinary)
        return isIrPureValueExpr(node->binary.lhs) && isIrPureValueExpr(node->binary.rhs);
    return false;
}

// Stores `val` in a fresh temporary and returns a reference to it.
// `borrowed` consumers only read the value, so an owned rvalue must be
// tracked (and dropped) as a regular local instead of an unowned temporary.
IrNode *flattenIrValue(LowerContext *ctx,
                       AstNode *node,
                       IrNode *val,
                       bool borrowed)
{
    cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    Flags flags = flgTemporary;
    if (isIrTypeDestructible(val->type))
        flags |= flgReferenceMembers;
    if (borrowed && node->type) {
        const Type *astType = resolveUnThisUnwrapType(node->type);
        if (astType && !isReferenceType(astType) && !isPointerType(astType) &&
            isDestructible(astType))
            flags = flgReferenceMembers;
    }
    IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    tempName,
                                    tempName,
                                    val->type,
                                    ctx->currentScopeId);
    emitStmt(ctx, tempVar);
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          val));
    return makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static IrNode *lowerFlattenedOperandEx(AstVisitor *visitor, AstNode *node, bool borrowed)
{
    if (node == NULL)
        return NULL;
    astVisit(visitor, node);
    IrNode *val = (IrNode *)node->codegen;
    if (val == NULL)
        return NULL;

    if (isIrLValueOrConst(val) || (isIrPureValueExpr(val) && !isIrTypeDestructible(val->type)))
        return val;

    return flattenIrValue(getAstVisitorContext(visitor), node, val, borrowed);
}

// An element of a struct, tuple or array literal. A call, or a nested literal
// that owns something, is hoisted into its own statement like a call argument,
// so the memory pass sees the moves and copies inside it.
IrNode *lowerCompositeElement(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node);
    IrNode *val = (IrNode *)node->codegen;
    if (val && ctx->currentFunc &&
        (val->tag == irCall ||
         (val->tag == irStructConst && isIrTypeDestructible(val->type))))
        return flattenIrValue(ctx, node, val, false);
    return val;
}

IrNode *lowerFlattenedOperand(AstVisitor *visitor, AstNode *node)
{
    return lowerFlattenedOperandEx(visitor, node, false);
}

IrNode *lowerBorrowedOperand(AstVisitor *visitor, AstNode *node)
{
    return lowerFlattenedOperandEx(visitor, node, true);
}

IrNode *lowerConditionOperand(LowerContext *ctx,
                              AstVisitor *visitor,
                              AstNode *condNode)
{
    const Type *astType = condNode->type;
    IrNode *cond = lowerBorrowedOperand(visitor, condNode);
    if (cond == NULL) {
        return makeIrBoolConst(ctx->pool,
                               &condNode->loc,
                               false,
                               getIrPrimitiveType(ctx->types, irTypeBool));
    }

    if (isOptionalType(astType)) {
        return makeIrMember(ctx->pool,
                            &condNode->loc,
                            cond,
                            0,
                            "ok",
                            getIrPrimitiveType(ctx->types, irTypeBool));
    }
    if (isClosureType(astType) && cond->type && cond->type->tag == irTypeStruct &&
        cond->type->strct.count >= 2) {
        const IrType *fnPtrType = cond->type->strct.fields[1].type;
        IrNode *fnMember =
            makeIrMember(ctx->pool, &condNode->loc, cond, 1, S_fn, fnPtrType);
        IrNode *nullConst =
            makeIrNullConst(ctx->pool, &condNode->loc, fnPtrType);
        return makeIrBinary(ctx->pool,
                            &condNode->loc,
                            opNe,
                            fnMember,
                            nullConst,
                            getIrPrimitiveType(ctx->types, irTypeBool));
    }

    // Integer, float, character and pointer conditions compare with zero
    IrNode *zero = NULL;
    if (isIntegerType(astType))
        zero = makeIrIntConst(
            ctx->pool, &condNode->loc, 0, false, cond->type);
    else if (isFloatType(astType))
        zero = makeIrFloatConst(ctx->pool, &condNode->loc, 0.0, cond->type);
    else if (isCharacterType(astType))
        zero = makeIrCharConst(ctx->pool, &condNode->loc, 0, cond->type);
    else if (isPointerType(astType))
        zero = makeIrNullConst(ctx->pool, &condNode->loc, cond->type);
    if (zero)
        return makeIrBinary(ctx->pool,
                            &condNode->loc,
                            opNe,
                            cond,
                            zero,
                            getIrPrimitiveType(ctx->types, irTypeBool));
    return cond;
}

void lowerSliceIntoTarget(LowerContext *ctx,
                          AstVisitor *visitor,
                          const AstNode *expr,
                          IrNode *targetLValue)
{
    // `[]` is the empty slice, all zero
    if (nodeIs(expr, ArrayExpr) && expr->arrayExpr.elements == NULL) {
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &expr->loc,
                              targetLValue,
                              makeIrNullConst(
                                  ctx->pool, &expr->loc, targetLValue->type)));
        return;
    }

    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const Type *exprAstType = expr->type;
    const IrType *elemIrType = targetLValue->type->strct.fields[0].type;
    u64 len =
        (exprAstType && isArrayType(exprAstType)) ? exprAstType->array.len : 0;

    IrNode *ptrTarget = makeIrMember(
        ctx->pool, &targetLValue->loc, targetLValue, 0, S_ptr, elemIrType);
    IrNode *lenTarget = makeIrMember(
        ctx->pool, &targetLValue->loc, targetLValue, 1, S_len, u64Type);

    if (nodeIs(expr, ArrayExpr)) {
        const IrType *fixedArrType = makeIrArrayType(ctx->types,
                                                     flgNone,
                                                     irTypeCatNone,
                                                     elemIrType->pointer.pointee,
                                                     len);
        cstring arrTempName =
            makeStringf(ctx->strings, "__arr%u", ctx->tempCounter++);
        IrNode *arrVar = makeIrVarDecl(ctx->pool,
                                       &expr->loc,
                                       flgTemporary,
                                       arrTempName,
                                       arrTempName,
                                       fixedArrType,
                                       ctx->currentScopeId);
        emitStmt(ctx, arrVar);

        u64 idx = 0;
        for (AstNode *elem = expr->arrayExpr.elements; elem;
             elem = elem->next, idx++) {
            IrNode *val = lowerFlattenedOperand(visitor, elem);
            IrNode *idxNode =
                makeIrIntConst(ctx->pool, &elem->loc, idx, false, u64Type);
            IrNode *elemSlot =
                makeIrIndex(ctx->pool,
                            &elem->loc,
                            makeIrVarRef(ctx->pool, &expr->loc, arrVar),
                            idxNode,
                            val->type);
            emitStmt(ctx, makeIrAssign(ctx->pool, &elem->loc, elemSlot, val));
        }

        IrNode *ptrVal = makeIrVarRef(ctx->pool, &expr->loc, arrVar);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, ptrTarget, ptrVal));
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &expr->loc,
                              lenTarget,
                              makeIrIntConst(
                                  ctx->pool, &expr->loc, len, false, u64Type)));
    }
    else if (nodeIs(expr, SliceExpr)) {
        astVisit(visitor, (AstNode *)expr);
        IrNode *sliceVal = (IrNode *)expr->codegen;
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, sliceVal));
    }
    else {
        IrNode *arrVal = lowerFlattenedOperand(visitor, (AstNode *)expr);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, ptrTarget, arrVal));
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &expr->loc,
                              lenTarget,
                              makeIrIntConst(
                                  ctx->pool, &expr->loc, len, false, u64Type)));
    }
}


void lowerOptionalIntoTarget(LowerContext *ctx,
                             AstVisitor *visitor,
                             const AstNode *expr,
                             IrNode *targetLValue)
{
    if (expr == NULL || nodeIs(expr, NullLit)) {
        IrNode *optVal = makeIrOptionalValue(ctx->pool, &targetLValue->loc, NULL, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &targetLValue->loc, targetLValue, optVal));
        return;
    }

    IrNode *val = lowerFlattenedOperand(visitor, (AstNode *)expr);
    IrNode *optVal = makeIrOptionalValue(ctx->pool, &expr->loc, val, targetLValue->type);
    emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, optVal));
}

void lowerTaggedUnionIntoTarget(LowerContext *ctx,
                                AstVisitor *visitor,
                                const AstNode *expr,
                                IrNode *targetLValue,
                                const Type *targetAstType)
{
    if (expr == NULL) {
        IrNode *uVal = makeIrUnionValue(ctx->pool, &targetLValue->loc, 0, NULL, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &targetLValue->loc, targetLValue, uVal));
        return;
    }

    if (nodeIs(expr, UnionValueExpr)) {
        u32 idx = expr->unionValue.idx;
        IrNode *innerVal = lowerFlattenedOperand(visitor, expr->unionValue.value);
        IrNode *uVal = makeIrUnionValue(ctx->pool, &expr->loc, idx, innerVal, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, uVal));
    }
    else if (expr->type && isUnionType(expr->type)) {
        astVisit(visitor, (AstNode *)expr);
        IrNode *rhsVal = (IrNode *)expr->codegen;
        if (rhsVal)
            emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, rhsVal));
    }
    else {
        u32 idx = findUnionTypeIndex(targetAstType, expr->type);
        if (idx == UINT32_MAX) {
            logError(ctx->driver->L, &expr->loc, "type '{t}' is not a member of union type '{t}'",
                     (FormatArg[]){{.t = expr->type}, {.t = targetAstType}});
            return;
        }
        IrNode *innerVal = lowerFlattenedOperand(visitor, (AstNode *)expr);
        IrNode *uVal = makeIrUnionValue(ctx->pool, &expr->loc, idx, innerVal, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, uVal));
    }
}

void lowerGroupExpr(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->groupExpr.expr);
    node->codegen = node->groupExpr.expr->codegen;
}

void lowerAliasExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *expr = lowerFlattenedOperand(visitor, node->aliasExpr.expr);
    cstring name = node->aliasExpr.name ?: getDeclarationName(node);
    node->codegen = expr;
    node->ir = makeIrAlias(ctx->pool, &node->loc, name, expr);
}
