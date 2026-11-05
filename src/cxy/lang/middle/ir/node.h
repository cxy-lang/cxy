#pragma once

#include "type.h"

#include "core/log.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/operator.h"

#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define CXY_IR_CONST_TAGS(f)        \
    f(NullConst)                    \
    f(BoolConst)                    \
    f(CharConst)                    \
    f(IntConst)                     \
    f(FloatConst)                   \
    f(StringConst)                  \
    f(ArrayConst)                   \
    f(FieldConst)                   \
    f(StructConst)                  \
    f(GlobalRef)

#define CXY_IR_VALUE_TAGS(f)        \
    f(VarRef)                       \
    f(Member)                       \
    f(Index)                        \
    f(Cast)                         \
    f(Unary)                        \
    f(Binary)                       \
    f(Call)                         \
    f(AddrOf)                       \
    f(Deref)                        \
    f(Alloca)                       \
    f(Malloc)                       \
    f(Sizeof)                       \
    f(Alias)                        \
    f(OptionalValue)                \
    f(UnionValue)                   \
    f(ClosureValue)

#define CXY_IR_RAII_TAGS(f)         \
    f(Alloc)                        \
    f(Copy)                         \
    f(Move)                         \
    f(Drop)                         \
    f(Zeromem)

#define CXY_IR_STMT_TAGS(f)         \
    f(VarDecl)                      \
    f(Assign)                       \
    f(ScopeEnter)                   \
    f(ScopeExit)                    \
    f(Branch)                       \
    f(BranchIf)                     \
    f(Switch)                       \
    f(Return)                       \
    f(Unreachable)                  \
    f(InlineAsm)

#define CXY_IR_DECL_TAGS(f)         \
    f(BasicBlock)                   \
    f(FuncParam)                    \
    f(Function)                     \
    f(GlobalVar)                    \
    f(Module)

#define CXY_IR_TAGS(f)              \
    CXY_IR_CONST_TAGS(f)            \
    CXY_IR_VALUE_TAGS(f)            \
    CXY_IR_RAII_TAGS(f)             \
    CXY_IR_STMT_TAGS(f)             \
    CXY_IR_DECL_TAGS(f)
// clang-format on

typedef enum IrTag {
#define f(NAME) ir##NAME,
    CXY_IR_TAGS(f)
#undef f
    irCOUNT
} IrTag;

struct AstNode;
typedef struct IrNode IrNode;
typedef struct IrBasicBlock IrBasicBlock;

#define CXY_IR_NODE_HEAD                                                       \
    IrTag tag;                                                                 \
    u32 info;                                                                  \
    FileLoc loc;                                                               \
    u64 flags;                                                                 \
    const IrType *type;                                                        \
    IrNode *parent;                                                            \
    IrNode *next;                                                              \
    struct AstNode *ast;                                                       \
    void *codegen;

typedef struct IrCase {
    IrNode *valNode;
    IrNode *endNode;
    IrNode *target;
} IrCase;

// Naked, NoReturn and ReturnsTwice are forwarded to the extern prototype of
// functions defined in C. CallSite is read by the backend in trace builds.
typedef enum {
    irFuncAttrNaked = BIT(0),
    irFuncAttrNoReturn = BIT(1),
    irFuncAttrReturnsTwice = BIT(2),
    // Takes the allocation site of its caller as a hidden first parameter
    irFuncAttrCallSite = BIT(3),
} IrFuncAttr;

struct IrNode {
    CXY_IR_NODE_HEAD
    union {
        // Const values
        struct { bool value; } boolConst;
        struct { u32 value; } charConst;
        struct { u128 value; bool isNegative; } intConst;
        struct { f64 value; } floatConst;
        struct { cstring value; } stringConst;
        struct { IrNode *elements; u64 count; } arrayConst;
        struct { cstring name; u32 index; IrNode *value; } fieldConst;
        struct { IrNode *fields; u64 count; } structConst;
        struct { cstring name; IrNode *target; } globalRef;

