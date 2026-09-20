#include "node.h"

#include <string.h>

IrNode *makeIrNode(MemPool *pool, const FileLoc *loc, u64 flags, IrTag tag, const IrType *type)
{
    IrNode *node = allocFromMemPool(pool, sizeof(IrNode));
    memset(node, 0, sizeof(IrNode));
    node->tag = tag;
    node->loc = loc ? *loc : (FileLoc){};
    node->flags = flags;
    node->type = type;
    return node;
}

IrNode *makeIrModule(MemPool *pool, const FileLoc *loc, cstring name, IrTypeTable *types)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irModule, NULL);
    node->module.name = name;
    node->module.types = types;
    return node;
}

IrNode *makeIrFunction(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       cstring name,
                       cstring display,
                       const IrType *funcType,
                       IrNode *params,
                       u64 paramsCount)
{
    IrNode *node = makeIrNode(pool, loc, flags, irFunction, funcType);
    node->function.name = name;
    node->function.display = display ?: name;
    node->function.params = params;
    node->function.paramsCount = paramsCount;
    return node;
}

IrNode *makeIrBasicBlock(MemPool *pool, const FileLoc *loc, cstring name)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irBasicBlock, NULL);
    node->basicBlock.name = name;
    return node;
}

IrNode *makeIrFuncParam(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        cstring name,
                        u32 index,
                        const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flags, irFuncParam, type);
    node->funcParam.name = name;
    node->funcParam.index = index;
    return node;
}

IrNode *makeIrGlobalVar(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        cstring name,
                        const IrType *type,
                        IrNode *init)
{
    IrNode *node = makeIrNode(pool, loc, flags, irGlobalVar, type);
    node->globalVar.name = name;
    node->globalVar.init = init;
    return node;
}

IrNode *makeIrVarDecl(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      cstring name,
                      cstring display,
                      const IrType *type,
                      u32 scopeId)
{
    IrNode *node = makeIrNode(pool, loc, flags, irVarDecl, type);
    node->varDecl.name = name;
    node->varDecl.display = display ?: name;
    node->varDecl.scopeId = scopeId;
    return node;
}

IrNode *makeIrAssign(MemPool *pool, const FileLoc *loc, IrNode *lhs, IrNode *rhs)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irAssign, NULL);
    node->assign.lhs = lhs;
    node->assign.rhs = rhs;
    return node;
}

IrNode *makeIrScopeEnter(MemPool *pool, const FileLoc *loc, u32 scopeId)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irScopeEnter, NULL);
    node->scopeEnter.scopeId = scopeId;
    return node;
}

IrNode *makeIrScopeExit(MemPool *pool, const FileLoc *loc, u32 scopeId)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irScopeExit, NULL);
    node->scopeExit.scopeId = scopeId;
    return node;
}

IrNode *makeIrBranch(MemPool *pool, const FileLoc *loc, IrNode *target)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irBranch, NULL);
    node->branch.target = target;
    return node;
}

IrNode *makeIrBranchIf(MemPool *pool,
                       const FileLoc *loc,
                       IrNode *cond,
                       IrNode *trueTarget,
                       IrNode *falseTarget)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irBranchIf, NULL);
    node->branchIf.cond = cond;
    node->branchIf.trueTarget = trueTarget;
    node->branchIf.falseTarget = falseTarget;
    return node;
}

IrNode *makeIrSwitch(MemPool *pool,
                     const FileLoc *loc,
                     IrNode *cond,
                     IrCase *cases,
                     u64 casesCount,
                     IrNode *defaultTarget)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irSwitch, NULL);
    node->switchStmt.cond = cond;
    node->switchStmt.cases = cases;
    node->switchStmt.casesCount = casesCount;
    node->switchStmt.defaultTarget = defaultTarget;
    return node;
}

IrNode *makeIrReturn(MemPool *pool, const FileLoc *loc, IrNode *value)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irReturn, value ? value->type : NULL);
    node->returnStmt.value = value;
    return node;
}

IrNode *makeIrUnreachable(MemPool *pool, const FileLoc *loc)
{
    return makeIrNode(pool, loc, flgNone, irUnreachable, NULL);
}

IrNode *makeIrVarRef(MemPool *pool, const FileLoc *loc, IrNode *decl)
{
    csAssert0(decl);
    cstring name = decl->tag == irVarDecl ? decl->varDecl.name : (decl->tag == irFuncParam ? decl->funcParam.name : decl->globalVar.name);
    IrNode *node = makeIrNode(pool, loc, decl->flags, irVarRef, decl->type);
    node->varRef.target = decl;
    node->varRef.name = name;
    return node;
}

IrNode *makeIrGlobalRef(MemPool *pool,
                        const FileLoc *loc,
                        cstring name,
                        IrNode *target,
                        const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, target ? target->flags : flgNone, irGlobalRef, type);
    node->globalRef.name = name;
    node->globalRef.target = target;
    return node;
}

IrNode *makeIrMember(MemPool *pool,
                     const FileLoc *loc,
                     IrNode *target,
                     u32 index,
                     cstring name,
                     const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, target ? target->flags : flgNone, irMember, type);
    node->member.target = target;
    node->member.index = index;
    node->member.name = name;
    return node;
}

