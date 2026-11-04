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
#include "lang/middle/mangle.h"
#include "lang/middle/memory/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void lowerFuncDecl(AstVisitor *visitor, AstNode *node)
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
        const Type *pAstType = resolveUnThisUnwrapType(p->type);
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
        ctx->funcRetAstType =
            typeIs(node->type, Func) ? node->type->func.retType : NULL;
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

void lowerExternDecl(AstVisitor *visitor, AstNode *node)
{
    if (node->externDecl.func && hasFlag(node->externDecl.func, Extern)) {
        astVisit(visitor, node->externDecl.func);
    }
}

void lowerClassOrStructDecl(AstVisitor *visitor, AstNode *node)
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

void lowerProgram(AstVisitor *visitor, AstNode *node)
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

void finalizeLifecycleTypes(LowerContext *ctx)
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

void lowerAsmStmt(AstVisitor *visitor, AstNode *node)
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

