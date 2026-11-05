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

void lowerNullLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen =
        makeIrNullConst(ctx->pool, &node->loc, lowerType(ctx, node->type));
}

void lowerBoolLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrBoolConst(ctx->pool,
                                    &node->loc,
                                    node->boolLiteral.value,
                                    lowerType(ctx, node->type));
}

void lowerCharLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrCharConst(ctx->pool,
                                    &node->loc,
                                    node->charLiteral.value,
                                    lowerType(ctx, node->type));
}

void lowerIntegerLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrIntConst(ctx->pool,
                                   &node->loc,
                                   node->intLiteral.uValue,
                                   node->intLiteral.isNegative,
                                   lowerType(ctx, node->type));
}

void lowerFloatLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrFloatConst(ctx->pool,
                                     &node->loc,
                                     node->floatLiteral.value,
                                     lowerType(ctx, node->type));
}

void lowerStringLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrStringConst(ctx->pool,
                                      &node->loc,
                                      node->stringLiteral.value,
                                      lowerType(ctx, node->type));
}

static void lowerLogicalAnd(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
    IrNode *lhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.lhs);

    cstring condName =
        makeStringf(ctx->strings, "__cond%u", ctx->tempCounter++);
    IrNode *condVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flgNone,
                                    condName,
                                    condName,
                                    boolType,
                                    ctx->currentScopeId);
    markCompilerVar(condVar, irClassTransient);
    emitStmt(ctx, condVar);

    u32 logicId = ctx->nextLogicId++;
    IrNode *rhsBB =
        createBlock(ctx, makeStringf(ctx->strings, "AND%u_rhs", logicId));
    IrNode *falseBB =
        createBlock(ctx, makeStringf(ctx->strings, "AND%u_false", logicId));
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "AND%u_merge", logicId));

    emitStmt(ctx,
             makeIrBranchIf(ctx->pool, &node->loc, lhs, rhsBB, falseBB));

    setBlock(ctx, rhsBB);
    IrNode *rhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.rhs);
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, condVar),
                          rhs));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, falseBB);
    emitStmt(ctx,
             makeIrAssign(
                 ctx->pool,
                 &node->loc,
                 makeIrVarRef(ctx->pool, &node->loc, condVar),
                 makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, condVar);
}

static void lowerLogicalOr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
    IrNode *lhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.lhs);

    cstring condName =
        makeStringf(ctx->strings, "__cond%u", ctx->tempCounter++);
    IrNode *condVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flgNone,
                                    condName,
                                    condName,
                                    boolType,
                                    ctx->currentScopeId);
    markCompilerVar(condVar, irClassTransient);
    emitStmt(ctx, condVar);

    u32 logicId = ctx->nextLogicId++;
    IrNode *trueBB =
        createBlock(ctx, makeStringf(ctx->strings, "OR%u_true", logicId));
    IrNode *rhsBB =
        createBlock(ctx, makeStringf(ctx->strings, "OR%u_rhs", logicId));
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "OR%u_merge", logicId));

    emitStmt(ctx,
             makeIrBranchIf(ctx->pool, &node->loc, lhs, trueBB, rhsBB));

    setBlock(ctx, trueBB);
    emitStmt(ctx,
             makeIrAssign(
                 ctx->pool,
                 &node->loc,
                 makeIrVarRef(ctx->pool, &node->loc, condVar),
                 makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, rhsBB);
    IrNode *rhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.rhs);
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, condVar),
                          rhs));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, condVar);
}

