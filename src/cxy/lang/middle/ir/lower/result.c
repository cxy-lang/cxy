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

static IrNode *getExceptionPushCallee(LowerContext *ctx)
{
    if (ctx->exceptionPushCallee)
        return ctx->exceptionPushCallee;

    const Type *baseExType = findBuiltinType(S_Exception);
    if (baseExType == NULL)
        return NULL;

    const AstNode *pushDecl = NULL;
    const AstNode *decl = getTypeDecl(baseExType);
    if (decl && nodeIs(decl, ClassDecl)) {
        for (const AstNode *mem = decl->classDecl.members; mem; mem = mem->next) {
            if (nodeIs(mem, FuncDecl) && mem->funcDecl.name == S_push) {
                pushDecl = mem;
                break;
            }
        }
    }

    const IrType *pushType = pushDecl ? lowerType(ctx, pushDecl->type) : NULL;
    ctx->exceptionPushCallee = makeIrGlobalRef(
        ctx->pool,
        builtinLoc(),
        "builtins_Exception_push",
        pushDecl ? pushDecl->ir : NULL,
        pushType);

    return ctx->exceptionPushCallee;
}

void emitExceptionTracePush(LowerContext *ctx,
                            const FileLoc *loc,
                            IrNode *errVal)
{
    if (errVal == NULL)
        return;

    IrNode *pushCallee = getExceptionPushCallee(ctx);
    if (pushCallee == NULL)
        return;

    const IrType *strType = makeIrPointerType(
        ctx->types, flgConst, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeChar));
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *voidType = getIrPrimitiveType(ctx->types, irTypeVoid);

    cstring modName = ctx->module ? ctx->module->module.name : "";
    cstring clsName = ctx->currentFunc && ctx->currentFunc->ast &&
                              ctx->currentFunc->ast->parentScope &&
                              isClassOrStructAstNode(ctx->currentFunc->ast->parentScope)
                          ? getDeclarationName(ctx->currentFunc->ast->parentScope)
                          : "";
    cstring funName = ctx->currentFunc
                          ? (ctx->currentFunc->function.display
                                 ?: ctx->currentFunc->function.name)
                          : "";
    cstring fileName = loc && loc->fileName ? loc->fileName : "";
    u64 row = loc ? loc->begin.row : 0;

    IrNode *arg0 = cloneIrNode(ctx->pool, errVal);
    IrNode *arg1 = makeIrStringConst(ctx->pool, loc, modName, strType);
    IrNode *arg2 = makeIrStringConst(ctx->pool, loc, clsName, strType);
    IrNode *arg3 = makeIrStringConst(ctx->pool, loc, funName, strType);
    IrNode *arg4 = makeIrStringConst(ctx->pool, loc, fileName, strType);
    IrNode *arg5 = makeIrIntConst(ctx->pool, loc, row, false, u64Type);

    arg0->next = arg1;
    arg1->next = arg2;
    arg2->next = arg3;
    arg3->next = arg4;
    arg4->next = arg5;

    IrNode *pushCall =
        makeIrCall(ctx->pool, loc, pushCallee, arg0, 6, voidType);
    emitStmt(ctx, pushCall);
}

