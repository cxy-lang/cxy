#include "visitor.h"

void irVisit(IrVisitor *visitor, IrNode *node)
{
    if (node == NULL)
        return;
    IrVisitorFn fn = visitor->visitors[node->tag] ?: visitor->fallback;
    if (fn) {
        if (visitor->dispatch)
            visitor->dispatch(fn, visitor, node);
        else
            fn(visitor, node);
    }
}

void irConstVisit(ConstIrVisitor *visitor, const IrNode *node)
{
    if (node == NULL)
        return;
    ConstIrVisitorFn fn = visitor->visitors[node->tag] ?: visitor->fallback;
    if (fn) {
        if (visitor->dispatch)
            visitor->dispatch(fn, visitor, node);
        else
            fn(visitor, node);
    }
}

void irVisitManyNodes(IrVisitor *visitor, IrNode *node)
{
    for (IrNode *it = node; it; it = it->next)
        irVisit(visitor, it);
}

void irConstVisitManyNodes(ConstIrVisitor *visitor, const IrNode *node)
{
    for (const IrNode *it = node; it; it = it->next)
        irConstVisit(visitor, it);
}

void irVisitFallbackVisitAll(IrVisitor *visitor, IrNode *node)
{
    if (node == NULL)
        return;

    switch (node->tag) {
    case irModule:
        irVisitManyNodes(visitor, node->module.globals);
        irVisitManyNodes(visitor, node->module.functions);
        break;
    case irFunction:
        irVisitManyNodes(visitor, node->function.params);
        irVisitManyNodes(visitor, node->function.firstBlock);
        break;
    case irBasicBlock:
        irVisitManyNodes(visitor, node->basicBlock.first);
        break;
    case irVarDecl:
        break;
    case irAssign:
        irVisit(visitor, node->assign.lhs);
        irVisit(visitor, node->assign.rhs);
        break;
    case irBranchIf:
        irVisit(visitor, node->branchIf.cond);
        break;
    case irSwitch:
        irVisit(visitor, node->switchStmt.cond);
        for (u64 i = 0; i < node->switchStmt.casesCount; i++) {
            if (node->switchStmt.cases[i].valNode)
                irVisit(visitor, node->switchStmt.cases[i].valNode);
            if (node->switchStmt.cases[i].endNode)
                irVisit(visitor, node->switchStmt.cases[i].endNode);
        }
        break;
    case irReturn:
        irVisit(visitor, node->returnStmt.value);
        break;
    case irCall:
        irVisit(visitor, node->call.callee);
        irVisitManyNodes(visitor, node->call.args);
        break;
    case irUnary:
        irVisit(visitor, node->unary.operand);
        break;
    case irBinary:
        irVisit(visitor, node->binary.lhs);
        irVisit(visitor, node->binary.rhs);
        break;
    case irCast:
        irVisit(visitor, node->cast.expr);
        break;
    case irMember:
        irVisit(visitor, node->member.target);
        break;
    case irIndex:
        irVisit(visitor, node->index.target);
        irVisit(visitor, node->index.index);
        break;
    case irAddrOf:
        irVisit(visitor, node->addrOf.target);
        break;
    case irDeref:
        irVisit(visitor, node->deref.target);
        break;
    case irAlias:
        irVisit(visitor, node->alias.expr);
        break;
    case irOptionalValue:
        if (node->optionalValue.hasValue)
            irVisit(visitor, node->optionalValue.value);
        break;
    case irUnionValue:
        irVisit(visitor, node->unionValue.value);
        break;
    case irClosureValue:
        if (node->closureValue.env)
            irVisit(visitor, node->closureValue.env);
        irVisit(visitor, node->closureValue.func);
        if (node->closureValue.dtor)
            irVisit(visitor, node->closureValue.dtor);
        break;
    case irCopy:
        irVisit(visitor, node->copy.src);
        irVisit(visitor, node->copy.dst);
        break;
    case irMove:
        irVisit(visitor, node->move.src);
        break;
    case irDrop:
        irVisit(visitor, node->drop.target);
        if (node->drop.flag)
            irVisit(visitor, node->drop.flag);
        break;
    case irZeromem:
        irVisit(visitor, node->zeromem.target);
        break;
    case irFieldConst:
        irVisit(visitor, node->fieldConst.value);
        break;
    case irStructConst:
        irVisitManyNodes(visitor, node->structConst.fields);
        break;
    case irArrayConst:
        irVisitManyNodes(visitor, node->arrayConst.elements);
        break;
    default:
        break;
    }
}

