#include "lower.h"
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
#include "lang/middle/mangle.h"
#include "lang/middle/memory/memory.h"
#include "lang/middle/builtins.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    astVisit(visitor, node->ternaryExpr.cond);
    IrNode *cond = (IrNode *)node->ternaryExpr.cond->codegen;

    cstring tempName =
        makeStringf(ctx->strings, "__ternary_%u", ctx->tempCounter++);
    Flags flags = flgTemporary;
    if (isIrTypeDestructible(irType))
        flags |= flgReferenceMembers;
    IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    tempName,
                                    tempName,
                                    irType,
                                    ctx->currentScopeId);
    emitStmt(ctx, tempVar);

    u32 ternaryId = ctx->nextTernaryId++;
    IrNode *thenBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_then", ternaryId));
    IrNode *elseBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_else", ternaryId));
    IrNode *mergeBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_merge", ternaryId));

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ternaryExpr.body);
    IrNode *thenVal = (IrNode *)node->ternaryExpr.body->codegen;
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          thenVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    astVisit(visitor, node->ternaryExpr.otherwise);
    IrNode *elseVal = (IrNode *)node->ternaryExpr.otherwise->codegen;
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          elseVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
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

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
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
        // see visitStructExpr, hoisted so the memory pass sees the value's move
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

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
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
        const Type *fieldAstType =
            f->type ? resolveUnThisUnwrapType(f->type) : NULL;
        const Type *valueAstType = f->fieldExpr.value->type;
        if (fVal && fieldAstType && isOptionalType(fieldAstType) &&
            !nodeIs(f->fieldExpr.value, NullLit) &&
            !(valueAstType && isOptionalType(valueAstType))) {
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

static void visitClosureValue(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *closureType =
        lowerType(ctx, node->closureValue.canonicalType ?: node->type);

    IrNode *envVal = NULL;
    IrNode *dtorVal = NULL;
    if (node->closureValue.env && !nodeIs(node->closureValue.env, NullLit)) {
        IrNode *envStruct =
            lowerFlattenedOperand(visitor, node->closureValue.env);

        if (node->type && node->type->closure.isEscaping) {
            const IrType *envPtrType =
                makeIrPointerType(ctx->types, flgNone, irTypeCatNone, envStruct->type);
            IrNode *allocNode =
                makeIrAlloc(ctx->pool, &node->loc, envStruct->type);
            cstring tempName =
                makeStringf(ctx->strings, "__env%u", ctx->tempCounter++);
            IrNode *envVar = makeIrVarDecl(ctx->pool,
                                           &node->loc,
                                           flgTemporary,
                                           tempName,
                                           tempName,
                                           envPtrType,
                                           ctx->currentScopeId);
            emitStmt(ctx, envVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, envVar),
                                  allocNode));
            emitStmt(
                ctx,
                makeIrAssign(
                    ctx->pool,
                    &node->loc,
                    makeIrDeref(ctx->pool,
                                &node->loc,
                                makeIrVarRef(ctx->pool, &node->loc, envVar),
                                envStruct->type),
                    envStruct));
            envVal = makeIrVarRef(ctx->pool, &node->loc, envVar);
        }
        else {
            const IrType *envStructType = lowerType(ctx, node->closureValue.env->type);
            cstring envVarName =
                makeStringf(ctx->strings, "__env%u", ctx->tempCounter++);
            Flags envFlags = flgNone;
            if (isIrTypeDestructible(envStructType))
                envFlags |= flgReferenceMembers;
            IrNode *envVar = makeIrVarDecl(ctx->pool,
                                           &node->loc,
                                           envFlags,
                                           envVarName,
                                           envVarName,
                                           envStructType,
                                           ctx->currentScopeId);
            emitStmt(ctx, envVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, envVar),
                                  envStruct));
            envVal = makeIrAddrOf(
                ctx->pool,
                &node->loc,
                makeIrVarRef(ctx->pool, &node->loc, envVar),
                makeIrPointerType(ctx->types, flgNone, irTypeCatNone, envStructType));
        }
    }

    IrNode *fnVal = lowerFlattenedOperand(visitor, node->closureValue.func);

    node->codegen = makeIrClosureValue(
        ctx->pool, &node->loc, envVal, fnVal, dtorVal, closureType);
}

static void visitUnionValueExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *unionIrType = lowerType(ctx, node->type);
    IrNode *innerVal = lowerFlattenedOperand(visitor, node->unionValue.value);
    node->codegen = makeIrUnionValue(
        ctx->pool, &node->loc, node->unionValue.idx, innerVal, unionIrType);
}