IrNode *emitResultCheckAndUnwrap(LowerContext *ctx,
                                 AstNode *node,
                                 IrNode *resVar,
                                 const Type *resAstType)
{
    const IrType *resIrType = resVar->type;
    cstring errName = makeStringf(ctx->strings, "call_err_%u", ctx->nextIfId++);
    IrNode *errBB = createBlock(ctx, errName);
    cstring contName = makeStringf(ctx->strings, "call_cont_%u", ctx->nextIfId);
    IrNode *contBB = createBlock(ctx, contName);

    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, resVar), 0, S_tag, u32Type);
    IrNode *cond = makeIrBinary(
        ctx->pool,
        &node->loc,
        opEq,
        tagVal,
        makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type),
        getIrPrimitiveType(ctx->types, irTypeBool));
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, errBB, contBB));

    setBlock(ctx, errBB);
    if (ctx->funcRetVar && isResultType(ctx->funcRetAstType)) {
        IrNode *retVarRef = makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        IrNode *tagSlot = makeIrMember(
            ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              tagSlot,
                              makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));

        IrNode *resVarRef = makeIrVarRef(ctx->pool, &node->loc, resVar);
        const IrType *resValFieldType = resIrType->strct.fields[1].type;
        IrNode *resValMember = makeIrMember(
            ctx->pool, &node->loc, resVarRef, 1, S_val, resValFieldType);
        IrNode *errVal = resValMember;
        if (resValFieldType->tag == irTypeUntaggedUnion) {
            const Type *baseExType = findBuiltinType(S_Exception);
            const IrType *exIrType =
                baseExType ? lowerType(ctx, baseExType)
                           : getIrPrimitiveType(ctx->types, irTypeOpaque);
            errVal = makeIrMember(
                ctx->pool, &node->loc, resValMember, 1, NULL, exIrType);
        }

        emitExceptionTracePush(ctx, &node->loc, errVal);

        const IrType *retIrType = ctx->funcRetVar->type;
        const IrType *retValFieldType = retIrType->strct.fields[1].type;
        IrNode *retValMember = makeIrMember(
            ctx->pool, &node->loc, retVarRef, 1, S_val, retValFieldType);
        IrNode *assignStmt = NULL;
        if (retValFieldType->tag == irTypeUntaggedUnion) {
            const Type *baseExType = findBuiltinType(S_Exception);
            const IrType *exIrType =
                baseExType ? lowerType(ctx, baseExType)
                           : getIrPrimitiveType(ctx->types, irTypeOpaque);
            IrNode *errSlot = makeIrMember(
                ctx->pool, &node->loc, retValMember, 1, NULL, exIrType);
            assignStmt = makeIrAssign(ctx->pool, &node->loc, errSlot, errVal);
        }
        else {
            assignStmt = makeIrAssign(ctx->pool, &node->loc, retValMember, errVal);
        }
        assignStmt->flags |= flgUnmanaged;
        emitStmt(ctx, assignStmt);

        IrNode *resTagSlot = makeIrMember(
            ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, resVar), 0, S_tag, u32Type);
        IrNode *defuseStmt = makeIrAssign(
            ctx->pool, &node->loc, resTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);

        IrNode *targetBB = getOrCreateScopeReturnBlock(
            ctx, ctx->currentScopeId, &node->loc);
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, targetBB));
    }
    else {
        emitStmt(ctx, makeIrUnreachable(ctx->pool, &node->loc));
    }

    setBlock(ctx, contBB);
    if (isVoidResultType(resAstType)) {
        return NULL;
    }

    const Type *targetType = getResultTargetType(resAstType);
    const IrType *irTargetType = lowerType(ctx, targetType);
    IrNode *valMember = makeIrMember(
        ctx->pool,
        &node->loc,
        makeIrVarRef(ctx->pool, &node->loc, resVar),
        1,
        S_val,
        resIrType->strct.fields[1].type);
    IrNode *unwrapped = valMember;
    if (valMember->type && valMember->type->tag == irTypeUntaggedUnion) {
        unwrapped = makeIrMember(
            ctx->pool, &node->loc, valMember, 0, NULL, irTargetType);
    }
    return unwrapped;
}

// The payload of a `!T` call result is owned by the caller, the result
// temporary itself is never dropped
bool isOwnedResultPayload(const IrNode *expr)
{
    if (expr->tag != irMember || !isIrTypeDestructible(expr->type))
        return false;
    const IrNode *root = expr;
    while (root->tag == irMember)
        root = root->member.target;
    const IrNode *decl = root->tag == irVarRef ? root->varRef.target : NULL;
    return decl && hasFlag(decl, Temporary) && decl->type &&
           decl->type->category == irTypeCatResult;
}

// The success payload of a result value, `targetType` is the unwrapped type
static IrNode *makeResultPayloadRef(LowerContext *ctx,
                                    const FileLoc *loc,
                                    const IrNode *res,
                                    const IrType *resIrType,
                                    const Type *targetType)
{
    IrNode *valMember = makeIrMember(ctx->pool,
                                     loc,
                                     cloneIrNode(ctx->pool, res),
                                     1,
                                     S_val,
                                     resIrType->strct.fields[1].type);
    if (valMember->type && valMember->type->tag == irTypeUntaggedUnion) {
        return makeIrMember(
            ctx->pool, loc, valMember, 0, NULL, lowerType(ctx, targetType));
    }
    return valMember;
}

void lowerCatchExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    bool prevExplicitCatch = ctx->explicitCatch;
    ctx->explicitCatch = true;

    IrNode *res = lowerFlattenedOperand(visitor, node->catchExpr.expr);
    ctx->explicitCatch = prevExplicitCatch;

    const Type *exprType = node->catchExpr.expr->type;
    const Type *targetType = node->type;
    const IrType *resIrType = lowerType(ctx, exprType);

    cstring catchName = makeStringf(ctx->strings, "catch_%u", ctx->nextIfId++);
    IrNode *catchBB = createBlock(ctx, catchName);
    IrNode *continueBB = createBlock(ctx, makeStringf(ctx->strings, "cont_%u", ctx->nextIfId));
    IrNode *mergeBB = createBlock(ctx, makeStringf(ctx->strings, "merge_%u", ctx->nextIfId));

    IrNode *targetVar = NULL;
    bool hasResultValue = targetType && !isVoidType(targetType);
    if (hasResultValue) {
        cstring varName =
            makeStringf(ctx->strings, "__catch_res%u", ctx->tempCounter++);
        const IrType *irTargetType = lowerType(ctx, targetType);
        Flags flags = flgTemporary;
        if (isIrTypeDestructible(irTargetType))
            flags |= flgReferenceMembers;
        targetVar = makeIrVarDecl(ctx->pool,
                                  &node->loc,
                                  flags,
                                  varName,
                                  varName,
                                  irTargetType,
                                  ctx->currentScopeId);
        emitStmt(ctx, targetVar);
    }

    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->loc, cloneIrNode(ctx->pool, res), 0, S_tag, u32Type);
    IrNode *cond = makeIrBinary(
        ctx->pool,
        &node->loc,
        opEq,
        tagVal,
        makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type),
        getIrPrimitiveType(ctx->types, irTypeBool));
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, catchBB, continueBB));

    // Continue BB (success: tag == 0)
    setBlock(ctx, continueBB);
    if (hasResultValue) {
        IrNode *unwrapped =
            makeResultPayloadRef(ctx, &node->loc, res, resIrType, targetType);
        IrNode *assignStmt = makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, targetVar), unwrapped);
        assignStmt->flags |= flgUnmanaged;
        emitStmt(ctx, assignStmt);

        IrNode *resTagSlot = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, res), 0, S_tag, u32Type);
        IrNode *defuseStmt = makeIrAssign(ctx->pool, &node->loc, resTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);
    }
    else if (!isVoidResultType(exprType)) {
        // nobody consumes the payload of a discarded result, drop it in place
        const Type *payloadType = getResultTargetType(exprType);
        if (isIrTypeDestructible(lowerType(ctx, payloadType))) {
            emitStmt(ctx,
                     makeIrDrop(ctx->pool,
                                &node->loc,
                                makeResultPayloadRef(ctx,
                                                     &node->loc,
                                                     res,
                                                     resIrType,
                                                     payloadType)));
        }
    }
    emitBranchIfNotTerminated(ctx, mergeBB);

    // Catch BB (error: tag == 1)
    setBlock(ctx, catchBB);
    IrNode *valMember = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, res), 1, S_val, resIrType->strct.fields[1].type);
    IrNode *errVal = valMember;
    if (valMember->type && valMember->type->tag == irTypeUntaggedUnion) {
        const Type *baseExType = findBuiltinType(S_Exception);
        const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
        errVal = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
    }

    if (node->catchExpr.var) {
        node->catchExpr.var->codegen = errVal;
        node->catchExpr.var->ir = makeIrAlias(
            ctx->pool,
            &node->loc,
            node->catchExpr.var ? node->catchExpr.var->_name : S_ex,
            errVal);
    }

    AstNode *body = node->catchExpr.body;
    if (body) {
        IrNode *prevCatchErr = ctx->currentCatchErr;
        IrNode *prevCatchRes = ctx->currentCatchRes;
        ctx->currentCatchErr = errVal;
        ctx->currentCatchRes = res;
        CatchCtx catchCtx = {.err = errVal, .res = res, .prev = ctx->catchCtx};
        ctx->catchCtx = &catchCtx;

        if (nodeIs(body, BlockStmt)) {
            IrNode *prevCatchTarget = ctx->catchTargetVar;
            IrNode *prevCatchMerge = ctx->catchMergeBB;
            ctx->catchTargetVar = targetVar;
            ctx->catchMergeBB = mergeBB;

            astVisit(visitor, body);

            ctx->catchTargetVar = prevCatchTarget;
            ctx->catchMergeBB = prevCatchMerge;

            if (ctx->currentBlock && !isBlockTerminated(ctx->currentBlock)) {
                emitStmt(ctx,
                         makeIrDrop(ctx->pool,
                                    &node->loc,
                                    cloneIrNode(ctx->pool, errVal)));
                IrNode *resTagSlot = makeIrMember(
                    ctx->pool,
                    &node->loc,
                    cloneIrNode(ctx->pool, res),
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
            emitBranchIfNotTerminated(ctx, mergeBB);
        }
        else {
            IrNode *defVal = lowerFlattenedOperand(visitor, body);
            if (hasResultValue && defVal) {
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, targetVar), defVal));
            }
            if (ctx->currentBlock && !isBlockTerminated(ctx->currentBlock)) {
                emitStmt(ctx,
                         makeIrDrop(ctx->pool,
                                    &node->loc,
                                    cloneIrNode(ctx->pool, errVal)));
                IrNode *resTagSlot = makeIrMember(
                    ctx->pool,
                    &node->loc,
                    cloneIrNode(ctx->pool, res),
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
            emitBranchIfNotTerminated(ctx, mergeBB);
        }

        ctx->catchCtx = catchCtx.prev;
        ctx->currentCatchErr = prevCatchErr;
        ctx->currentCatchRes = prevCatchRes;
    }

    setBlock(ctx, mergeBB);
    if (hasResultValue) {
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, targetVar);
    }
}