static void lowerCoalesce(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *lhs = lowerFlattenedOperand(visitor, node->binaryExpr.lhs);
    const Type *lhsAstType = node->binaryExpr.lhs->type;
    const IrType *resIrType = lowerType(ctx, node->type);

    cstring tempName =
        makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    IrNode *resVar = makeIrVarDecl(ctx->pool,
                                   &node->loc,
                                   flgNone,
                                   tempName,
                                   tempName,
                                   resIrType,
                                   ctx->currentScopeId);
    markCompilerVar(resVar, irClassTransient);
    emitStmt(ctx, resVar);

    u32 optId = ctx->nextOptId++;
    IrNode *thenBB = createBlock(
        ctx, makeStringf(ctx->strings, "COALESCE%u_then", optId));
    IrNode *elseBB = createBlock(
        ctx, makeStringf(ctx->strings, "COALESCE%u_else", optId));
    IrNode *mergeBB = createBlock(
        ctx, makeStringf(ctx->strings, "COALESCE%u_merge", optId));

    IrNode *cond = NULL;
    IrNode *thenVal = NULL;
    if (isOptionalType(lhsAstType)) {
        cond = makeIrMember(ctx->pool,
                            &node->binaryExpr.lhs->loc,
                            lhs,
                            0,
                            S_ok,
                            getIrPrimitiveType(ctx->types, irTypeBool));
        thenVal = makeIrMember(ctx->pool,
                               &node->binaryExpr.lhs->loc,
                               lhs,
                               1,
                               S_val,
                               resIrType);
    }
    else {
        IrNode *nullConst = makeIrNullConst(
            ctx->pool, &node->binaryExpr.lhs->loc, lhs->type);
        cond = makeIrBinary(ctx->pool,
                            &node->binaryExpr.lhs->loc,
                            opNe,
                            lhs,
                            nullConst,
                            getIrPrimitiveType(ctx->types, irTypeBool));
        thenVal = lhs;
    }

    emitStmt(ctx,
             makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, resVar),
                          thenVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    astVisit(visitor, node->binaryExpr.rhs);
    IrNode *rhsVal = (IrNode *)node->binaryExpr.rhs->codegen;
    if (rhsVal)
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, resVar),
                              rhsVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, resVar);
}

// `x is T` on a union is a tag check. Returns false when `x` is not a union.
static bool lowerUnionIs(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *sumType =
        stripPointerOrReferenceOnce(node->binaryExpr.lhs->type, NULL);
    if (isUnionType(sumType)) {
        const Type *targetType = node->binaryExpr.rhs->type;
        if (typeIs(targetType, Info))
            targetType = targetType->info.target;
        u32 idx = findUnionTypeIndex(sumType, targetType);
        if (idx == UINT32_MAX) {
            node->codegen =
                makeIrBoolConst(ctx->pool,
                                &node->loc,
                                false,
                                getIrPrimitiveType(ctx->types, irTypeBool));
            return true;
        }
        IrNode *lhs = lowerFlattenedOperand(visitor, node->binaryExpr.lhs);
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *tagMember =
            makeIrMember(ctx->pool, &node->loc, lhs, 0, "tag", u32Type);
        IrNode *idxConst =
            makeIrIntConst(ctx->pool, &node->loc, idx, false, u32Type);
        node->codegen =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opEq,
                         tagMember,
                         idxConst,
                         getIrPrimitiveType(ctx->types, irTypeBool));
        return true;
    }
    return false;
}

// `f == null` and `null == f` for a closure compare its function pointer
static bool lowerClosureNullCompare(LowerContext *ctx,
                                    AstNode *node,
                                    IrNode *lhs,
                                    IrNode *rhs)
{
    Operator op = node->binaryExpr.op;
    const Type *lhsAstType = node->binaryExpr.lhs->type;
    const Type *rhsAstType = node->binaryExpr.rhs->type;
    if (isClosureType(lhsAstType) && (rhsAstType == NULL || typeIs(rhsAstType, Null) || nodeIs(node->binaryExpr.rhs, NullLit))) {
        const IrType *fnPtrType = lhs->type && lhs->type->tag == irTypeStruct && lhs->type->strct.count >= 2
                                      ? lhs->type->strct.fields[1].type
                                      : makeIrPointerType(ctx->types, flgNone, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeVoid));
        IrNode *fnMember = makeIrMember(ctx->pool, &node->binaryExpr.lhs->loc, lhs, 1, S_fn, fnPtrType);
        IrNode *nullConst = makeIrNullConst(ctx->pool, &node->binaryExpr.rhs->loc, fnPtrType);
        node->codegen = makeIrBinary(ctx->pool, &node->loc, op, fnMember, nullConst, getIrPrimitiveType(ctx->types, irTypeBool));
        return true;
    }
    if (isClosureType(rhsAstType) && (lhsAstType == NULL || typeIs(lhsAstType, Null) || nodeIs(node->binaryExpr.lhs, NullLit))) {
        const IrType *fnPtrType = rhs->type && rhs->type->tag == irTypeStruct && rhs->type->strct.count >= 2
                                      ? rhs->type->strct.fields[1].type
                                      : makeIrPointerType(ctx->types, flgNone, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeVoid));
        IrNode *fnMember = makeIrMember(ctx->pool, &node->binaryExpr.rhs->loc, rhs, 1, S_fn, fnPtrType);
        IrNode *nullConst = makeIrNullConst(ctx->pool, &node->binaryExpr.lhs->loc, fnPtrType);
        node->codegen = makeIrBinary(ctx->pool, &node->loc, op, nullConst, fnMember, getIrPrimitiveType(ctx->types, irTypeBool));
        return true;
    }
    return false;
}

void lowerBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->binaryExpr.op;

    switch (op) {
    case opLAnd:
        lowerLogicalAnd(visitor, node);
        return;
    case opLOr:
        lowerLogicalOr(visitor, node);
        return;
    case opCoalesce:
        lowerCoalesce(visitor, node);
        return;
    case opIs:
        if (lowerUnionIs(visitor, node))
            return;
        break;
    default:
        break;
    }

    IrNode *lhs = lowerFlattenedOperand(visitor, node->binaryExpr.lhs);
    IrNode *rhs = lowerFlattenedOperand(visitor, node->binaryExpr.rhs);

    if ((op == opEq || op == opNe) &&
        lowerClosureNullCompare(ctx, node, lhs, rhs))
        return;

    if (isReferenceType(node->binaryExpr.lhs->type) &&
        !isClassType(stripReference(node->binaryExpr.lhs->type))) {
        const IrType *valType =
            lowerType(ctx, stripReference(node->binaryExpr.lhs->type));
        lhs = makeIrDeref(ctx->pool, &node->binaryExpr.lhs->loc, lhs, valType);
    }
    if (isReferenceType(node->binaryExpr.rhs->type) &&
        !isClassType(stripReference(node->binaryExpr.rhs->type))) {
        const IrType *valType =
            lowerType(ctx, stripReference(node->binaryExpr.rhs->type));
        rhs = makeIrDeref(ctx->pool, &node->binaryExpr.rhs->loc, rhs, valType);
    }
    node->codegen = makeIrBinary(
        ctx->pool, &node->loc, op, lhs, rhs, lowerType(ctx, node->type));
}

// `&&`, `@copy` and `&` from the source, not the ones the compiler inserts
IrNode *markUserWritten(const AstNode *ast, IrNode *ir)
{
    if (ir && ast->userWritten)
        irSetInfo(ir, irInfoUserWritten);
    return ir;
}

void lowerUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    bool borrows =
        node->unaryExpr.op == opPtrof || node->unaryExpr.op == opRefof;
    IrNode *operand = lowerFlattenedOperand(visitor, node->unaryExpr.operand);
    const IrType *irType = lowerType(ctx, node->type);
    if (borrows) {
        const Type *opType = node->unaryExpr.operand->type;
        bool isRefOf = node->unaryExpr.op == opRefof;
        bool operandIsRef =
            isReferenceType(opType) ||
            (operand && operand->type &&
             operand->type->category == irTypeCatReference);
        // `&array` and `&class` are the value itself, `ptrof` takes the address
        bool isArrayRef = isRefOf && operand && operand->type &&
                          operand->type->tag == irTypeArray;
        if (operandIsRef || isArrayRef || (isRefOf && isClassType(opType))) {
            node->codegen = (node->unaryExpr.op == opRefof)
                                ? markUserWritten(
                                      node,
                                      makeIrCast(ctx->pool, &node->loc, operand, irType))
                                : operand;
        }
        else
            node->codegen = markUserWritten(
                node, makeIrAddrOf(ctx->pool, &node->loc, operand, irType));
    }
    else if (node->unaryExpr.op == opMove)
        node->codegen =
            markUserWritten(node, makeIrMove(ctx->pool, &node->loc, operand));
    else if (node->unaryExpr.op == opDelete) {
        node->codegen = makeIrDrop(ctx->pool, &node->loc, operand);
    }
    else if (node->unaryExpr.op == opDeref) {
        const Type *opType = node->unaryExpr.operand->type;
        if (isOptionalType(opType))
            node->codegen =
                makeIrMember(ctx->pool, &node->loc, operand, 1, S_val, irType);
        else
            node->codegen = makeIrDeref(ctx->pool, &node->loc, operand, irType);
    }
    else if (node->unaryExpr.op == opNot) {
        const Type *opType = node->unaryExpr.operand->type;
        if (isOptionalType(opType)) {
            IrNode *okMember =
                makeIrMember(ctx->pool,
                             &node->loc,
                             operand,
                             0,
                             S_ok,
                             getIrPrimitiveType(ctx->types, irTypeBool));
            node->codegen =
                makeIrUnary(ctx->pool, &node->loc, opNot, okMember, irType);
        }
        else {
            if (isReferenceType(opType) &&
                !isClassType(stripReference(opType))) {
                const IrType *valType = lowerType(ctx, stripReference(opType));
                operand = makeIrDeref(
                    ctx->pool, &node->unaryExpr.operand->loc, operand, valType);
            }
            node->codegen = makeIrUnary(
                ctx->pool, &node->loc, node->unaryExpr.op, operand, irType);
        }
    }
    else {
        const Type *opType = node->unaryExpr.operand->type;
        if (isReferenceType(opType) &&
            !isClassType(stripReference(opType))) {
            const IrType *valType = lowerType(ctx, stripReference(opType));
            operand = makeIrDeref(
                ctx->pool, &node->unaryExpr.operand->loc, operand, valType);
        }
        node->codegen = makeIrUnary(
            ctx->pool, &node->loc, node->unaryExpr.op, operand, irType);
    }
}