static void visitBackendCall(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    BackendFuncId func = node->backendCallExpr.func;
    AstNode *args = node->backendCallExpr.args;

    switch (func) {
    case bfiMemAlloc: {
        AstNode *typeArg = args;
        const Type *allocType = typeArg->type;
        if (typeIs(allocType, Pointer))
            allocType = allocType->pointer.pointed;
        const IrType *elemIrType = lowerType(ctx, allocType);
        node->codegen = makeIrAlloc(ctx->pool, &node->loc, elemIrType);
        break;
    }
    case bfiZeromem: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrZeromem(ctx->pool, &node->loc, target);
        break;
    }
    case bfiSizeOf: {
        const IrType *targetType = lowerType(ctx, args->type);
        node->codegen = makeIrSizeof(ctx->pool,
                                     &node->loc,
                                     targetType,
                                     getIrPrimitiveType(ctx->types, irTypeU64));
        break;
    }
    case bfiLen: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
        const Type *argAstType = stripReference(resolveUnThisUnwrapType(args->type));
        if (isSliceType(argAstType)) {
            node->codegen = makeIrMember(
                ctx->pool, &node->loc, target, 1, S_len, u64Type);
        }
        else if (isArrayType(argAstType)) {
            node->codegen = makeIrIntConst(
                ctx->pool, &node->loc, argAstType->array.len, false, u64Type);
        }
        else {
            const IrType *charPtrType = makeIrPointerType(
                ctx->types, flgConst, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeChar));
            const IrType *fnType = makeIrFuncType(
                ctx->types, flgNone, (const IrType *[]){charPtrType}, 1, u64Type, false);
            IrNode *fn = makeIrGlobalRef(ctx->pool, &node->loc, "strlen", NULL, fnType);
            node->codegen = makeIrCall(ctx->pool, &node->loc, fn, target, 1, u64Type);
        }
        break;
    }
    case bfiSlice: {
        IrNode *ptrArg = lowerFlattenedOperand(visitor, args);
        IrNode *lenArg = lowerFlattenedOperand(visitor, args->next);
        const IrType *sliceType = lowerType(ctx, node->type);
        ptrArg->next = lenArg;
        node->codegen = makeIrStructConst(ctx->pool, &node->loc, ptrArg, 2, sliceType);
        break;
    }
    case bfiAlloca: {
        const Type *allocType = args ? args->type : NULL;
        if (allocType == NULL && node->type) {
            allocType = isPointerType(node->type) ? node->type->pointer.pointed : node->type;
        }
        else if (allocType && isPointerType(allocType)) {
            allocType = allocType->pointer.pointed;
        }
        const IrType *elemType = lowerType(ctx, allocType);
        IrNode *count =
            (args && args->next) ? lowerFlattenedOperand(visitor, args->next) : NULL;
        const IrType *ptrType =
            node->type ? lowerType(ctx, node->type)
                       : makeIrPointerType(ctx->types, flgNone, irTypeCatNone, elemType);
        node->codegen = makeIrAlloca(ctx->pool, &node->loc, elemType, count, ptrType);
        break;
    }
    case bfiAtomicLoad: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *order = lowerFlattenedOperand(visitor, args->next);
        ptr->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn =
            makeIrGlobalRef(ctx->pool, &node->loc, "__atomic_load_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 2, retType);
        break;
    }
    case bfiAtomicStore: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *val = lowerFlattenedOperand(visitor, args->next);
        IrNode *order = lowerFlattenedOperand(visitor, args->next->next);
        ptr->next = val;
        val->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn =
            makeIrGlobalRef(ctx->pool, &node->loc, "__atomic_store_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 3, retType);
        break;
    }
    case bfiAtomicExchange: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *val = lowerFlattenedOperand(visitor, args->next);
        IrNode *order = lowerFlattenedOperand(visitor, args->next->next);
        ptr->next = val;
        val->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_exchange_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 3, retType);
        break;
    }
    case bfiAtomicCompareExchange: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *expected = lowerFlattenedOperand(visitor, args->next);
        IrNode *desired = lowerFlattenedOperand(visitor, args->next->next);
        IrNode *weak = lowerFlattenedOperand(visitor, args->next->next->next);
        IrNode *succOrder =
            lowerFlattenedOperand(visitor, args->next->next->next->next);
        IrNode *failOrder =
            lowerFlattenedOperand(visitor, args->next->next->next->next->next);
        ptr->next = expected;
        expected->next = desired;
        desired->next = weak;
        weak->next = succOrder;
        succOrder->next = failOrder;
        failOrder->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_compare_exchange_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 6, retType);
        break;
    }
    case bfiAtomicFetchAdd:
    case bfiAtomicFetchSub:
    case bfiAtomicFetchOr:
    case bfiAtomicFetchAnd:
    case bfiAtomicFetchXor: {
        cstring fnName = "__atomic_fetch_add";
        if (func == bfiAtomicFetchSub)
            fnName = "__atomic_fetch_sub";
        else if (func == bfiAtomicFetchOr)
            fnName = "__atomic_fetch_or";
        else if (func == bfiAtomicFetchAnd)
            fnName = "__atomic_fetch_and";
        else if (func == bfiAtomicFetchXor)
            fnName = "__atomic_fetch_xor";

        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *val = lowerFlattenedOperand(visitor, args->next);
        IrNode *order = lowerFlattenedOperand(visitor, args->next->next);
        ptr->next = val;
        val->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(ctx->pool, &node->loc, fnName, NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 3, retType);
        break;
    }
    case bfiAtomicThreadFence: {
        IrNode *order = lowerFlattenedOperand(visitor, args);
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_thread_fence", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, order, 1, retType);
        break;
    }
    case bfiAtomicSignalFence: {
        IrNode *order = lowerFlattenedOperand(visitor, args);
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_signal_fence", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, order, 1, retType);
        break;
    }
    case bfiCopy: {
        IrNode *src = lowerFlattenedOperand(visitor, args);
        if (args && args->next) {
            IrNode *dst = lowerFlattenedOperand(visitor, args->next);
            node->codegen = markUserWritten(
                node, makeIrCopy(ctx->pool, &node->loc, src, dst));
        }
        else {
            const IrType *elemType =
                lowerType(ctx, node->type ?: (args ? args->type : NULL));
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                            &node->loc,
                                            flgTemporary,
                                            tempName,
                                            tempName,
                                            elemType,
                                            ctx->currentScopeId);
            emitStmt(ctx, tempVar);
            IrNode *dst = makeIrVarRef(ctx->pool, &node->loc, tempVar);
            emitStmt(ctx,
                     markUserWritten(
                         node, makeIrCopy(ctx->pool, &node->loc, src, dst)));
            node->codegen = dst;
        }
        break;
    }
    case bfiMove: {
        IrNode *src = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrMove(ctx->pool, &node->loc, src);
        break;
    }
    case bfiDrop: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrDrop(ctx->pool, &node->loc, target);
        break;
    }
    case bfiCurrentException: {
        // Borrowed: the exception stays owned by the catch result temporary
        if (ctx->currentCatchErr == NULL) {
            logError(ctx->driver->L,
                     &node->loc,
                     "`ex!` used outside of a catch block during IR lowering",
                     NULL);
            node->codegen = NULL;
            break;
        }
        node->codegen = cloneIrNode(ctx->pool, ctx->currentCatchErr);
        break;
    }
    default:
        node->codegen = NULL;
        break;
    }
}

static void visitGroupExpr(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->groupExpr.expr);
    node->codegen = node->groupExpr.expr->codegen;
}

static void visitAliasExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *expr = lowerFlattenedOperand(visitor, node->aliasExpr.expr);
    cstring name = node->aliasExpr.name ?: getDeclarationName(node);
    node->codegen = expr;
    node->ir = makeIrAlias(ctx->pool, &node->loc, name, expr);
}

// Module globals get the declaring module's prefix; references reuse it via
// decl->ir, so only the declaration decides the C name.
static cstring makeGlobalVarSymbolName(LowerContext *ctx,
                                       const AstNode *node,
                                       cstring name)
{
    if (name == NULL || hasFlag(node, Extern))
        return name;
    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    if (modName == NULL || modName == S_main)
        return name;
    return makeStringf(ctx->strings, "%s_%s", modName, name);
}

