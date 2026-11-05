#include "node.h"

#include <string.h>

// A move, also behind casts (`<Base>(&&x)`)
static bool isMoveOperand(const IrNode *node)
{
    while (node && node->tag == irCast)
        node = node->cast.expr;
    return node && node->tag == irMove;
}

// The constructors of nodes that consume an operand mark it, so the pass reads
// one bit instead of knowing the shape. A value that owns nothing is not marked,
// but a move always is: the pass has to find it to take the variable out of play.
static void markOwnedOperand(IrNode *operand)
{
    if (operand && (isIrTypeDestructible(operand->type) || isMoveOperand(operand)))
        irSetInfo(operand, irInfoOwnedPosition);
}

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

// Matches `<token>` at the start or after '_' (module prefix), ending at '\0' or '_'
static bool hasFiniToken(cstring name, const char *token)
{
    size_t len = strlen(token);
    for (const char *p = strstr(name, token); p; p = strstr(p + 1, token)) {
        bool startOk = p == name || p[-1] == '_';
        bool endOk = p[len] == '\0' || p[len] == '_';
        if (startOk && endOk)
            return true;
    }
    return false;
}

int irModuleFiniKind(const IrNode *func)
{
    if (func == NULL || func->tag != irFunction || func->function.name == NULL)
        return 0;
    cstring name = func->function.name;
    if (hasFiniToken(name, "__fini"))
        return 1;
    if (hasFiniToken(name, "__tfini"))
        return 2;
    return 0;
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
    markOwnedOperand(rhs);
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

IrNode *makeIrInlineAsm(MemPool *pool,
                        const FileLoc *loc,
                        cstring text,
                        IrNode *outputs,
                        IrNode *inputs,
                        IrNode *clobbers,
                        bool isVolatile)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irInlineAsm, NULL);
    node->inlineAsm.text = text;
    node->inlineAsm.outputs = outputs;
    node->inlineAsm.inputs = inputs;
    node->inlineAsm.clobbers = clobbers;
    node->inlineAsm.isVolatile = isVolatile;
    return node;
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

IrNode *makeIrAlias(MemPool *pool, const FileLoc *loc, cstring name, IrNode *expr)
{
    IrNode *node = makeIrNode(pool, loc, expr ? expr->flags : flgNone, irAlias, expr ? expr->type : NULL);
    node->alias.name = name;
    node->alias.expr = expr;
    return node;
}

IrNode *cloneIrNode(MemPool *pool, const IrNode *node)
{
    if (node == NULL)
        return NULL;

    IrNode *copy = makeIrNode(pool, &node->loc, node->flags, node->tag, node->type);
    copy->ast = node->ast;

    switch (node->tag) {
    case irBoolConst:
        copy->boolConst = node->boolConst;
        break;
    case irCharConst:
        copy->charConst = node->charConst;
        break;
    case irIntConst:
        copy->intConst = node->intConst;
        break;
    case irFloatConst:
        copy->floatConst = node->floatConst;
        break;
    case irStringConst:
        copy->stringConst = node->stringConst;
        break;
    case irNullConst:
        break;
    case irArrayConst:
        copy->arrayConst.count = node->arrayConst.count;
        copy->arrayConst.elements = cloneIrNodes(pool, node->arrayConst.elements);
        break;
    case irFieldConst:
        copy->fieldConst.name = node->fieldConst.name;
        copy->fieldConst.index = node->fieldConst.index;
        copy->fieldConst.value = cloneIrNode(pool, node->fieldConst.value);
        break;
    case irStructConst:
        copy->structConst.count = node->structConst.count;
        copy->structConst.fields = cloneIrNodes(pool, node->structConst.fields);
        break;
    case irGlobalRef:
        copy->globalRef = node->globalRef;
        break;
    case irVarRef:
        copy->varRef = node->varRef;
        break;
    case irMember:
        copy->member.target = cloneIrNode(pool, node->member.target);
        copy->member.index = node->member.index;
        copy->member.name = node->member.name;
        break;
    case irIndex:
        copy->index.target = cloneIrNode(pool, node->index.target);
        copy->index.index = cloneIrNode(pool, node->index.index);
        break;
    case irCast:
        copy->cast.expr = cloneIrNode(pool, node->cast.expr);
        break;
    case irUnary:
        copy->unary.op = node->unary.op;
        copy->unary.operand = cloneIrNode(pool, node->unary.operand);
        break;
    case irBinary:
        copy->binary.op = node->binary.op;
        copy->binary.lhs = cloneIrNode(pool, node->binary.lhs);
        copy->binary.rhs = cloneIrNode(pool, node->binary.rhs);
        break;
    case irAddrOf:
        copy->addrOf.target = cloneIrNode(pool, node->addrOf.target);
        break;
    case irDeref:
        copy->deref.target = cloneIrNode(pool, node->deref.target);
        break;
    case irSizeof:
        copy->sizeofVal.targetType = node->sizeofVal.targetType;
        break;
    case irAlias:
        copy->alias.name = node->alias.name;
        copy->alias.expr = cloneIrNode(pool, node->alias.expr);
        break;
    case irOptionalValue:
        copy->optionalValue.hasValue = node->optionalValue.hasValue;
        copy->optionalValue.value = cloneIrNode(pool, node->optionalValue.value);
        break;
    case irUnionValue:
        copy->unionValue.index = node->unionValue.index;
        copy->unionValue.value = cloneIrNode(pool, node->unionValue.value);
        break;
    case irClosureValue:
        copy->closureValue.env = cloneIrNode(pool, node->closureValue.env);
        copy->closureValue.func = cloneIrNode(pool, node->closureValue.func);
        copy->closureValue.dtor = cloneIrNode(pool, node->closureValue.dtor);
        break;
    case irAlloc:
        copy->alloc.type = node->alloc.type;
        break;
    case irMove:
        copy->move.src = cloneIrNode(pool, node->move.src);
        break;
    case irDrop:
        copy->drop.target = cloneIrNode(pool, node->drop.target);
        copy->drop.flag = cloneIrNode(pool, node->drop.flag);
        break;
    case irZeromem:
        copy->zeromem.target = cloneIrNode(pool, node->zeromem.target);
        break;
    case irCopy:
        copy->copy.src = cloneIrNode(pool, node->copy.src);
        copy->copy.dst = cloneIrNode(pool, node->copy.dst);
        break;
    default:
        *copy = *node;
        copy->next = NULL;
        copy->parent = NULL;
        break;
    }
    return copy;
}