static void lowerTypeCoercion(AstVisitor *visitor,
                              AstNode *node,
                              AstNode *subExpr)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *targetType = node->type;
    const Type *srcType = subExpr->type;

    if (isOptionalType(targetType)) {
        const IrType *optIrType = lowerType(ctx, targetType);
        if (subExpr == NULL || nodeIs(subExpr, NullLit) || typeIs(srcType, Null)) {
            node->codegen = makeIrOptionalValue(ctx->pool, &node->loc, NULL, optIrType);
            return;
        }
        if (!isOptionalType(srcType)) {
            IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
            node->codegen = makeIrOptionalValue(ctx->pool, &node->loc, expr, optIrType);
            return;
        }
    }
    else if (isSliceType(targetType) && isArrayType(srcType)) {
        const IrType *sliceIrType = lowerType(ctx, targetType);
        cstring tempName =
            makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
        IrNode *sliceTemp = makeIrVarDecl(ctx->pool,
                                          &node->loc,
                                          flgNone,
                                          tempName,
                                          tempName,
                                          sliceIrType,
                                          ctx->currentScopeId);
        emitStmt(ctx, sliceTemp);
        lowerSliceIntoTarget(ctx,
                             visitor,
                             subExpr,
                             makeIrVarRef(ctx->pool, &node->loc, sliceTemp));
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, sliceTemp);
        return;
    }

    if (isOptionalType(srcType) && isBooleanType(targetType)) {
        IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
        node->codegen =
            makeIrMember(ctx->pool,
                         &node->loc,
                         expr,
                         0,
                         S_ok,
                         getIrPrimitiveType(ctx->types, irTypeBool));
        return;
    }

    if (isUnionType(srcType)) {
        u32 idx = findUnionTypeIndex(srcType, stripReference(targetType));
        if (idx != UINT32_MAX) {
            IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
            const IrType *unionIrType = expr->type;
            if (unionIrType && unionIrType->tag == irTypeStruct &&
                unionIrType->strct.count == 2) {
                if (!hasFlag(node, BitUnionCast)) {
                    const IrType *u32Type =
                        getIrPrimitiveType(ctx->types, irTypeU32);
                    const IrType *boolType =
                        getIrPrimitiveType(ctx->types, irTypeBool);
                    const IrType *u64Type =
                        getIrPrimitiveType(ctx->types, irTypeU64);
                    const IrType *strType = makeIrPointerType(
                        ctx->types,
                        flgNone, irTypeCatNone,
                        getIrPrimitiveType(ctx->types, irTypeChar));
                    const IrType *voidType =
                        getIrPrimitiveType(ctx->types, irTypeVoid);

                    IrNode *tagMember = makeIrMember(
                        ctx->pool, &node->loc, expr, 0, S_tag, u32Type);
                    IrNode *idxConst = makeIrIntConst(
                        ctx->pool, &node->loc, idx, false, u32Type);
                    IrNode *cond = makeIrBinary(ctx->pool,
                                                &node->loc,
                                                opEq,
                                                tagMember,
                                                idxConst,
                                                boolType);

                    IrNode *file = makeIrStringConst(ctx->pool,
                                                     &node->loc,
                                                     node->loc.fileName ?: "",
                                                     strType);
                    IrNode *line = makeIrIntConst(ctx->pool,
                                                  &node->loc,
                                                  node->loc.begin.row,
                                                  false,
                                                  u64Type);
                    IrNode *col = makeIrIntConst(ctx->pool,
                                                 &node->loc,
                                                 node->loc.begin.col,
                                                 false,
                                                 u64Type);
                    cond->next = file;
                    file->next = line;
                    line->next = col;

                    const IrType *params[] = {
                        boolType, strType, u64Type, u64Type};
                    const IrType *assertFuncType = makeIrFuncType(
                        ctx->types, flgNone, params, 4, voidType, false);
                    IrNode *callee = makeIrGlobalRef(ctx->pool,
                                                     &node->loc,
                                                     S___cxy_assert,
                                                     NULL,
                                                     assertFuncType);
                    IrNode *assertCall = makeIrCall(
                        ctx->pool, &node->loc, callee, cond, 4, voidType);
                    emitStmt(ctx, assertCall);
                }

                const IrType *payloadUnionType =
                    unionIrType->strct.fields[1].type;
                IrNode *valMember = makeIrMember(
                    ctx->pool, &node->loc, expr, 1, S_val, payloadUnionType);
                cstring fieldName = makeStringf(ctx->strings, "_%u", idx);
                const IrType *memberIrType =
                    (payloadUnionType && idx < payloadUnionType->strct.count)
                        ? payloadUnionType->strct.fields[idx].type
                        : lowerType(ctx, targetType);
                node->codegen = makeIrMember(ctx->pool,
                                             &node->loc,
                                             valMember,
                                             idx,
                                             fieldName,
                                             memberIrType);
                return;
            }
        }
    }

    IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
    node->codegen =
        makeIrCast(ctx->pool, &node->loc, expr, lowerType(ctx, node->type));
}