// `@register("x0")` pins a local to a machine register, see inline asm operands
static cstring getRegisterAttr(const AstNode *node)
{
    const AstNode *attr = findAttribute(node, S_register);
    const AstNode *arg = attr ? attr->attr.args : NULL;
    return arg && nodeIs(arg, StringLit) ? arg->stringLiteral.value : NULL;
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    if (hasFlag(node, Comptime))
        return;

    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *varAstType =
        (node->varDecl.names && node->varDecl.names->type)
            ? node->varDecl.names->type
            : (node->varDecl.type ? node->varDecl.type->type : node->type);
    varAstType = resolveUnThisUnwrapType(varAstType);
    const IrType *irType = lowerType(ctx, varAstType);
    cstring name = getDeclarationName(node);

    if (ctx->currentFunc == NULL) {
        IrNode *init = NULL;
        if (node->varDecl.init) {
            astVisit(visitor, node->varDecl.init);
            init = (IrNode *)node->varDecl.init->codegen;
        }
        IrNode *glob = makeIrGlobalVar(ctx->driver->pool,
                                       &node->loc,
                                       node->flags,
                                       makeGlobalVarSymbolName(ctx, node, name),
                                       irType,
                                       init);
        // @thread: one instance per thread, not shared by the whole program
        glob->globalVar.isThreadLocal = findAttribute(node, S_thread) != NULL;
        node->ir = glob;
        if (node->varDecl.names)
            node->varDecl.names->ir = glob;

        if (ctx->module->module.globals) {
            IrNode *last = ctx->module->module.globals;
            while (last->next)
                last = last->next;
            last->next = glob;
        }
        else {
            ctx->module->module.globals = glob;
        }
        return;
    }

    cstring displayName = getDeclarationName(node);
    cstring cName = makeUniqueVarName(ctx, displayName);
    Flags flags = node->flags;
    if (!isReferenceType(varAstType) && !isPointerType(varAstType) &&
        isDestructible(varAstType)) {
        flags |= flgReferenceMembers;
    }
    IrNode *varDecl = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    cName,
                                    displayName,
                                    irType,
                                    ctx->currentScopeId);
    varDecl->varDecl.reg = getRegisterAttr(node);
    node->ir = varDecl;
    if (node->varDecl.names)
        node->varDecl.names->ir = varDecl;
    emitStmt(ctx, varDecl);

    if (node->varDecl.init) {
        AstNode *initNode = node->varDecl.init;
        if (hasFlag(varAstType, Slice) || isSliceType(varAstType)) {
            if (initNode->type && isSliceType(initNode->type)) {
                astVisit(visitor, initNode);
                IrNode *val = (IrNode *)initNode->codegen;
                if (val)
                    emitStmt(ctx,
                             makeIrAssign(
                                 ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
            }
            else {
                lowerSliceIntoTarget(ctx,
                                     visitor,
                                     initNode,
                                     makeIrVarRef(ctx->pool, &node->loc, varDecl));
            }
        }
        else if (isOptionalType(varAstType)) {
            if (initNode->type && isOptionalType(initNode->type)) {
                astVisit(visitor, initNode);
                IrNode *val = (IrNode *)initNode->codegen;
                if (val)
                    emitStmt(ctx,
                             makeIrAssign(
                                 ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
            }
            else {
                lowerOptionalIntoTarget(
                    ctx,
                    visitor,
                    initNode,
                    makeIrVarRef(ctx->pool, &node->loc, varDecl));
            }
        }
        else if (typeIs(varAstType, Union)) {
            lowerTaggedUnionIntoTarget(
                ctx,
                visitor,
                initNode,
                makeIrVarRef(ctx->pool, &node->loc, varDecl),
                varAstType);
        }
        else if (nodeIs(initNode, BackendCall) &&
                 initNode->backendCallExpr.func == bfiCopy &&
                 initNode->backendCallExpr.args &&
                 initNode->backendCallExpr.args->next == NULL) {
            IrNode *src =
                lowerFlattenedOperand(visitor, initNode->backendCallExpr.args);
            emitStmt(ctx,
                     markUserWritten(
                         initNode,
                         makeIrCopy(ctx->pool,
                                    &node->loc,
                                    src,
                                    makeIrVarRef(ctx->pool, &node->loc, varDecl))));
        }
        else {
            astVisit(visitor, initNode);
            IrNode *val = (IrNode *)initNode->codegen;
            if (val)
                emitStmt(
                    ctx,
                    makeIrAssign(ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
        }
    }
    else if (isOptionalType(varAstType)) {
        lowerOptionalIntoTarget(
            ctx, visitor, NULL, makeIrVarRef(ctx->pool, &node->loc, varDecl));
    }
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->exprStmt.expr);
    IrNode *expr = (IrNode *)node->exprStmt.expr->codegen;
    node->codegen = expr;
    if (expr) {
        const Type *astType =
            node->exprStmt.expr->type
                ? resolveUnThisUnwrapType(node->exprStmt.expr->type)
                : NULL;
        // the payload already lives in the result temporary, drop it in place
        if (isOwnedResultPayload(expr)) {
            emitStmt(ctx, makeIrDrop(ctx->pool, &node->loc, expr));
            return;
        }
        // the value of a discarded catch lives in its temporary, drop it in place
        if (nodeIs(node->exprStmt.expr, CatchExpr) && expr->tag == irVarRef &&
            isIrTypeDestructible(expr->type)) {
            emitStmt(ctx, makeIrDrop(ctx->pool, &node->loc, expr));
            return;
        }
        if (expr->tag == irVarRef || isIrConst(expr) ||
            (isIrPureValueExpr(expr) && expr->tag != irCall)) {
            return;
        }
        if (expr->tag == irCall && astType && !isReferenceType(astType) &&
            !isPointerType(astType) && isDestructible(astType)) {
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                            &node->loc,
                                            flgEphemeral | flgReferenceMembers,
                                            tempName,
                                            tempName,
                                            expr->type,
                                            ctx->currentScopeId);
            emitStmt(ctx, tempVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, tempVar),
                                  expr));
        }
        else {
            emitStmt(ctx, expr);
        }
    }
}

static void visitStmtExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *stmt = node->stmtExpr.stmt;
    if (stmt == NULL)
        return;

    if (nodeIs(stmt, BlockStmt)) {
        IrNode *lastVal = NULL;
        for (AstNode *s = stmt->blockStmt.stmts; s; s = s->next) {
            astVisit(visitor, s);
            if (s->codegen) {
                lastVal = (IrNode *)s->codegen;
            }
            else if (nodeIs(s, ExprStmt) && s->exprStmt.expr && s->exprStmt.expr->codegen) {
                lastVal = (IrNode *)s->exprStmt.expr->codegen;
            }
        }
        node->codegen = lastVal;
    }
    else {
        astVisit(visitor, stmt);
        node->codegen = stmt->codegen;
    }
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 scopeId = ctx->nextScopeId++;
    u32 prevScopeId = ctx->currentScopeId;
    csAssert0(scopeId == ctx->scopes.size);
    pushOnDynArray(&ctx->scopes,
                   &(ScopeInfo){.parent = prevScopeId, .hasDrops = false});
    ctx->currentScopeId = scopeId;
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, scopeId));

    for (AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        astVisit(visitor, stmt);
    }

    if (!isBlockTerminated(ctx->currentBlock))
        emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, scopeId));
    ctx->currentScopeId = prevScopeId;
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->ifStmt.cond);
    u32 ifId = ctx->nextIfId++;
    IrNode *thenBB =
        createBlock(ctx, makeStringf(ctx->strings, "IF%u_then", ifId));
    IrNode *elseBB =
        node->ifStmt.otherwise
            ? createBlock(ctx, makeStringf(ctx->strings, "IF%u_else", ifId))
            : NULL;
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "IF%u_end", ifId));

    emitStmt(
        ctx,
        makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB ?: mergeBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ifStmt.body);
    emitBranchIfNotTerminated(ctx, mergeBB);

    if (elseBB) {
        setBlock(ctx, elseBB);
        astVisit(visitor, node->ifStmt.otherwise);
        emitBranchIfNotTerminated(ctx, mergeBB);
    }

    setBlock(ctx, mergeBB);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 whileId = ctx->nextWhileId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_cond", whileId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_body", whileId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_exit", whileId));

    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, condBB);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->whileStmt.cond);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = condBB,
                    .exitBlock = exitBB,
                    .scopeId = ctx->currentScopeId,
                    .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->whileStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

