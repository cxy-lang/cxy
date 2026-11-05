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

void lowerBackendCall(AstVisitor *visitor, AstNode *node)
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
        else if (isPointerType(allocType)) {
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
                                            flgNone,
                                            tempName,
                                            tempName,
                                            elemType,
                                            ctx->currentScopeId);
            markCompilerVar(tempVar, irClassTransient);
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