IrNode *makeIrIndex(MemPool *pool,
                    const FileLoc *loc,
                    IrNode *target,
                    IrNode *index,
                    const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, target ? target->flags : flgNone, irIndex, type);
    node->index.target = target;
    node->index.index = index;
    return node;
}

IrNode *makeIrCast(MemPool *pool, const FileLoc *loc, IrNode *expr, const IrType *toType)
{
    IrNode *node = makeIrNode(pool, loc, expr ? expr->flags : flgNone, irCast, toType);
    node->cast.expr = expr;
    return node;
}

IrNode *makeIrUnary(MemPool *pool,
                    const FileLoc *loc,
                    Operator op,
                    IrNode *operand,
                    const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, operand ? operand->flags : flgNone, irUnary, type);
    node->unary.op = op;
    node->unary.operand = operand;
    return node;
}

IrNode *makeIrBinary(MemPool *pool,
                     const FileLoc *loc,
                     Operator op,
                     IrNode *lhs,
                     IrNode *rhs,
                     const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irBinary, type);
    node->binary.op = op;
    node->binary.lhs = lhs;
    node->binary.rhs = rhs;
    return node;
}

IrNode *makeIrCall(MemPool *pool,
                   const FileLoc *loc,
                   IrNode *callee,
                   IrNode *args,
                   u64 argsCount,
                   const IrType *retType)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irCall, retType);
    node->call.callee = callee;
    node->call.args = args;
    node->call.argsCount = argsCount;
    return node;
}

IrNode *makeIrAddrOf(MemPool *pool, const FileLoc *loc, IrNode *target, const IrType *ptrType)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irAddrOf, ptrType);
    node->addrOf.target = target;
    return node;
}

IrNode *makeIrDeref(MemPool *pool, const FileLoc *loc, IrNode *target, const IrType *valType)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irDeref, valType);
    node->deref.target = target;
    return node;
}

IrNode *makeIrSizeof(MemPool *pool, const FileLoc *loc, const IrType *targetType)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irSizeof, NULL);
    node->sizeofVal.targetType = targetType;
    return node;
}

IrNode *makeIrAlloca(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irAlloca, NULL);
    node->alloca.elemType = elemType;
    node->alloca.size = size;
    return node;
}

IrNode *makeIrMalloc(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irMalloc, NULL);
    node->malloc.elemType = elemType;
    node->malloc.size = size;
    return node;
}

IrNode *makeIrMove(MemPool *pool, const FileLoc *loc, IrNode *src)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irMove, src ? src->type : NULL);
    node->move.src = src;
    return node;
}

IrNode *makeIrCopy(MemPool *pool, const FileLoc *loc, IrNode *src, IrNode *dst)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irCopy, src ? src->type : NULL);
    node->copy.src = src;
    node->copy.dst = dst;
    return node;
}

IrNode *makeIrDrop(MemPool *pool, const FileLoc *loc, IrNode *target)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irDrop, NULL);
    node->drop.target = target;
    return node;
}

IrNode *makeIrZeromem(MemPool *pool, const FileLoc *loc, IrNode *target)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irZeromem, NULL);
    node->zeromem.target = target;
    return node;
}

IrNode *makeIrNullConst(MemPool *pool, const FileLoc *loc, const IrType *type)
{
    return makeIrNode(pool, loc, flgNone, irNullConst, type);
}

IrNode *makeIrBoolConst(MemPool *pool, const FileLoc *loc, bool value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irBoolConst, type);
    node->boolConst.value = value;
    return node;
}

IrNode *makeIrCharConst(MemPool *pool, const FileLoc *loc, u32 value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irCharConst, type);
    node->charConst.value = value;
    return node;
}

IrNode *makeIrIntConst(MemPool *pool, const FileLoc *loc, u128 value, bool isNegative, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irIntConst, type);
    node->intConst.value = value;
    node->intConst.isNegative = isNegative;
    return node;
}

IrNode *makeIrFloatConst(MemPool *pool, const FileLoc *loc, f64 value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irFloatConst, type);
    node->floatConst.value = value;
    return node;
}

IrNode *makeIrStringConst(MemPool *pool, const FileLoc *loc, cstring value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irStringConst, type);
    node->stringConst.value = value;
    return node;
}

void irAppendBlockStmt(IrNode *block, IrNode *stmt)
{
    csAssert0(block && block->tag == irBasicBlock && stmt);
    stmt->parent = block;
    stmt->next = NULL;
    if (block->basicBlock.last) {
        block->basicBlock.last->next = stmt;
        block->basicBlock.last = stmt;
    }
    else {
        block->basicBlock.first = block->basicBlock.last = stmt;
    }
}

void irAppendFunctionBlock(IrNode *func, IrNode *block)
{
    csAssert0(func && func->tag == irFunction && block);
    block->parent = func;
    block->next = NULL;
    if (func->function.lastBlock) {
        func->function.lastBlock->next = block;
        func->function.lastBlock = block;
    }
    else {
        func->function.firstBlock = func->function.lastBlock = block;
    }
}