void lowerCastExpr(AstVisitor *visitor, AstNode *node)
{
    lowerTypeCoercion(visitor, node, node->castExpr.expr);
}

void lowerTypedExpr(AstVisitor *visitor, AstNode *node)
{
    lowerTypeCoercion(visitor, node, node->typedExpr.expr);
}

static inline Operator assignOpToBinaryOp(Operator op)
{
    switch (op) {
#define f(O, ...)                                                              \
    case op##O##Equal:                                                         \
        return op##O;
        AST_ASSIGN_EXPR_LIST(f)
#undef f
    default:
        return opInvalid;
    }
}

void lowerAssignExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *lhs = lowerFlattenedOperand(visitor, node->assignExpr.lhs);
    AstNode *rhsNode = node->assignExpr.rhs;
    const Type *lhsAstType = node->assignExpr.lhs->type;
    const Type *rhsAstType = rhsNode->type;

    if (isReferenceType(lhsAstType) &&
        !isClassType(stripReference(lhsAstType)) &&
        !isReferenceType(rhsAstType) &&
        !isClassType(stripReference(rhsAstType))) {
        lhs = makeIrDeref(ctx->pool,
                          &node->assignExpr.lhs->loc,
                          lhs,
                          lowerType(ctx, stripReference(lhsAstType)));
    }

    if (isSliceType(lhsAstType) && !isSliceType(rhsAstType)) {
        lowerSliceIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else if (isOptionalType(lhsAstType) && !isOptionalType(rhsAstType)) {
        lowerOptionalIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else if (typeIs(lhsAstType, Union) &&
             (nodeIs(rhsNode, UnionValueExpr) ||
              (rhsAstType && !typeIs(rhsAstType, Union)))) {
        lowerTaggedUnionIntoTarget(ctx, visitor, rhsNode, lhs, lhsAstType);
    }
    else if (isBackendCopyExpr(rhsNode)) {
        IrNode *src =
            lowerFlattenedOperand(visitor, rhsNode->backendCallExpr.args);
        emitStmt(ctx,
                 markUserWritten(rhsNode,
                                 makeIrCopy(ctx->pool, &node->loc, src, lhs)));
    }
    else {
        astVisit(visitor, rhsNode);
        IrNode *rhs = (IrNode *)rhsNode->codegen;
        if (rhs) {
            Operator op = node->assignExpr.op;
            if (op != opAssign && op != opInvalid) {
                rhs = makeIrBinary(ctx->pool,
                                   &node->loc,
                                   op,
                                   cloneIrNode(ctx->pool, lhs),
                                   rhs,
                                   lowerType(ctx, lhsAstType));
            }
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, lhs, rhs));
        }
    }
}