static void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);

    if (nodeIs(node->forStmt.range, RangeExpr)) {
        AstNode *startNode = node->forStmt.range->rangeExpr.start;
        AstNode *endNode = node->forStmt.range->rangeExpr.end;
        AstNode *stepNode = node->forStmt.range->rangeExpr.step;

        IrNode *startVal = lowerFlattenedOperand(visitor, startNode);
        IrNode *endVal = lowerFlattenedOperand(visitor, endNode);

        // sema typed the variable with the common type of the range, the
        // start value alone may be a small literal (`0..size`)
        AstNode *loopVarNode = node->forStmt.var;
        const IrType *varType =
            loopVarNode->type ? lowerType(ctx, loopVarNode->type)
                              : (startVal ? startVal->type : u64Type);
        cstring displayName = getDeclarationName(loopVarNode);
        cstring cName = makeUniqueVarName(ctx, displayName);
        IrNode *varDecl = makeIrVarDecl(ctx->pool,
                                        &loopVarNode->loc,
                                        loopVarNode->flags,
                                        cName,
                                        displayName,
                                        varType,
                                        ctx->currentScopeId);
        loopVarNode->ir = varDecl;
        emitStmt(ctx, varDecl);
        emitStmt(
            ctx,
            makeIrAssign(ctx->pool,
                         &startNode->loc,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         startVal));

        u32 forId = ctx->nextForId++;
        IrNode *condBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_cond", forId));
        IrNode *bodyBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_body", forId));
        IrNode *updateBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_update", forId));
        IrNode *exitBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_exit", forId));

        emitBranchIfNotTerminated(ctx, condBB);
        setBlock(ctx, condBB);

        IrNode *cond =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opLt,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         endVal,
                         boolType);
        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

        setBlock(ctx, bodyBB);
        LoopCtx loop = {.condBlock = updateBB,
                        .exitBlock = exitBB,
                        .scopeId = ctx->currentScopeId,
                        .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
        ctx->loopCtx = &loop;
        astVisit(visitor, node->forStmt.body);
        ctx->loopCtx = loop.prev;
        emitBranchIfNotTerminated(ctx, updateBB);

        setBlock(ctx, updateBB);
        IrNode *stepVal =
            stepNode ? lowerFlattenedOperand(visitor, stepNode)
                     : makeIrIntConst(ctx->pool, &node->loc, 1, false, varType);
        IrNode *nextVal =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opAdd,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         stepVal,
                         varType);
        emitStmt(
            ctx,
            makeIrAssign(ctx->pool,
                         &node->loc,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         nextVal));
        emitBranchIfNotTerminated(ctx, condBB);

        setBlock(ctx, exitBB);
        return;
    }

    u32 forId = ctx->nextForId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_cond", forId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_body", forId));
    IrNode *updateBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_update", forId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_exit", forId));

    emitBranchIfNotTerminated(ctx, condBB);
    setBlock(ctx, condBB);

    IrNode *cond = lowerConditionOperand(ctx, visitor, node->forStmt.range);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = updateBB,
                    .exitBlock = exitBB,
                    .scopeId = ctx->currentScopeId,
                    .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->forStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, updateBB);

    setBlock(ctx, updateBB);
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

// Drops a caught exception and defuses its result so it is not dropped twice
static void emitReleaseCaught(LowerContext *ctx,
                              const FileLoc *loc,
                              const CatchCtx *c)
{
    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    emitStmt(ctx, makeIrDrop(ctx->pool, loc, cloneIrNode(ctx->pool, c->err)));
    IrNode *tagSlot = makeIrMember(
        ctx->pool, loc, cloneIrNode(ctx->pool, c->res), 0, S_tag, u32Type);
    IrNode *defuse = makeIrAssign(
        ctx->pool,
        loc,
        tagSlot,
        makeIrIntConst(ctx->pool, loc, 0, false, u32Type));
    defuse->flags |= flgUnmanaged;
    emitStmt(ctx, defuse);
}

// Releases the catch bodies entered since `outer`, innermost first
static void emitReleaseCaughtUntil(LowerContext *ctx,
                                   const FileLoc *loc,
                                   const CatchCtx *outer)
{
    for (const CatchCtx *c = ctx->catchCtx; c != outer; c = c->prev)
        emitReleaseCaught(ctx, loc, c);
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->exitBlock) {
        emitReleaseCaughtUntil(ctx, &node->loc, ctx->loopCtx->catchCtx);
        for (u32 s = ctx->currentScopeId; s > ctx->loopCtx->scopeId; s--) {
            emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, s));
        }
        emitStmt(ctx,
                 makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->exitBlock));
    }
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->condBlock) {
        emitReleaseCaughtUntil(ctx, &node->loc, ctx->loopCtx->catchCtx);
        for (u32 s = ctx->currentScopeId; s > ctx->loopCtx->scopeId; s--) {
            emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, s));
        }
        emitStmt(ctx,
                 makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->condBlock));
    }
}

static u32 nearestDirtyScope(LowerContext *ctx, u32 scopeId)
{
    while (scopeId != 0 && scopeId < ctx->scopes.size &&
           !dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).hasDrops)
        scopeId = dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).parent;
    return scopeId;
}

IrNode *getOrCreateScopeReturnBlock(LowerContext *ctx,
                                    u32 scopeId,
                                    const FileLoc *loc)
{
    scopeId = nearestDirtyScope(ctx, scopeId);
    if (scopeId == 0)
        return ctx->funcExitBlock;

    cstring name = makeStringf(ctx->strings, "_L_UNWIND%u_return", scopeId);
    if (ctx->currentFunc) {
        for (IrNode *b = ctx->currentFunc->function.firstBlock; b;
             b = b->next) {
            if (b->tag == irBasicBlock && b->basicBlock.name == name)
                return b;
        }
    }

    IrNode *savedBlock = ctx->currentBlock;
    IrNode *unwindBB =
        createBlock(ctx, makeStringf(ctx->strings, "UNWIND%u_return", scopeId));
    setBlock(ctx, unwindBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, loc, scopeId));
    IrNode *parentUnwindBB = getOrCreateScopeReturnBlock(
        ctx, dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).parent, loc);
    emitStmt(ctx, makeIrBranch(ctx->pool, loc, parentUnwindBB));
    ctx->currentBlock = savedBlock;
    return unwindBB;
}

