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

static inline bool isIrVariableDecl(const IrNode *node)
{
    return node && (node->tag == irVarDecl || node->tag == irFuncParam ||
                    node->tag == irGlobalVar);
}

static void lowerResolvedDecl(LowerContext *ctx,
                              AstNode *node,
                              const AstNode *decl,
                              const IrType *irType)
{
    if (nodeIs(decl, ExternDecl) && decl->externDecl.func != NULL) {
        decl = decl->externDecl.func;
    }

    if (decl && nodeIs(decl, EnumOptionDecl)) {
        const IrType *enumIrType = decl->type ? lowerType(ctx, decl->type) : irType;
        cstring optName = decl->enumOption.name;
        cstring cName = (enumIrType && enumIrType->name)
                            ? makeStringf(ctx->strings, "%s_%s", enumIrType->name, optName)
                            : optName;
        node->codegen =
            makeIrGlobalRef(ctx->pool, &node->loc, cName, NULL, enumIrType);
        return;
    }

    if (decl && decl->ir) {
        if (decl->ir->tag == irAlias) {
            node->codegen = cloneIrNode(ctx->pool, decl->ir->alias.expr);
            return;
        }
        if (isIrVariableDecl(decl->ir)) {
            node->codegen = makeIrVarRef(ctx->pool, &node->loc, decl->ir);
            return;
        }
        if (decl->ir->tag == irFunction) {
            node->codegen = makeIrGlobalRef(ctx->pool,
                                            &node->loc,
                                            decl->ir->function.name,
                                            decl->ir,
                                            decl->ir->type);
            return;
        }
    }

    if (decl && decl->codegen) {
        node->codegen = decl->codegen;
        return;
    }

    if (decl) {
        if (nodeIs(decl, FuncDecl)) {
            cstring cName = getFunctionSymbolName(ctx, decl);
            node->codegen =
                makeIrGlobalRef(ctx->pool, &node->loc, cName, decl->ir, irType);
            return;
        }
        if (nodeIs(decl, FieldDecl) || hasFlag(node, Member) ||
            hasFlag(node, AddThis)) {
            IrNode *thisParam =
                ctx->currentFunc ? ctx->currentFunc->function.params : NULL;
            if (thisParam == NULL) {
                logError(
                    ctx->driver->L,
                    &node->loc,
                    "cannot access member field '{s}' without 'this' context",
                    (FormatArg[]){{.s = getDeclarationName(decl)}});
                return;
            }
            IrNode *thisRef = makeIrVarRef(ctx->pool, &node->loc, thisParam);
            cstring fieldName = getDeclarationName(decl);
            u32 fieldIdx = decl->structField.index;
            node->codegen = makeIrMember(
                ctx->pool, &node->loc, thisRef, fieldIdx, fieldName, irType);
            return;
        }

        cstring name = getDeclarationName(decl) ?: getNormalizedName(ctx, node);
        cstring cName = makeScopedSymbolName(ctx, decl, name);
        node->codegen =
            makeIrGlobalRef(ctx->pool, &node->loc, cName, decl->ir, irType);
        return;
    }

    cstring identName =
        nodeIs(node, Identifier) ? node->ident.value : getDeclarationName(node);
    if (ctx->currentFunc && identName) {
        for (IrNode *p = ctx->currentFunc->function.params; p; p = p->next) {
            if (p->funcParam.name == identName) {
                node->codegen = makeIrVarRef(ctx->pool, &node->loc, p);
                return;
            }
        }
    }

    logError(ctx->driver->L,
             &node->loc,
             "unresolved symbol '{s}' during IR lowering",
             (FormatArg[]){{.s = identName ?: "unknown"}});
}

void lowerIdentifier(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    lowerResolvedDecl(ctx, node, node->ident.resolvesTo, irType);
}

void lowerPath(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    const AstNode *lastElem =
        node->path.elements ? getLastAstNode(node->path.elements) : NULL;
    const AstNode *decl = lastElem ? lastElem->pathElement.resolvesTo : NULL;
    lowerResolvedDecl(ctx, node, decl, irType);
}

void lowerMemberExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    AstNode *mem = node->memberExpr.member;

    if (nodeIsMemberFunctionReference(node) && nodeIs(mem, Identifier)) {
        const AstNode *resolvesTo = mem->ident.resolvesTo;
        // Simplify wraps functions of other modules in an ExternDecl
        if (resolvesTo && nodeIs(resolvesTo, ExternDecl) &&
            resolvesTo->externDecl.func != NULL)
            resolvesTo = resolvesTo->externDecl.func;
        if (resolvesTo == NULL) {
            logError(ctx->driver->L,
                     &node->loc,
                     "unresolved symbol '{s}' during IR lowering",
                     (FormatArg[]){{.s = mem->ident.value ?: "unknown"}});
            return;
        }
        if (nodeIs(resolvesTo, FuncDecl)) {
            cstring cName = getFunctionSymbolName(ctx, resolvesTo);
            node->codegen = makeIrGlobalRef(ctx->pool,
                                            &node->loc,
                                            cName,
                                            resolvesTo->ir,
                                            lowerType(ctx, node->type));
            return;
        }
        cstring normName = getNormalizedName(ctx, node);
        cstring cName = makeScopedSymbolName(ctx, resolvesTo, normName);
        node->codegen = makeIrGlobalRef(
            ctx->pool, &node->loc, cName, NULL, lowerType(ctx, node->type));
        return;
    }

    if (nodeIs(mem, Identifier) && mem->ident.resolvesTo &&
        nodeIs(mem->ident.resolvesTo, EnumOptionDecl)) {
        const AstNode *opt = mem->ident.resolvesTo;
        const IrType *enumIrType = lowerType(ctx, opt->type ?: (node->type ?: NULL));
        cstring optName = opt->enumOption.name;
        cstring cName = (enumIrType && enumIrType->name)
                            ? makeStringf(ctx->strings, "%s_%s", enumIrType->name, optName)
                            : optName;
        node->codegen =
            makeIrGlobalRef(ctx->pool, &node->loc, cName, NULL, enumIrType);
        return;
    }

    IrNode *target = lowerFlattenedOperand(visitor, node->memberExpr.target);
    const Type *targetAstType = node->memberExpr.target->type;
    if (node->memberExpr.isRedirect && isOptionalType(targetAstType)) {
        const IrType *valType = target->type->strct.fields[1].type;
        target = makeIrMember(ctx->pool, &node->loc, target, 1, S_val, valType);
    }
    if (mem == NULL) {
        node->codegen = target;
        return;
    }
    u32 index = 0;
    cstring name = NULL;
    if (nodeIs(mem, IntegerLit)) {
        index = (u32)mem->intLiteral.uValue;
    }
    else {
        csAssert0(nodeIs(mem, Identifier));
        name = mem->ident.value;
    }

    if (node->memberExpr.isOptional && isOptionalType(node->type)) {
        const IrType *resIrType = lowerType(ctx, node->type);
        const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
        const IrType *memberValType = getIrOptionalTypeTarget(resIrType);

        cstring tempName =
            makeStringf(ctx->strings, "__opt%u", ctx->tempCounter++);
        IrNode *resVar = makeIrVarDecl(ctx->pool,
                                       &node->loc,
                                       flgTemporary,
                                       tempName,
                                       tempName,
                                       resIrType,
                                       ctx->currentScopeId);
        emitStmt(ctx, resVar);
        IrNode *resVarRef = makeIrVarRef(ctx->pool, &node->loc, resVar);

        IrNode *cond = NULL;
        IrNode *targetObj = NULL;
        if (isOptionalType(targetAstType)) {
            cond =
                makeIrMember(ctx->pool, &node->loc, target, 0, S_ok, boolType);
            targetObj = makeIrMember(ctx->pool,
                                     &node->loc,
                                     target,
                                     1,
                                     S_val,
                                     getIrOptionalTypeTarget(target->type));
        }
        else {
            IrNode *nullConst =
                makeIrNullConst(ctx->pool, &node->loc, target->type);
            cond = makeIrBinary(
                ctx->pool, &node->loc, opNe, target, nullConst, boolType);
            targetObj = target;
        }

        u32 optId = ctx->nextOptId++;
        IrNode *thenBB =
            createBlock(ctx, makeStringf(ctx->strings, "OPT%u_then", optId));
        IrNode *elseBB =
            createBlock(ctx, makeStringf(ctx->strings, "OPT%u_else", optId));
        IrNode *mergeBB =
            createBlock(ctx, makeStringf(ctx->strings, "OPT%u_merge", optId));

        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

        setBlock(ctx, thenBB);
        IrNode *memberVal = makeIrMember(
            ctx->pool, &node->loc, targetObj, index, name, memberValType);
        IrNode *okField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 0, S_ok, boolType);
        IrNode *valField = makeIrMember(
            ctx->pool, &node->loc, resVarRef, 1, S_val, memberValType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     okField,
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valField, memberVal));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, elseBB);
        IrNode *elseOkField =
            makeIrMember(ctx->pool,
                         &node->loc,
                         makeIrVarRef(ctx->pool, &node->loc, resVar),
                         0,
                         S_ok,
                         boolType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     elseOkField,
                     makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, resVar);
        return;
    }

    node->codegen = makeIrMember(
        ctx->pool, &node->loc, target, index, name, lowerType(ctx, node->type));
}

void lowerSliceExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *target = lowerFlattenedOperand(visitor, node->sliceExpr.target);
    const Type *targetAstType = node->sliceExpr.target->type;
    const IrType *sliceIrType = lowerType(ctx, node->type);
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *elemPtrType = sliceIrType->strct.fields[0].type;

    AstNode *rng = node->sliceExpr.range;
    IrNode *startNode = rng->rangeExpr.start
                            ? lowerFlattenedOperand(visitor, rng->rangeExpr.start)
                            : makeIrIntConst(ctx->pool, &node->loc, 0, false, u64Type);

    IrNode *endNode = NULL;
    if (rng->rangeExpr.end) {
        endNode = lowerFlattenedOperand(visitor, rng->rangeExpr.end);
    }
    else {
        if (isArrayType(targetAstType) && !isSliceType(targetAstType)) {
            endNode = makeIrIntConst(ctx->pool, &node->loc, targetAstType->array.len, false, u64Type);
        }
        else {
            endNode = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, target), 1, S_len, u64Type);
        }
    }

    IrNode *ptrVal = NULL;
    if (isSliceType(targetAstType)) {
        IrNode *basePtr = makeIrMember(ctx->pool, &node->loc, target, 0, S_ptr, elemPtrType);
        ptrVal = makeIrBinary(ctx->pool, &node->loc, opAdd, basePtr, startNode, elemPtrType);
    }
    else {
        IrNode *slot = makeIrIndex(ctx->pool, &node->loc, target, startNode, elemPtrType->pointer.pointee);
        ptrVal = makeIrAddrOf(ctx->pool, &node->loc, slot, elemPtrType);
    }

    IrNode *lenVal = NULL;
    if (startNode->tag == irIntConst && endNode->tag == irIntConst && !startNode->intConst.isNegative && !endNode->intConst.isNegative) {
        u64 s = (u64)startNode->intConst.value;
        u64 e = (u64)endNode->intConst.value;
        lenVal = makeIrIntConst(ctx->pool, &node->loc, (e >= s) ? (e - s) : 0, false, u64Type);
    }
    else {
        lenVal = makeIrBinary(ctx->pool, &node->loc, opSub, endNode, cloneIrNode(ctx->pool, startNode), u64Type);
    }

    ptrVal->next = lenVal;
    node->codegen = makeIrStructConst(ctx->pool, &node->loc, ptrVal, 2, sliceIrType);
}

void lowerIndexExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *target = lowerFlattenedOperand(visitor, node->indexExpr.target);
    IrNode *index = lowerFlattenedOperand(visitor, node->indexExpr.index);
    const Type *targetAstType = node->indexExpr.target->type;

    if (isSliceType(targetAstType)) {
        IrNode *ptrTarget = makeIrMember(ctx->pool,
                                         &node->loc,
                                         target,
                                         0,
                                         S_ptr,
                                         target->type->strct.fields[0].type);
        node->codegen = makeIrIndex(ctx->pool,
                                    &node->loc,
                                    ptrTarget,
                                    index,
                                    lowerType(ctx, node->type));
        return;
    }

    node->codegen = makeIrIndex(
        ctx->pool, &node->loc, target, index, lowerType(ctx, node->type));
}

void lowerPointerOf(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    // `&x` and `^x` only borrow, an rvalue lives in a local of the scope
    IrNode *operand = lowerFlattenedOperand(visitor, node->unaryExpr.operand);
    const Type *opType = node->unaryExpr.operand->type;
    const IrType *irType = lowerType(ctx, node->type);
    bool isRefOf = nodeIs(node, ReferenceOf);
    bool operandIsRef =
        isReferenceType(opType) ||
        (operand && operand->type &&
         operand->type->category == irTypeCatReference);
    // `&array` and `&class` are the value itself, `ptrof` takes the address
    bool isArrayRef = isRefOf && operand && operand->type &&
                      operand->type->tag == irTypeArray;
    if (operandIsRef || isArrayRef || (isRefOf && isClassType(opType))) {
        node->codegen = isRefOf
                            ? markUserWritten(
                                  node,
                                  makeIrCast(ctx->pool, &node->loc, operand, irType))
                            : operand;
        return;
    }
    node->codegen = markUserWritten(
        node, makeIrAddrOf(ctx->pool, &node->loc, operand, irType));
}
