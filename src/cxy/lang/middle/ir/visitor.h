#pragma once

#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IrVisitor IrVisitor;
typedef void (*IrVisitorFn)(struct IrVisitor *, IrNode *);

typedef struct IrVisitor {
    void *context;
    IrNode *current;
    IrNode **currentList;
    void (*visitors[irCOUNT])(struct IrVisitor *, IrNode *node);
    void (*fallback)(struct IrVisitor *, IrNode *);
    void (*dispatch)(IrVisitorFn, struct IrVisitor *, IrNode *);
} IrVisitor;

typedef struct ConstIrVisitor ConstIrVisitor;
typedef void (*ConstIrVisitorFn)(struct ConstIrVisitor *, const IrNode *);

typedef struct ConstIrVisitor {
    void *context;
    const IrNode *current;
    IrNode *next;
    void (*visitors[irCOUNT])(struct ConstIrVisitor *, const IrNode *node);
    void (*fallback)(struct ConstIrVisitor *, const IrNode *);
    void (*dispatch)(ConstIrVisitorFn, struct ConstIrVisitor *, const IrNode *);
} ConstIrVisitor;

typedef struct IrModifier {
    IrNode *parent;
    IrNode *previous;
    IrNode *current;
} IrModifier;

#define getIrVisitorContext(V) ((IrVisitor *)(V))->context
#define getConstIrVisitorContext(V) ((ConstIrVisitor *)(V))->context

#define makeIrVisitor(C, ...) (IrVisitor){.context = (C), .visitors = __VA_ARGS__}
#define makeConstIrVisitor(C, ...) (ConstIrVisitor){.context = (C), .visitors = __VA_ARGS__}

void irVisit(IrVisitor *visitor, IrNode *node);
void irConstVisit(ConstIrVisitor *visitor, const IrNode *node);

void irVisitManyNodes(IrVisitor *visitor, IrNode *node);
void irConstVisitManyNodes(ConstIrVisitor *visitor, const IrNode *node);

void irVisitFallbackVisitAll(IrVisitor *visitor, IrNode *node);
void irConstVisitFallbackVisitAll(ConstIrVisitor *visitor, const IrNode *node);

// Calls `cb` with the slot of every operand of `node`, in evaluation order. A
// slot is the pointer that holds the operand, so `cb` can replace it. Operands
// of a list (call arguments, literal elements) are visited one by one.
void irForEachOperandSlot(IrNode *node,
                          void (*cb)(IrNode **slot, void *ctx),
                          void *ctx);

void irVisitSkip(IrVisitor *visitor, IrNode *node);
void irConstVisitSkip(ConstIrVisitor *visitor, const IrNode *node);

void irModifierInit(IrModifier *modifier, IrNode *parent);
void irModifierAdd(IrModifier *modifier, IrNode *node);
void irModifierRemove(IrModifier *modifier);
void irModifierNext(IrModifier *modifier, IrNode *node);

#ifdef __cplusplus
}
#endif