static void visitYieldStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->yieldStmt.expr && ctx->catchTargetVar) {
        IrNode *val = lowerFlattenedOperand(visitor, node->yieldStmt.expr);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, ctx->catchTargetVar),
                              val));
    }
    if (ctx->currentCatchErr) {
        emitStmt(ctx,
                 makeIrDrop(ctx->pool,
                            &node->loc,
                            cloneIrNode(ctx->pool, ctx->currentCatchErr)));
    }
    if (ctx->currentCatchRes) {
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *resTagSlot = makeIrMember(
            ctx->pool,
            &node->loc,
            cloneIrNode(ctx->pool, ctx->currentCatchRes),
            0,
            S_tag,
            u32Type);
        IrNode *defuseStmt = makeIrAssign(
            ctx->pool,
            &node->loc,
            resTagSlot,
            makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);
    }
    if (ctx->catchMergeBB) {
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, ctx->catchMergeBB));
    }
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->returnStmt.expr && ctx->funcRetVar) {
        AstNode *retExpr = node->returnStmt.expr;
        IrNode *retVarRef =
            makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        const Type *retAstType = ctx->funcRetAstType;
        const Type *exprAstType = retExpr->type;

        if (isResultType(retAstType)) {
            const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
            IrNode *tagSlot = makeIrMember(ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
            const IrType *retIrType = ctx->funcRetVar->type;
            const IrType *valFieldType = retIrType->strct.fields[1].type;
            IrNode *valMember = makeIrMember(ctx->pool, &node->loc, retVarRef, 1, S_val, valFieldType);

            if (node->returnStmt.isRaise) {
                IrNode *errVal = lowerFlattenedOperand(visitor, retExpr);
                emitExceptionTracePush(ctx, &node->loc, errVal);
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));
                if (valFieldType->tag == irTypeUntaggedUnion) {
                    const Type *baseExType = findBuiltinType(S_Exception);
                    const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
                    IrNode *errSlot = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
                    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, errSlot, errVal));
                }
                else {
                    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valMember, errVal));
                }
            }
            else {
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type)));
                if (retExpr && valFieldType->tag == irTypeUntaggedUnion) {
                    const IrType *elemType = valFieldType->strct.fields[0].type;
                    IrNode *valSlot = makeIrMember(ctx->pool, &node->loc, valMember, 0, NULL, elemType);
                    const Type *resTargetType = getResultTargetType(retAstType);
                    if (isSliceType(resTargetType) && (exprAstType == NULL || !isSliceType(exprAstType))) {
                        lowerSliceIntoTarget(ctx, visitor, retExpr, valSlot);
                    }
                    else if (isOptionalType(resTargetType) && !isOptionalType(exprAstType)) {
                        lowerOptionalIntoTarget(ctx, visitor, retExpr, valSlot);
                    }
                    else {
                        IrNode *val = lowerFlattenedOperand(visitor, retExpr);
                        if (val)
                            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valSlot, val));
                    }
                }
            }
        }
        else if (isSliceType(retAstType) && (exprAstType == NULL ||!isSliceType(exprAstType))) {
            lowerSliceIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else if (isOptionalType(retAstType) && !isOptionalType(exprAstType)) {
            lowerOptionalIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else if (retAstType && typeIs(retAstType, Union) &&
                 (nodeIs(retExpr, UnionValueExpr) ||
                  (exprAstType && !typeIs(exprAstType, Union)))) {
            lowerTaggedUnionIntoTarget(
                ctx, visitor, retExpr, retVarRef, retAstType);
        }
        else if (isBackendCopyExpr(retExpr)) {
            IrNode *src =
                lowerFlattenedOperand(visitor, retExpr->backendCallExpr.args);
            emitStmt(ctx,
                     markUserWritten(
                         retExpr,
                         makeIrCopy(ctx->pool, &node->loc, src, retVarRef)));
        }
        else {
            astVisit(visitor, retExpr);
            IrNode *val = (IrNode *)retExpr->codegen;
            if (val)
                emitStmt(ctx,
                         makeIrAssign(ctx->pool, &node->loc, retVarRef, val));
        }
    }
    else if (ctx->funcRetVar && isResultType(ctx->funcRetAstType)) {
        IrNode *retVarRef = makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *tagSlot = makeIrMember(ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
        if (node->returnStmt.isRaise) {
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));
            if (ctx->currentCatchErr) {
                emitExceptionTracePush(ctx, &node->loc, ctx->currentCatchErr);
                const IrType *retIrType = ctx->funcRetVar->type;
                const IrType *valFieldType = retIrType->strct.fields[1].type;
                IrNode *valMember = makeIrMember(ctx->pool, &node->loc, retVarRef, 1, S_val, valFieldType);
                IrNode *assignStmt = NULL;
                if (valFieldType->tag == irTypeUntaggedUnion) {
                    const Type *baseExType = findBuiltinType(S_Exception);
                    const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
                    IrNode *errSlot = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
                    assignStmt = makeIrAssign(ctx->pool, &node->loc, errSlot, cloneIrNode(ctx->pool, ctx->currentCatchErr));
                }
                else {
                    assignStmt = makeIrAssign(ctx->pool, &node->loc, valMember, cloneIrNode(ctx->pool, ctx->currentCatchErr));
                }
                assignStmt->flags |= flgUnmanaged;
                emitStmt(ctx, assignStmt);
                if (ctx->currentCatchRes) {
                    IrNode *srcTagSlot = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, ctx->currentCatchRes), 0, S_tag, u32Type);
                    IrNode *defuseStmt = makeIrAssign(ctx->pool, &node->loc, srcTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
                    defuseStmt->flags |= flgUnmanaged;
                    emitStmt(ctx, defuseStmt);
                }
            }
        }
        else {
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type)));
        }
    }
    // `raise` moves the caught exception, any other return leaves it owned
    if (!node->returnStmt.isRaise)
        emitReleaseCaughtUntil(ctx, &node->loc, NULL);
    IrNode *targetBB =
        getOrCreateScopeReturnBlock(ctx, ctx->currentScopeId, &node->loc);
    emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, targetBB));
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerFlattenedOperand(visitor, node->switchStmt.cond);
    u32 switchId = ctx->nextSwitchId++;
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "SWITCH%u_exit", switchId));

    u64 totalCases = 0;
    for (const AstNode *c = node->switchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match)
            totalCases += countAstNodes(c->caseStmt.match);
        else
            totalCases++;
    }

    IrCase *cases =
        allocFromMemPool(ctx->pool, sizeof(IrCase) * (totalCases ?: 1));
    u64 caseIdx = 0;
    IrNode *defaultBB = NULL;

    u64 caseStmtCount = countAstNodes(node->switchStmt.cases);
    IrNode **caseBBs =
        allocFromMemPool(ctx->pool, sizeof(IrNode *) * (caseStmtCount ?: 1));

    // Pass 1: Create a single basic block per shared body
    u32 cIdx = 0;
    for (AstNode *c = node->switchStmt.cases; c; c = c->next, cIdx++) {
        if (c->caseStmt.body != NULL || c->next == NULL) {
            caseBBs[cIdx] = createBlock(
                ctx, makeStringf(ctx->strings, "SWITCH%u_case_%u", switchId, cIdx));
        }
    }
    // Propagate the target block backwards to preceding cases sharing the same body
    IrNode *currentGroupBB = NULL;
    for (i64 i = (i64)caseStmtCount - 1; i >= 0; i--) {
        if (caseBBs[i] != NULL)
            currentGroupBB = caseBBs[i];
        else
            caseBBs[i] = currentGroupBB;
    }

    // Pass 2: Lower cases and emit bodies
    cIdx = 0;
    for (AstNode *c = node->switchStmt.cases; c; c = c->next, cIdx++) {
        IrNode *caseBB = caseBBs[cIdx];
        if (c->caseStmt.match == NULL) {
            defaultBB = caseBB;
        }
        else {
            for (const AstNode *m = c->caseStmt.match; m; m = m->next) {
                if (nodeIs(m, RangeExpr)) {
                    IrNode *startNode = lowerFlattenedOperand(visitor, m->rangeExpr.start);
                    IrNode *endNode = lowerFlattenedOperand(visitor, m->rangeExpr.end);
                    cases[caseIdx++] = (IrCase){
                        .valNode = startNode, .endNode = endNode, .target = caseBB};
                }
                else {
                    IrNode *valNode = lowerFlattenedOperand(visitor, (AstNode *)m);
                    cases[caseIdx++] = (IrCase){
                        .valNode = valNode, .endNode = NULL, .target = caseBB};
                }
            }
        }

        if (c->caseStmt.body) {
            IrNode *savedBlock = ctx->currentBlock;
            setBlock(ctx, caseBB);
            LoopCtx loop = {
                .condBlock = NULL, .exitBlock = exitBB, .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
            ctx->loopCtx = &loop;
            astVisit(visitor, c->caseStmt.body);
            ctx->loopCtx = loop.prev;
            emitBranchIfNotTerminated(ctx, exitBB);
            ctx->currentBlock = savedBlock;
        }
    }

    IrNode *switchNode = makeIrSwitch(
        ctx->pool, &node->loc, cond, cases, caseIdx, defaultBB ?: exitBB);
    emitStmt(ctx, switchNode);

    setBlock(ctx, exitBB);
}