        // Values
        struct { IrNode *target; cstring name; } varRef;
        struct { IrNode *target; u32 index; cstring name; } member;
        struct { IrNode *target; IrNode *index; } index;
        struct { IrNode *expr; } cast;
        struct { Operator op; IrNode *operand; } unary;
        struct { Operator op; IrNode *lhs; IrNode *rhs; } binary;
        struct { IrNode *callee; IrNode *args; u64 argsCount; } call;
        struct { IrNode *target; } addrOf;
        struct { IrNode *target; } deref;
        struct { const IrType *elemType; IrNode *size; } alloca;
        struct { const IrType *elemType; IrNode *size; } malloc;
        struct { const IrType *targetType; } sizeofVal;
        struct { cstring name; IrNode *expr; } alias;
        struct { bool hasValue; IrNode *value; } optionalValue;
        struct { u32 index; IrNode *value; } unionValue;
        struct { IrNode *env; IrNode *func; IrNode *dtor; } closureValue;

        // RAII
        struct { const IrType *type; } alloc;
        struct { IrNode *src; IrNode *dst; } copy;
        struct { IrNode *src; } move;
        struct { IrNode *target; IrNode *flag; } drop;
        struct { IrNode *target; } zeromem;

        // Statements
        struct { cstring name; cstring display; u32 scopeId; cstring reg; } varDecl;
        struct { IrNode *lhs; IrNode *rhs; } assign;
        struct { u32 scopeId; } scopeEnter;
        struct { u32 scopeId; } scopeExit;
        struct { IrNode *target; } branch;
        struct { IrNode *cond; IrNode *trueTarget; IrNode *falseTarget; } branchIf;
        struct { IrNode *cond; IrCase *cases; u64 casesCount; IrNode *defaultTarget; } switchStmt;
        struct { IrNode *value; } returnStmt;
        struct {
            cstring text;
            IrNode *inputs;
            IrNode *outputs;
            IrNode *clobbers;
            bool isVolatile;
        } inlineAsm;

        // Declarations
        struct { cstring name; IrNode *first; IrNode *last; } basicBlock;
        struct { cstring name; u32 index; } funcParam;
        struct {
            cstring name;
            cstring display;
            IrNode *params;
            u64 paramsCount;
            IrNode *firstBlock;
            IrNode *lastBlock;
            u32 nextScopeId;
            bool isHint;
            u8 attrs;
        } function;
        struct { cstring name; IrNode *init; bool isThreadLocal; } globalVar;
        struct {
            cstring name;
            IrNode *globals;
            IrNode *functions;
            IrTypeTable *types;
        } module;
    };
};

typedef IrNode IrModule;
typedef IrNode IrFunction;

// `info` fills the padding after `tag`. `flags` is all AST flags, so what
// lowering knows about ownership gets its own field.
#ifdef __cplusplus
#define CXY_IR_STATIC_ASSERT(c, m) static_assert(c, m)
#else
#define CXY_IR_STATIC_ASSERT(c, m) _Static_assert(c, m)
#endif
CXY_IR_STATIC_ASSERT(sizeof(IrNode) == 160, "IrNode must not grow");
CXY_IR_STATIC_ASSERT(offsetof(IrNode, loc) == 8, "info must fit in the padding");

// Low 3 bits of `info`: the role of a value in the memory pass
typedef enum IrValueClass {
    irClassNone,
    irClassInFlight,
    irClassStatement,
    irClassExtended,
    irClassResult,
    irClassReturnSlot,
    // lowering made it for an operand and does not know what it is for, normalize
    // decides from its uses
    irClassPending,
    // made by the compiler with a lifetime its creator vouches for: consumed by the
    // next operation or owning nothing. The memory pass leaves it alone.
    irClassTransient,
} IrValueClass;