void irConstVisitFallbackVisitAll(ConstIrVisitor *visitor, const IrNode *node)
{
    if (node == NULL)
        return;

    switch (node->tag) {
    case irModule:
        irConstVisitManyNodes(visitor, node->module.globals);
        irConstVisitManyNodes(visitor, node->module.functions);
        break;
    case irFunction:
        irConstVisitManyNodes(visitor, node->function.params);
        irConstVisitManyNodes(visitor, node->function.firstBlock);
        break;
    case irBasicBlock:
        irConstVisitManyNodes(visitor, node->basicBlock.first);
        break;
    case irVarDecl:
        break;
    case irAssign:
        irConstVisit(visitor, node->assign.lhs);
        irConstVisit(visitor, node->assign.rhs);
        break;
    case irBranchIf:
        irConstVisit(visitor, node->branchIf.cond);
        break;
    case irSwitch:
        irConstVisit(visitor, node->switchStmt.cond);
        for (u64 i = 0; i < node->switchStmt.casesCount; i++) {
            if (node->switchStmt.cases[i].valNode)
                irConstVisit(visitor, node->switchStmt.cases[i].valNode);
            if (node->switchStmt.cases[i].endNode)
                irConstVisit(visitor, node->switchStmt.cases[i].endNode);
        }
        break;
    case irReturn:
        irConstVisit(visitor, node->returnStmt.value);
        break;
    case irCall:
        irConstVisit(visitor, node->call.callee);
        irConstVisitManyNodes(visitor, node->call.args);
        break;
    case irUnary:
        irConstVisit(visitor, node->unary.operand);
        break;
    case irBinary:
        irConstVisit(visitor, node->binary.lhs);
        irConstVisit(visitor, node->binary.rhs);
        break;
    case irCast:
        irConstVisit(visitor, node->cast.expr);
        break;
    case irMember:
        irConstVisit(visitor, node->member.target);
        break;
    case irIndex:
        irConstVisit(visitor, node->index.target);
        irConstVisit(visitor, node->index.index);
        break;
    case irAddrOf:
        irConstVisit(visitor, node->addrOf.target);
        break;
    case irDeref:
        irConstVisit(visitor, node->deref.target);
        break;
    case irAlias:
        irConstVisit(visitor, node->alias.expr);
        break;
    case irOptionalValue:
        if (node->optionalValue.hasValue)
            irConstVisit(visitor, node->optionalValue.value);
        break;
    case irUnionValue:
        irConstVisit(visitor, node->unionValue.value);
        break;
    case irClosureValue:
        if (node->closureValue.env)
            irConstVisit(visitor, node->closureValue.env);
        irConstVisit(visitor, node->closureValue.func);
        if (node->closureValue.dtor)
            irConstVisit(visitor, node->closureValue.dtor);
        break;
    case irCopy:
        irConstVisit(visitor, node->copy.src);
        irConstVisit(visitor, node->copy.dst);
        break;
    case irMove:
        irConstVisit(visitor, node->move.src);
        break;
    case irDrop:
        irConstVisit(visitor, node->drop.target);
        if (node->drop.flag)
            irConstVisit(visitor, node->drop.flag);
        break;
    case irZeromem:
        irConstVisit(visitor, node->zeromem.target);
        break;
    case irFieldConst:
        irConstVisit(visitor, node->fieldConst.value);
        break;
    case irStructConst:
        irConstVisitManyNodes(visitor, node->structConst.fields);
        break;
    case irArrayConst:
        irConstVisitManyNodes(visitor, node->arrayConst.elements);
        break;
    default:
        break;
    }
}

static void forEachListSlot(IrNode **first,
                            void (*cb)(IrNode **slot, void *ctx),
                            void *ctx)
{
    for (IrNode **slot = first; *slot; slot = &(*slot)->next)
        cb(slot, ctx);
}

