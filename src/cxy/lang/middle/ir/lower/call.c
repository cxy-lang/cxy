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

static const Type *getCalleeType(const AstNode *calleeNode)
{
    const Type *calleeType = resolveUnThisUnwrapType(calleeNode->type);
    if (calleeType == NULL) {
        const AstNode *calleeDecl =
            nodeIs(calleeNode, Identifier)
                ? calleeNode->ident.resolvesTo
                : (calleeNode->path.elements
                       ? calleeNode->path.elements->pathElement.resolvesTo
                       : NULL);
        if (calleeDecl && calleeDecl->type)
            calleeType = resolveUnThisUnwrapType(calleeDecl->type);
    }
    if (typeIs(calleeType, Pointer))
        calleeType = resolveUnThisUnwrapType(calleeType->pointer.pointed);
    return calleeType;
}

static IrNode *lowerCallArguments(LowerContext *ctx,
                                  AstVisitor *visitor,
                                  AstNode *args,
                                  const Type *calleeType,
                                  u64 startParamIdx,
                                  IrNode *initialArg,
                                  u64 *outCount)
{
    IrNode *argsFirst = initialArg;
    IrNode *argsLast = initialArg;
    u64 count = initialArg ? 1 : 0;
    u64 argIdx = startParamIdx;
    if (isClosureType(calleeType)) {
        calleeType = calleeType->closure.fn;
    }
    AstNode *func = calleeType->func.decl;
    AstNode *params = nodeIs(func, FuncDecl) ? func->funcDecl.signature->params
                                             : func->funcType.params,
            *param = params;
    for (AstNode *arg = args; arg; arg = arg->next, count++) {
        // Past the named params (or at the varargs marker): lower as-is
        bool isVarArg = param == NULL || hasFlag(param, Variadic);
        const Type *paramAstType = isVarArg ? NULL : param->type;
        if (!isVarArg)
            param = param->next;
        IrNode *loweredArg = NULL;
        if (isSliceType(paramAstType) && isArrayType(arg->type) &&
            !isSliceType(arg->type)) {
            const IrType *sliceIrType = lowerType(ctx, paramAstType);
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *sliceTemp = makeIrVarDecl(ctx->pool,
                                              &arg->loc,
                                              flgNone,
                                              tempName,
                                              tempName,
                                              sliceIrType,
                                              ctx->currentScopeId);
            markCompilerVar(sliceTemp, irClassTransient);
            emitStmt(ctx, sliceTemp);
            lowerSliceIntoTarget(ctx,
                                 visitor,
                                 arg,
                                 makeIrVarRef(ctx->pool, &arg->loc, sliceTemp));
            loweredArg = makeIrVarRef(ctx->pool, &arg->loc, sliceTemp);
        }
        else {
            loweredArg = lowerFlattenedOperand(visitor, arg);
        }

        if (argsLast) {
            argsLast->next = loweredArg;
            argsLast = loweredArg;
        }
        else {
            argsFirst = argsLast = loweredArg;
        }
    }

    if (outCount)
        *outCount = count;
    return argsFirst;
}

static void lowerOptionalCallExpr(AstVisitor *visitor,
                                  AstNode *node,
                                  const Type *calleeType)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    AstNode *targetAstNode = node->callExpr.args;
    IrNode *target = lowerFlattenedOperand(visitor, targetAstNode);
    const Type *targetAstType = targetAstNode->type;
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
    const IrType *resIrType = lowerType(ctx, node->type);

    IrNode *cond = NULL;
    IrNode *thisVal = NULL;
    if (isOptionalType(targetAstType)) {
        cond = makeIrMember(ctx->pool, &node->loc, target, 0, S_ok, boolType);
        thisVal = makeIrMember(ctx->pool,
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
        thisVal = target;
    }

    cstring tempName = makeStringf(ctx->strings, "__opt%u", ctx->tempCounter++);
    IrNode *resVar = makeIrVarDecl(ctx->pool,
                                   &node->loc,
                                   flgNone,
                                   tempName,
                                   tempName,
                                   resIrType,
                                   ctx->currentScopeId);
    markCompilerVar(resVar, irClassTransient);
    emitStmt(ctx, resVar);
    IrNode *resVarRef = makeIrVarRef(ctx->pool, &node->loc, resVar);

    u32 optId = ctx->nextOptId++;
    IrNode *thenBB =
        createBlock(ctx, makeStringf(ctx->strings, "OPTCALL%u_then", optId));
    IrNode *elseBB =
        createBlock(ctx, makeStringf(ctx->strings, "OPTCALL%u_else", optId));
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "OPTCALL%u_merge", optId));

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->callExpr.callee);
    IrNode *callee = (IrNode *)node->callExpr.callee->codegen;

    u64 argsCount = 0;
    IrNode *argsFirst = lowerCallArguments(ctx,
                                           visitor,
                                           node->callExpr.args->next,
                                           calleeType,
                                           1,
                                           thisVal,
                                           &argsCount);

    const IrType *valType = getIrOptionalTypeTarget(resIrType)
                                ?: getIrPrimitiveType(ctx->types, irTypeVoid);
    IrNode *callNode = makeIrCall(
        ctx->pool, &node->loc, callee, argsFirst, argsCount, valType);

    if (valType && valType->tag != irTypeVoid) {
        IrNode *okField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 0, S_ok, boolType);
        IrNode *valField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 1, S_val, valType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     okField,
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valField, callNode));
    }
    else {
        emitStmt(ctx, callNode);
        IrNode *okField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 0, S_ok, boolType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     okField,
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
    }
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    IrNode *elseOkField =
        makeIrMember(ctx->pool,
                     &node->loc,
                     makeIrVarRef(ctx->pool, &node->loc, resVar),
                     0,
                     S_ok,
                     boolType);
    emitStmt(
        ctx,
        makeIrAssign(ctx->pool,
                     &node->loc,
                     elseOkField,
                     makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, resVar);
}

void lowerCallExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const AstNode *calleeNode = node->callExpr.callee;
    bool isOptCall =
        node->callExpr.isOptional ||
        (nodeIs(calleeNode, MemberExpr) && calleeNode->memberExpr.isOptional);

    const Type *calleeType = getCalleeType(calleeNode);
    if (isOptCall && node->callExpr.args) {
        lowerOptionalCallExpr(visitor, node, calleeType);
        return;
    }

    // `.fn` and `.env` are both read from a closure, so a callee that is not a
    // variable is evaluated once, into a local
    IrNode *callee = NULL;
    if (isClosureType(calleeType)) {
        callee = lowerFlattenedOperand(visitor, node->callExpr.callee);
    }
    else {
        astVisit(visitor, node->callExpr.callee);
        callee = (IrNode *)node->callExpr.callee->codegen;
    }
    u64 argsCount = 0;
    IrNode *argsFirst = lowerCallArguments(
        ctx, visitor, node->callExpr.args, calleeType, 0, NULL, &argsCount);

    if (isClosureType(calleeType)) {
        // the callee is read twice, it is a value that can be read again
        csAssert0(callee->tag != irClosureValue && callee->tag != irCall);
        const IrType *voidPtrType = makeIrPointerType(
            ctx->types,
            flgNone, irTypeCatNone,
            getIrPrimitiveType(ctx->types, irTypeVoid));
        const IrType *fnPtrType = lowerClosureFnPtrType(ctx, calleeType);
        IrNode *fnCallee = makeIrMember(
            ctx->pool, &node->loc, cloneIrNode(ctx->pool, callee), 1, S_fn, fnPtrType);
        IrNode *envArg =
            makeIrMember(ctx->pool, &node->loc, callee, 0, S_env, voidPtrType);
        envArg->next = argsFirst;
        argsFirst = envArg;
        argsCount++;
        callee = fnCallee;
    }

    const Type *calleeRetAstType =
        calleeType
            ? (isClosureType(calleeType) ? calleeType->closure.fn->func.retType
                                         : calleeType->func.retType)
            : node->type;
    calleeRetAstType = resolveUnThisUnwrapType(calleeRetAstType);
    const IrType *callRetIrType = lowerType(ctx, calleeRetAstType);

    IrNode *callNode = makeIrCall(ctx->pool,
                                  &node->loc,
                                  callee,
                                  argsFirst,
                                  argsCount,
                                  callRetIrType);

    if (!ctx->explicitCatch && isResultType(calleeRetAstType)) {
        const IrType *resIrType = callRetIrType;
        cstring tempName =
            makeStringf(ctx->strings, "__call_res_%u", ctx->tempCounter++);
        Flags flags = flgNone;
        if (isIrTypeDestructible(resIrType))
            flags |= flgReferenceMembers;
        IrNode *resVar = makeIrVarDecl(ctx->pool,
                                       &node->loc,
                                       flags,
                                       tempName,
                                       tempName,
                                       resIrType,
                                       ctx->currentScopeId);
        markCompilerVar(resVar, irClassResult);
        emitTempDecl(ctx, resVar);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, resVar),
                              callNode));

        node->codegen =
            emitResultCheckAndUnwrap(ctx, node, resVar, calleeRetAstType);
    }
    else {
        node->codegen = callNode;
    }
}

