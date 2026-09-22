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