typedef enum IrInfo {
    irInfoClassMask = 0x7,
    // an operand that a consumer takes ownership of: an argument for a
    // non-reference parameter, an assignment source, a literal element or
    // payload. Only set on values that own something.
    irInfoOwnedPosition = BIT(3),
    // `&&`, `@copy` or `&` written in the source
    irInfoUserWritten = BIT(4),
    // a copy or move inserted by the compiler
    irInfoImplicit = BIT(5),
    // on a function: generated copy or destructor, there is no user code in it
    irInfoGlue = BIT(6),
    // a statement or variable created by a pass, not written by the user
    irInfoSynthetic = BIT(7),
    // an owned-position operand whose variable a `move x` statement before it
    // already took out of play, so the consumer owns it with no copy
    irInfoMoved = BIT(8),
} IrInfo;

static inline bool irHasInfo(const IrNode *node, u32 bits)
{
    return (node->info & bits) == bits;
}

static inline void irSetInfo(IrNode *node, u32 bits) { node->info |= bits; }

static inline void irClearInfo(IrNode *node, u32 bits) { node->info &= ~bits; }

static inline IrValueClass irValueClass(const IrNode *node)
{
    return (IrValueClass)(node->info & irInfoClassMask);
}

static inline void irSetValueClass(IrNode *node, IrValueClass cls)
{
    node->info = (node->info & ~(u32)irInfoClassMask) | (u32)cls;
}

// What the memory pass does with a variable is told by its class. These are the
// questions it asks.

// Consumed by its consumer or transient by its creator: never tracked, never
// dropped at scope exit, never copied
static inline bool irIsConsumedTemp(const IrNode *decl)
{
    IrValueClass cls = irValueClass(decl);
    return cls == irClassInFlight || cls == irClassTransient || cls == irClassResult;
}

// Dropped right after the last statement that reads it
static inline bool irIsStatementTemp(const IrNode *decl)
{
    return irValueClass(decl) == irClassStatement;
}

// The slot a function returns through, its owner is the caller
static inline bool irIsReturnSlot(const IrNode *decl)
{
    return irValueClass(decl) == irClassReturnSlot;
}

IrNode *makeIrNode(MemPool *pool, const FileLoc *loc, u64 flags, IrTag tag, const IrType *type);

IrNode *makeIrModule(MemPool *pool, const FileLoc *loc, cstring name, IrTypeTable *types);
IrNode *makeIrFunction(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, cstring display, const IrType *funcType, IrNode *params, u64 paramsCount);
IrNode *makeIrBasicBlock(MemPool *pool, const FileLoc *loc, cstring name);
IrNode *makeIrFuncParam(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, u32 index, const IrType *type);
IrNode *makeIrGlobalVar(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, const IrType *type, IrNode *init);

// 1 = process finalizer (__fini_<module>), 2 = thread finalizer (__tfini_<module>), 0 = neither
int irModuleFiniKind(const IrNode *func);

IrNode *makeIrVarDecl(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, cstring display, const IrType *type, u32 scopeId);
IrNode *makeIrAssign(MemPool *pool, const FileLoc *loc, IrNode *lhs, IrNode *rhs);
IrNode *makeIrScopeEnter(MemPool *pool, const FileLoc *loc, u32 scopeId);
IrNode *makeIrScopeExit(MemPool *pool, const FileLoc *loc, u32 scopeId);
IrNode *makeIrBranch(MemPool *pool, const FileLoc *loc, IrNode *target);
IrNode *makeIrBranchIf(MemPool *pool, const FileLoc *loc, IrNode *cond, IrNode *trueTarget, IrNode *falseTarget);
IrNode *makeIrSwitch(MemPool *pool, const FileLoc *loc, IrNode *cond, IrCase *cases, u64 casesCount, IrNode *defaultTarget);
IrNode *makeIrReturn(MemPool *pool, const FileLoc *loc, IrNode *value);
IrNode *makeIrUnreachable(MemPool *pool, const FileLoc *loc);
IrNode *makeIrInlineAsm(MemPool *pool,
                        const FileLoc *loc,
                        cstring text,
                        IrNode *outputs,
                        IrNode *inputs,
                        IrNode *clobbers,
                        bool isVolatile);