static void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *unionVal = lowerFlattenedOperand(visitor, node->matchStmt.expr);
    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->matchStmt.expr->loc, unionVal, 0, "tag", u32Type);
    u32 matchId = ctx->nextMatchId++;
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "MATCH%u_exit", matchId));

    u64 casesCount = 0;
    AstNode *defaultCase = node->matchStmt.defaultCase;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match != NULL)
            casesCount++;
        else if (defaultCase == NULL)
            defaultCase = c;
    }

    IrCase *cases =
        allocFromMemPool(ctx->pool, sizeof(IrCase) * (casesCount ?: 1));
    IrNode **caseBBs =
        allocFromMemPool(ctx->pool, sizeof(IrNode *) * (casesCount ?: 1));

    const Type *sumType =
        stripPointerOrReferenceOnce(node->matchStmt.expr->type, NULL);
    u64 ci = 0;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match == NULL)
            continue;
        IrNode *bb = createBlock(
            ctx,
            makeStringf(
                ctx->strings, "MATCH%u_case_%u", matchId, (unsigned)ci));
        u32 caseIdx = c->caseStmt.idx;
        if (caseIdx == 0 && c->caseStmt.match && sumType &&
            isUnionType(sumType)) {
            u32 found = findUnionTypeIndex(sumType, c->caseStmt.match->type);
            if (found != UINT32_MAX)
                caseIdx = found;
        }
        cases[ci].valNode =
            makeIrIntConst(ctx->pool, &c->loc, caseIdx, false, u32Type);
        cases[ci].target = bb;
        caseBBs[ci] = bb;
        ci++;
    }

    IrNode *defaultBB =
        defaultCase
            ? createBlock(ctx,
                          makeStringf(ctx->strings, "MATCH%u_default", matchId))
            : exitBB;
    emitStmt(ctx,
             makeIrSwitch(
                 ctx->pool, &node->loc, tagVal, cases, casesCount, defaultBB));

    ci = 0;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match == NULL)
            continue;
        u32 caseIdx = cases[ci].valNode->intConst.value;
        setBlock(ctx, caseBBs[ci++]);
        if (c->caseStmt.alias) {
            const IrType *aliasIrType = lowerType(ctx, c->caseStmt.match->type);
            const IrType *unionStructType = unionVal->type;
            if (unionStructType && unionStructType->tag == irTypePointer)
                unionStructType = unionStructType->pointer.pointee;
            const IrType *payloadUnionType =
                (unionStructType && unionStructType->tag == irTypeStruct &&
                 unionStructType->strct.count > 1)
                    ? unionStructType->strct.fields[1].type
                    : NULL;
            IrNode *valMember = makeIrMember(
                ctx->pool, &c->loc, unionVal, 1, "val", payloadUnionType);
            cstring fieldName = makeStringf(ctx->strings, "_%u", caseIdx);
            IrNode *payloadMember = makeIrMember(
                ctx->pool, &c->loc, valMember, caseIdx, fieldName, aliasIrType);
            cstring aliasName = nodeIs(c->caseStmt.alias, AliasExpr)
                                    ? c->caseStmt.alias->aliasExpr.name
                                    : getDeclarationName(c->caseStmt.alias);
            c->caseStmt.alias->ir =
                makeIrAlias(ctx->pool, &c->loc, aliasName, payloadMember);
        }
        if (c->caseStmt.body) {
            astVisit(visitor, c->caseStmt.body);
        }
        emitBranchIfNotTerminated(ctx, exitBB);
    }

    if (defaultCase) {
        setBlock(ctx, defaultBB);
        if (defaultCase->caseStmt.body) {
            astVisit(visitor, defaultCase->caseStmt.body);
        }
        emitBranchIfNotTerminated(ctx, exitBB);
    }

    setBlock(ctx, exitBB);
}