void irForEachOperandSlot(IrNode *node,
                          void (*cb)(IrNode **slot, void *ctx),
                          void *ctx)
{
    if (node == NULL)
        return;

    switch (node->tag) {
    case irAssign:
        cb(&node->assign.lhs, ctx);
        cb(&node->assign.rhs, ctx);
        break;
    case irBranchIf:
        cb(&node->branchIf.cond, ctx);
        break;
    case irSwitch:
        cb(&node->switchStmt.cond, ctx);
        break;
    case irReturn:
        if (node->returnStmt.value)
            cb(&node->returnStmt.value, ctx);
        break;
    case irCall:
        cb(&node->call.callee, ctx);
        forEachListSlot(&node->call.args, cb, ctx);
        break;
    case irUnary:
        cb(&node->unary.operand, ctx);
        break;
    case irBinary:
        cb(&node->binary.lhs, ctx);
        cb(&node->binary.rhs, ctx);
        break;
    case irCast:
        cb(&node->cast.expr, ctx);
        break;
    case irMember:
        cb(&node->member.target, ctx);
        break;
    case irIndex:
        cb(&node->index.target, ctx);
        cb(&node->index.index, ctx);
        break;
    case irAddrOf:
        cb(&node->addrOf.target, ctx);
        break;
    case irDeref:
        cb(&node->deref.target, ctx);
        break;
    case irAlias:
        cb(&node->alias.expr, ctx);
        break;
    case irOptionalValue:
        if (node->optionalValue.hasValue)
            cb(&node->optionalValue.value, ctx);
        break;
    case irUnionValue:
        if (node->unionValue.value)
            cb(&node->unionValue.value, ctx);
        break;
    case irClosureValue:
        if (node->closureValue.env)
            cb(&node->closureValue.env, ctx);
        break;
    case irCopy:
        cb(&node->copy.src, ctx);
        cb(&node->copy.dst, ctx);
        break;
    case irMove:
        cb(&node->move.src, ctx);
        break;
    case irDrop:
        cb(&node->drop.target, ctx);
        break;
    case irZeromem:
        cb(&node->zeromem.target, ctx);
        break;
    case irFieldConst:
        cb(&node->fieldConst.value, ctx);
        break;
    case irStructConst:
        forEachListSlot(&node->structConst.fields, cb, ctx);
        break;
    case irArrayConst:
        forEachListSlot(&node->arrayConst.elements, cb, ctx);
        break;
    default:
        break;
    }
}

void irVisitSkip(IrVisitor *visitor, IrNode *node) { (void)visitor; (void)node; }
void irConstVisitSkip(ConstIrVisitor *visitor, const IrNode *node) { (void)visitor; (void)node; }

void irModifierInit(IrModifier *modifier, IrNode *parent)
{
    modifier->parent = parent;
    modifier->previous = NULL;
    modifier->current = NULL;
}

void irModifierAdd(IrModifier *modifier, IrNode *node)
{
    if (modifier->previous == NULL) {
        if (modifier->parent && modifier->parent->tag == irBasicBlock) {
            node->next = modifier->parent->basicBlock.first;
            modifier->parent->basicBlock.first = node;
            if (!modifier->parent->basicBlock.last)
                modifier->parent->basicBlock.last = node;
        }
    }
    else {
        node->next = modifier->previous->next;
        modifier->previous->next = node;
    }
    modifier->previous = node;
}

void irModifierRemove(IrModifier *modifier)
{
    if (modifier->current == NULL)
        return;
    if (modifier->previous == NULL) {
        if (modifier->parent && modifier->parent->tag == irBasicBlock) {
            modifier->parent->basicBlock.first = modifier->current->next;
            if (modifier->parent->basicBlock.last == modifier->current)
                modifier->parent->basicBlock.last = NULL;
        }
    }
    else {
        modifier->previous->next = modifier->current->next;
        if (modifier->parent && modifier->parent->tag == irBasicBlock &&
            modifier->parent->basicBlock.last == modifier->current)
            modifier->parent->basicBlock.last = modifier->previous;
    }
}

void irModifierNext(IrModifier *modifier, IrNode *node)
{
    modifier->previous = modifier->current;
    modifier->current = node;
}