IrNode *makeIrVarRef(MemPool *pool, const FileLoc *loc, IrNode *decl);
IrNode *makeIrGlobalRef(MemPool *pool, const FileLoc *loc, cstring name, IrNode *target, const IrType *type);
IrNode *makeIrAlias(MemPool *pool, const FileLoc *loc, cstring name, IrNode *expr);
IrNode *cloneIrNode(MemPool *pool, const IrNode *node);
IrNode *cloneIrNodes(MemPool *pool, const IrNode *node);
IrNode *makeIrMember(MemPool *pool, const FileLoc *loc, IrNode *target, u32 index, cstring name, const IrType *type);
IrNode *makeIrIndex(MemPool *pool, const FileLoc *loc, IrNode *target, IrNode *index, const IrType *type);
IrNode *makeIrCast(MemPool *pool, const FileLoc *loc, IrNode *expr, const IrType *toType);
IrNode *makeIrUnary(MemPool *pool, const FileLoc *loc, Operator op, IrNode *operand, const IrType *type);
IrNode *makeIrBinary(MemPool *pool, const FileLoc *loc, Operator op, IrNode *lhs, IrNode *rhs, const IrType *type);
IrNode *makeIrCall(MemPool *pool, const FileLoc *loc, IrNode *callee, IrNode *args, u64 argsCount, const IrType *retType);
IrNode *makeIrAddrOf(MemPool *pool, const FileLoc *loc, IrNode *target, const IrType *ptrType);
IrNode *makeIrDeref(MemPool *pool, const FileLoc *loc, IrNode *target, const IrType *valType);
IrNode *makeIrSizeof(MemPool *pool, const FileLoc *loc, const IrType *targetType, const IrType *u64Type);
IrNode *makeIrAlloca(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size, const IrType *ptrType);
IrNode *makeIrMalloc(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size);
IrNode *makeIrOptionalValue(MemPool *pool, const FileLoc *loc, IrNode *value, const IrType *type);
IrNode *makeIrUnionValue(MemPool *pool, const FileLoc *loc, u32 index, IrNode *value, const IrType *type);
IrNode *makeIrClosureValue(MemPool *pool, const FileLoc *loc, IrNode *env, IrNode *func, IrNode *dtor, const IrType *type);

IrNode *makeIrMove(MemPool *pool, const FileLoc *loc, IrNode *src);
IrNode *makeIrAlloc(MemPool *pool, const FileLoc *loc, const IrType *type);
IrNode *makeIrCopy(MemPool *pool, const FileLoc *loc, IrNode *src, IrNode *dst);
IrNode *makeIrDrop(MemPool *pool, const FileLoc *loc, IrNode *target);
IrNode *makeIrDropIf(MemPool *pool, const FileLoc *loc, IrNode *target, IrNode *flag);
IrNode *makeIrZeromem(MemPool *pool, const FileLoc *loc, IrNode *target);

IrNode *makeIrNullConst(MemPool *pool, const FileLoc *loc, const IrType *type);
IrNode *makeIrBoolConst(MemPool *pool, const FileLoc *loc, bool value, const IrType *type);
IrNode *makeIrCharConst(MemPool *pool, const FileLoc *loc, u32 value, const IrType *type);
IrNode *makeIrIntConst(MemPool *pool, const FileLoc *loc, u128 value, bool isNegative, const IrType *type);
IrNode *makeIrFloatConst(MemPool *pool, const FileLoc *loc, f64 value, const IrType *type);
IrNode *makeIrStringConst(MemPool *pool, const FileLoc *loc, cstring value, const IrType *type);
IrNode *makeIrArrayConst(MemPool *pool, const FileLoc *loc, IrNode *elements, u64 count, const IrType *type);
IrNode *makeIrFieldConst(MemPool *pool, const FileLoc *loc, cstring name, u32 index, IrNode *value, const IrType *type);
IrNode *makeIrStructConst(MemPool *pool, const FileLoc *loc, IrNode *fields, u64 count, const IrType *type);

static inline bool isIrConst(const IrNode *node)
{
    return node && node->tag >= irNullConst && node->tag <= irGlobalRef;
}

void irAppendBlockStmt(IrNode *block, IrNode *stmt);
void irAppendFunctionBlock(IrNode *func, IrNode *block);

#ifdef __cplusplus
}
#endif
