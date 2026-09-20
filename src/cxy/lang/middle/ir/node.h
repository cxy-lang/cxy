#pragma once

#include "type.h"

#include "core/log.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/operator.h"


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
    f(Sizeof)

#define CXY_IR_RAII_TAGS(f)         \
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
    FileLoc loc;                                                               \
    u64 flags;                                                                 \
    const IrType *type;                                                        \
    IrNode *parent;                                                            \
    IrNode *next;                                                              \
    struct AstNode *ast;                                                       \
    void *codegen;

typedef struct IrCase {
    u64 value;
    IrNode *target;
} IrCase;

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

        // RAII
        struct { IrNode *src; IrNode *dst; } copy;
        struct { IrNode *src; } move;
        struct { IrNode *target; } drop;
        struct { IrNode *target; } zeromem;

        // Statements
        struct { cstring name; cstring display; u32 scopeId; } varDecl;
        struct { IrNode *lhs; IrNode *rhs; } assign;
        struct { u32 scopeId; } scopeEnter;
        struct { u32 scopeId; } scopeExit;
        struct { IrNode *target; } branch;
        struct { IrNode *cond; IrNode *trueTarget; IrNode *falseTarget; } branchIf;
        struct { IrNode *cond; IrCase *cases; u64 casesCount; IrNode *defaultTarget; } switchStmt;
        struct { IrNode *value; } returnStmt;
        struct { cstring text; IrNode *inputs; IrNode *outputs; } inlineAsm;

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
        } function;
        struct { cstring name; IrNode *init; } globalVar;
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

IrNode *makeIrNode(MemPool *pool, const FileLoc *loc, u64 flags, IrTag tag, const IrType *type);

IrNode *makeIrModule(MemPool *pool, const FileLoc *loc, cstring name, IrTypeTable *types);
IrNode *makeIrFunction(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, cstring display, const IrType *funcType, IrNode *params, u64 paramsCount);
IrNode *makeIrBasicBlock(MemPool *pool, const FileLoc *loc, cstring name);
IrNode *makeIrFuncParam(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, u32 index, const IrType *type);
IrNode *makeIrGlobalVar(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, const IrType *type, IrNode *init);

IrNode *makeIrVarDecl(MemPool *pool, const FileLoc *loc, u64 flags, cstring name, cstring display, const IrType *type, u32 scopeId);
IrNode *makeIrAssign(MemPool *pool, const FileLoc *loc, IrNode *lhs, IrNode *rhs);
IrNode *makeIrScopeEnter(MemPool *pool, const FileLoc *loc, u32 scopeId);
IrNode *makeIrScopeExit(MemPool *pool, const FileLoc *loc, u32 scopeId);
IrNode *makeIrBranch(MemPool *pool, const FileLoc *loc, IrNode *target);
IrNode *makeIrBranchIf(MemPool *pool, const FileLoc *loc, IrNode *cond, IrNode *trueTarget, IrNode *falseTarget);
IrNode *makeIrReturn(MemPool *pool, const FileLoc *loc, IrNode *value);
IrNode *makeIrUnreachable(MemPool *pool, const FileLoc *loc);

IrNode *makeIrVarRef(MemPool *pool, const FileLoc *loc, IrNode *decl);
IrNode *makeIrGlobalRef(MemPool *pool, const FileLoc *loc, cstring name, IrNode *target, const IrType *type);
IrNode *makeIrMember(MemPool *pool, const FileLoc *loc, IrNode *target, u32 index, cstring name, const IrType *type);
IrNode *makeIrIndex(MemPool *pool, const FileLoc *loc, IrNode *target, IrNode *index, const IrType *type);
IrNode *makeIrCast(MemPool *pool, const FileLoc *loc, IrNode *expr, const IrType *toType);
IrNode *makeIrUnary(MemPool *pool, const FileLoc *loc, Operator op, IrNode *operand, const IrType *type);
IrNode *makeIrBinary(MemPool *pool, const FileLoc *loc, Operator op, IrNode *lhs, IrNode *rhs, const IrType *type);
IrNode *makeIrCall(MemPool *pool, const FileLoc *loc, IrNode *callee, IrNode *args, u64 argsCount, const IrType *retType);
IrNode *makeIrAddrOf(MemPool *pool, const FileLoc *loc, IrNode *target, const IrType *ptrType);
IrNode *makeIrDeref(MemPool *pool, const FileLoc *loc, IrNode *target, const IrType *valType);
IrNode *makeIrSizeof(MemPool *pool, const FileLoc *loc, const IrType *targetType);
IrNode *makeIrAlloca(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size);
IrNode *makeIrMalloc(MemPool *pool, const FileLoc *loc, const IrType *elemType, IrNode *size);

IrNode *makeIrMove(MemPool *pool, const FileLoc *loc, IrNode *src);
IrNode *makeIrCopy(MemPool *pool, const FileLoc *loc, IrNode *src, IrNode *dst);
IrNode *makeIrDrop(MemPool *pool, const FileLoc *loc, IrNode *target);
IrNode *makeIrZeromem(MemPool *pool, const FileLoc *loc, IrNode *target);

IrNode *makeIrNullConst(MemPool *pool, const FileLoc *loc, const IrType *type);
IrNode *makeIrBoolConst(MemPool *pool, const FileLoc *loc, bool value, const IrType *type);
IrNode *makeIrCharConst(MemPool *pool, const FileLoc *loc, u32 value, const IrType *type);
IrNode *makeIrIntConst(MemPool *pool, const FileLoc *loc, u128 value, bool isNegative, const IrType *type);
IrNode *makeIrFloatConst(MemPool *pool, const FileLoc *loc, f64 value, const IrType *type);
IrNode *makeIrStringConst(MemPool *pool, const FileLoc *loc, cstring value, const IrType *type);

void irAppendBlockStmt(IrNode *block, IrNode *stmt);
void irAppendFunctionBlock(IrNode *func, IrNode *block);

#ifdef __cplusplus
}
#endif