static u8 lowerExternFunctionAttrs(const AstNode *node)
{
    u8 attrs = 0;
    if (findAttribute(node, S_naked))
        attrs |= irFuncAttrNaked;
    if (findAttribute(node, S_noreturn))
        attrs |= irFuncAttrNoReturn;
    if (findAttribute(node, S_returns_twice))
        attrs |= irFuncAttrReturnsTwice;
    return attrs;
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Extern)) {
        bool isHint = findAttribute(node, S_hint) != NULL;

        const IrType *funcType = lowerType(ctx, node->type);
        u64 paramsCount = 0;
        IrNode *paramsFirst = NULL, *paramsLast = NULL;
        u32 pidx = 0;
        for (AstNode *p = node->funcDecl.signature->params; p;
             p = p->next, pidx++) {
            // C varargs marker, emitted as `...` from the function flag
            if (hasFlag(p, Variadic))
                continue;
            paramsCount++;
            const IrType *pType = lowerType(ctx, p->type);
            cstring pname =
                p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
            IrNode *param = makeIrFuncParam(
                ctx->pool, &p->loc, p->flags, pname, pidx, pType);
            p->ir = param;
            if (paramsLast) {
                paramsLast->next = param;
                paramsLast = param;
            }
            else {
                paramsFirst = paramsLast = param;
            }
        }

        cstring funcName = getDeclarationName(node);
        cstring cName = getFunctionSymbolName(ctx, node);
        cstring displayName = funcName;
        IrNode *func = makeIrFunction(ctx->pool,
                                      &node->loc,
                                      node->flags | flgExtern,
                                      cName,
                                      displayName,
                                      funcType,
                                      paramsFirst,
                                      paramsCount);
        func->ast = node;
        func->function.isHint = isHint;
        func->function.attrs = lowerExternFunctionAttrs(node);
        node->ir = func;

        if (ctx->module->module.functions) {
            IrNode *last = ctx->module->module.functions;
            while (last->next)
                last = last->next;
            last->next = func;
        }
        else {
            ctx->module->module.functions = func;
        }
        return;
    }

    const IrType *funcType = lowerType(ctx, node->type);
    u64 paramsCount = countAstNodes(node->funcDecl.signature->params);
    IrNode *paramsFirst = NULL, *paramsLast = NULL;
    u32 pidx = 0;
    for (AstNode *p = node->funcDecl.signature->params; p;
         p = p->next, pidx++) {
        const IrType *pType = lowerType(ctx, p->type);
        cstring pname =
            p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
        Flags pFlags = p->flags;
        const Type *pAstType =
            p->type ? resolveUnThisUnwrapType(p->type) : NULL;
        if (pAstType && !isReferenceType(pAstType) &&
            !isPointerType(pAstType) && !hasFlag(p, Reference) &&
            isDestructible(pAstType)) {
            pFlags |= flgReferenceMembers;
        }
        IrNode *param =
            makeIrFuncParam(ctx->pool, &p->loc, pFlags, pname, pidx, pType);
        p->ir = param;
        if (paramsLast) {
            paramsLast->next = param;
            paramsLast = param;
        }
        else {
            paramsFirst = paramsLast = param;
        }
    }

    cstring funcName = getDeclarationName(node);
    AstNode *parent = node->parentScope;
    cstring modName = ctx->module->module.name;
    cstring cName = getFunctionSymbolName(ctx, node);
    cstring displayName = funcName;

    if (parent && isClassOrStructAstNode(parent)) {
        cstring typeName = getDeclarationName(parent);
        displayName =
            makeStringf(ctx->strings, "%s.%s.%s", modName, typeName, funcName);
    }
    else if (hasFlag(node, Main) || funcName == S_main) {
        displayName = S_main;
    }
    else if (!hasFlag(node, Extern) && !hasFlag(node, Builtin) && modName &&
             modName != S_main) {
        displayName = makeStringf(ctx->strings, "%s.%s", modName, funcName);
    }

    IrNode *func = makeIrFunction(ctx->pool,
                                  &node->loc,
                                  node->flags,
                                  cName,
                                  displayName,
                                  funcType,
                                  paramsFirst,
                                  paramsCount);
    func->ast = node;
    node->ir = func;
    if (node->glue)
        irSetInfo(func, irInfoGlue);
    if (node->funcDecl.isInitCover || findAttribute(node, S_memtrace))
        func->function.attrs |= irFuncAttrCallSite;

    ctx->currentFunc = func;
    ctx->nextBlockId = 0;
    ctx->nextForId = 0;
    ctx->nextWhileId = 0;
    ctx->nextIfId = 0;
    ctx->nextSwitchId = 0;
    ctx->nextMatchId = 0;
    ctx->nextOptId = 0;
    ctx->nextTernaryId = 0;
    ctx->nextLogicId = 0;
    ctx->currentScopeId = 0;
    ctx->nextScopeId = 1;
    clearDynArray(&ctx->scopes);
    pushOnDynArray(&ctx->scopes, &(ScopeInfo){.parent = 0, .hasDrops = false});
    ctx->varNames = newHashTable(sizeof(VarNameEntry), ctx->pool);
    // Params own their C names; locals that shadow a param get a suffix
    for (IrNode *p = func->function.params; p; p = p->next)
        makeUniqueVarName(ctx, p->funcParam.name);

    IrNode *entryBB = createBlock(ctx, "entry");
    IrNode *exitBB = makeIrBasicBlock(ctx->pool, NULL, "_L_exit");
    ctx->funcExitBlock = exitBB;

    setBlock(ctx, entryBB);
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, 0));

    bool hasRet = funcType->func.ret && funcType->func.ret->tag != irTypeVoid;
    if (hasRet) {
        ctx->funcRetVar = makeIrVarDecl(ctx->pool,
                                        &node->loc,
                                        flgReturned,
                                        "__ret",
                                        "__ret",
                                        funcType->func.ret,
                                        0);
        ctx->funcRetAstType = (node->type && typeIs(node->type, Func))
                                  ? node->type->func.retType
                                  : NULL;
        emitStmt(ctx, ctx->funcRetVar);
    }
    else {
        ctx->funcRetVar = NULL;
        ctx->funcRetAstType = NULL;
    }

    if (node->funcDecl.body) {
        if (nodeIs(node->funcDecl.body, BlockStmt)) {
            for (AstNode *stmt = node->funcDecl.body->blockStmt.stmts; stmt;
                 stmt = stmt->next) {
                astVisit(visitor, stmt);
            }
        }
        else {
            astVisit(visitor, node->funcDecl.body);
        }
    }

    emitBranchIfNotTerminated(ctx, exitBB);

    irAppendFunctionBlock(ctx->currentFunc, exitBB);
    setBlock(ctx, exitBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, 0));
    IrNode *retVal = ctx->funcRetVar
                         ? makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar)
                         : NULL;
    emitStmt(ctx, makeIrReturn(ctx->pool, &node->loc, retVal));

    if (ctx->module->module.functions) {
        IrNode *last = ctx->module->module.functions;
        while (last->next)
            last = last->next;
        last->next = func;
    }
    else {
        ctx->module->module.functions = func;
    }

    ctx->currentFunc = NULL;
    ctx->currentBlock = NULL;
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    if (node->externDecl.func && hasFlag(node->externDecl.func, Extern)) {
        astVisit(visitor, node->externDecl.func);
    }
}