IrNode *cloneIrNodes(MemPool *pool, const IrNode *node)
{
    if (node == NULL)
        return NULL;
    IrNode *first = cloneIrNode(pool, node);
    IrNode *curr = first;
    for (const IrNode *it = node->next; it; it = it->next) {
        curr->next = cloneIrNode(pool, it);
        curr = curr->next;
    }
    return first;
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

    // A reference parameter borrows. Variadic arguments go by their own type.
    const IrType *calleeType = callee ? callee->type : NULL;
    if (calleeType && calleeType->tag == irTypeFunc) {
        u64 i = 0;
        for (IrNode *arg = args; arg; arg = arg->next, i++) {
            const IrType *param = i < calleeType->func.count ? calleeType->func.params[i] : arg->type;
            if (param && param->category != irTypeCatReference)
                markOwnedOperand(arg);
        }
    }
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

IrNode *makeIrSizeof(MemPool *pool, const FileLoc *loc, const IrType *targetType, const IrType *u64Type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irSizeof, u64Type);
    node->sizeofVal.targetType = targetType;
    return node;
}

IrNode *makeIrAlloca(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size, const IrType *ptrType)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irAlloca, ptrType);
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

IrNode *makeIrOptionalValue(MemPool *pool, const FileLoc *loc, IrNode *value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irOptionalValue, type);
    node->optionalValue.hasValue = (value != NULL);
    node->optionalValue.value = value;
    markOwnedOperand(value);
    return node;
}

IrNode *makeIrUnionValue(MemPool *pool, const FileLoc *loc, u32 index, IrNode *value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irUnionValue, type);
    node->unionValue.index = index;
    node->unionValue.value = value;
    markOwnedOperand(value);
    return node;
}

IrNode *makeIrAlloc(MemPool *pool, const FileLoc *loc, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irAlloc, type);
    node->alloc.type = type;
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
    node->drop.flag = NULL;
    return node;
}

IrNode *makeIrDropIf(MemPool *pool, const FileLoc *loc, IrNode *target, IrNode *flag)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irDrop, NULL);
    node->drop.target = target;
    node->drop.flag = flag;
    return node;
}

IrNode *makeIrZeromem(MemPool *pool, const FileLoc *loc, IrNode *target)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irZeromem, NULL);
    node->zeromem.target = target;
    return node;
}

IrNode *makeIrClosureValue(MemPool *pool, const FileLoc *loc, IrNode *env, IrNode *func, IrNode *dtor, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irClosureValue, type);
    node->closureValue.env = env;
    node->closureValue.func = func;
    node->closureValue.dtor = dtor;
    markOwnedOperand(env);
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

IrNode *makeIrArrayConst(MemPool *pool, const FileLoc *loc, IrNode *elements, u64 count, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irArrayConst, type);
    node->arrayConst.elements = elements;
    node->arrayConst.count = count;
    for (IrNode *e = elements; e; e = e->next)
        markOwnedOperand(e);
    return node;
}

IrNode *makeIrFieldConst(MemPool *pool, const FileLoc *loc, cstring name, u32 index, IrNode *value, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irFieldConst, type);
    node->fieldConst.name = name;
    node->fieldConst.index = index;
    node->fieldConst.value = value;
    return node;
}

IrNode *makeIrStructConst(MemPool *pool, const FileLoc *loc, IrNode *fields, u64 count, const IrType *type)
{
    IrNode *node = makeIrNode(pool, loc, flgNone, irStructConst, type);
    node->structConst.fields = fields;
    node->structConst.count = count;
    for (IrNode *f = fields; f; f = f->next)
        markOwnedOperand(f->tag == irFieldConst ? f->fieldConst.value : f);
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