static void visitClassOrStructDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    lowerType(ctx, node->type);
    AstNode *members = nodeIs(node, StructDecl) ? node->structDecl.members
                                                : node->classDecl.members;
    for (AstNode *m = members; m; m = m->next) {
        if (nodeIs(m, FuncDecl)) {
            astVisit(visitor, m);
        }
    }
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    cstring moduleName =
        node->program.module
            ? node->program.module->moduleDecl.name
            : (hasFlag(node, BuiltinsModule) ? S_builtins : S_main);
    ctx->module =
        makeIrModule(ctx->driver->pool, &node->loc, moduleName, ctx->types);

    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        astVisit(visitor, decl);
    }
}

static void finalizeLifecycleTypes(LowerContext *ctx)
{
    HashtableIt it = newHashTableIt(&ctx->types->types, sizeof(IrType *));
    while (hashTableItHasNext(&it)) {
        IrType *type = *(IrType **)hashTableItNext(&it);
        if (type && type->category == irTypeCatArray) {
            const AstNode *dAst = type->array.dctorAst;
            if (dAst != NULL) {
                csAssert(dAst->ir != NULL,
                         "array destructor '%s' was never lowered to IR",
                         getDeclarationName(dAst));
                type->array.dctor = dAst->ir;
            }
            else {
                type->array.dctor = NULL;
            }
        }
        else if (type && (type->category == irTypeCatStruct ||
                          type->category == irTypeCatTuple ||
                          type->category == irTypeCatUnion ||
                          type->category == irTypeCatClass)) {
            const AstNode *dAst = type->strct.dctorAst;
            const AstNode *fAst = type->strct.dctorFwdAst;
            const AstNode *cAst = type->strct.copyAst;

            if (dAst != NULL) {
                csAssert(dAst->ir != NULL,
                         "destructor '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(dAst),
                         type->name ?: "<anonymous>");
                type->strct.dctor = dAst->ir;
            }
            else {
                type->strct.dctor = NULL;
            }

            if (fAst != NULL) {
                csAssert(fAst->ir != NULL,
                         "destructor forward '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(fAst),
                         type->name ?: "<anonymous>");
                type->strct.dctorFwd = fAst->ir;
            }
            else {
                type->strct.dctorFwd = NULL;
            }

            if (cAst != NULL) {
                csAssert(cAst->ir != NULL,
                         "copy function '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(cAst),
                         type->name ?: "<anonymous>");
                type->strct.copy = cAst->ir;
            }
            else {
                type->strct.copy = NULL;
            }
        }
    }
}

static void visitAsmStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    bool isVolatile = findAttribute(node, S_volatile) != NULL;
    for (AstNode *out = node->inlineAssembly.outputs; out; out = out->next) {
        if (out->asmOperand.operand)
            out->asmOperand.operand->codegen =
                lowerFlattenedOperand(visitor, out->asmOperand.operand);
    }
    for (AstNode *in = node->inlineAssembly.inputs; in; in = in->next) {
        if (in->asmOperand.operand)
            in->asmOperand.operand->codegen =
                lowerFlattenedOperand(visitor, in->asmOperand.operand);
    }
    IrNode *asmNode = makeIrInlineAsm(
        ctx->pool,
        &node->loc,
        node->inlineAssembly.text,
        NULL,
        NULL,
        NULL,
        isVolatile);
    asmNode->ast = node;
    emitStmt(ctx, asmNode);
}

IrModule *lowerToIr(CompilerDriver *driver, AstNode *node)
{
    AstNode *program = nodeIs(node, Metadata) ? node->metadata.node : node;
    csAssert0(program && nodeIs(program, Program));

    LowerContext context = {
        .driver = driver,
        .pool = driver->pool,
        .strings = driver->strings,
        .types = newIrTypeTable(driver->pool, driver->strings),
        .typeMap = newHashTable(sizeof(TypeMapping), driver->pool),
        .currentScopeId = 0,
        .nextScopeId = 1,
        .nextBlockId = 0,
        .tempCounter = 0,
        .scopes = newDynArray(sizeof(ScopeInfo)),
        .loopCtx = NULL};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astNullLit] = lowerNullLit,
        [astBoolLit] = lowerBoolLit,
        [astCharLit] = lowerCharLit,
        [astIntegerLit] = lowerIntegerLit,
        [astFloatLit] = lowerFloatLit,
        [astStringLit] = lowerStringLit,
        [astIdentifier] = lowerIdentifier,
        [astPath] = lowerPath,
        [astMemberExpr] = lowerMemberExpr,
        [astIndexExpr] = lowerIndexExpr,
        [astSliceExpr] = lowerSliceExpr,
        [astCatchExpr] = lowerCatchExpr,
        [astYieldStmt] = visitYieldStmt,
        [astBinaryExpr] = lowerBinaryExpr,
        [astUnaryExpr] = lowerUnaryExpr,
        [astPointerOf] = lowerPointerOf,
        [astReferenceOf] = lowerPointerOf,
        [astCastExpr] = lowerCastExpr,
        [astTypedExpr] = lowerTypedExpr,
        [astCallExpr] = lowerCallExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astGroupExpr] = visitGroupExpr,
        [astBackendCall] = visitBackendCall,
        [astArrayExpr] = visitArrayExpr,
        [astTupleExpr] = visitTupleExpr,
        [astStructExpr] = visitStructExpr,
        [astUnionValueExpr] = visitUnionValueExpr,
        [astClosureValue] = visitClosureValue,
        [astAliasExpr] = visitAliasExpr,
        [astVarDecl] = visitVarDecl,
        [astAssignExpr] = lowerAssignExpr,
        [astExprStmt] = visitExprStmt,
        [astStmtExpr] = visitStmtExpr,
        [astBlockStmt] = visitBlockStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astForStmt] = visitForStmt,
        [astBreakStmt] = visitBreakStmt,
        [astContinueStmt] = visitContinueStmt,
        [astReturnStmt] = visitReturnStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astMatchStmt] = visitMatchStmt,
        [astAsm] = visitAsmStmt,
        [astFuncDecl] = visitFuncDecl,
        [astExternDecl] = visitExternDecl,
        [astStructDecl] = visitClassOrStructDecl,
        [astClassDecl] = visitClassOrStructDecl
    });
    // clang-format on

    pushOnDynArray(&context.scopes,
                   &(ScopeInfo){.parent = 0, .hasDrops = false});
    astVisit(&visitor, program);

    finalizeLifecycleTypes(&context);
    freeDynArray(&context.scopes);

    if (nodeIs(node, Metadata)) {
        node->metadata.ir = context.module;
    }

    return context.module;
}
