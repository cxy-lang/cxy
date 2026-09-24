#pragma once

#include "core/htable.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IrNode;

// clang-format off
#define CXY_IR_TYPE_PRIMITIVE_TAGS(f)       \
    f(Void)                                 \
    f(Bool)                                 \
    f(Char)                                 \
    f(I8)                                   \
    f(I16)                                  \
    f(I32)                                  \
    f(I64)                                  \
    f(I128)                                 \
    f(U8)                                   \
    f(U16)                                  \
    f(U32)                                  \
    f(U64)                                  \
    f(U128)                                 \
    f(F32)                                  \
    f(F64)

#define CXY_IR_TYPE_TAGS(f)                 \
    CXY_IR_TYPE_PRIMITIVE_TAGS(f)           \
    f(Pointer)                              \
    f(Array)                                \
    f(Struct)                               \
    f(UntaggedUnion)                        \
    f(Func)                                 \
    f(Opaque)
// clang-format on

typedef enum IrTypeTag {
#define f(NAME) irType##NAME,
    CXY_IR_TYPE_TAGS(f)
#undef f
    irTypeCOUNT
} IrTypeTag;

typedef enum IrTypeCategory {
    irTypeCatNone = 0,
    irTypeCatClass,
    irTypeCatReference,
    irTypeCatStruct,
    irTypeCatTuple,
    irTypeCatUnion,
    irTypeCatOptional,
    irTypeCatSlice,
    irTypeCatResult
} IrTypeCategory;

typedef struct IrType IrType;

typedef struct IrField {
    cstring name;
    const IrType *type;
    u64 offset;
    u32 index;
    u32 bits;
} IrField;

struct IrType {
    IrTypeTag tag;
    IrTypeCategory category;
    Flags flags;
    u64 size;
    u64 align;
    u64 index;
    cstring name;
    cstring display;
    union {
        struct {
            const IrType *pointee;
        } pointer;
        struct {
            const IrType *elem;
            u64 len;
        } array;
        struct {
            IrField *fields;
            u64 count;
            union {
                const struct AstNode *dctorAst;
                struct IrNode *dctor;
            };
            union {
                const struct AstNode *dctorFwdAst;
                struct IrNode *dctorFwd;
            };
            union {
                const struct AstNode *copyAst;
                struct IrNode *copy;
            };
        } strct;
        struct {
            const IrType **params;
            u64 count;
            const IrType *ret;
            bool variadic;
        } func;
    };
};

typedef struct IrTypeTable {
    MemPool *pool;
    StrPool *strings;
    HashTable types;
    u64 count;
    const IrType *primitives[irTypeCOUNT];
} IrTypeTable;

IrTypeTable *newIrTypeTable(MemPool *pool, StrPool *strings);
void freeIrTypeTable(IrTypeTable *table);

const IrType *getIrPrimitiveType(IrTypeTable *table, IrTypeTag tag);
const IrType *makeIrPointerType(IrTypeTable *table, Flags flags, const IrType *pointee);
const IrType *makeIrClassType(IrTypeTable *table, Flags flags, const IrType *structType);
const IrType *makeIrArrayType(IrTypeTable *table, Flags flags, const IrType *elem, u64 len);
const IrType *makeIrStructType(IrTypeTable *table, Flags flags, cstring name, cstring display, IrField *fields, u64 count);
const IrType *makeIrUntaggedUnionType(IrTypeTable *table, Flags flags, cstring name, cstring display, IrField *fields, u64 count);
const IrType *makeIrFuncType(IrTypeTable *table, Flags flags, const IrType **params, u64 count, const IrType *ret, bool variadic);

static inline bool isIrPointerType(const IrType *type) { return type && type->tag == irTypePointer; }
static inline bool isIrArrayType(const IrType *type) { return type && type->tag == irTypeArray; }
static inline bool isIrStructType(const IrType *type) { return type && type->tag == irTypeStruct; }
static inline bool isIrFuncType(const IrType *type) { return type && type->tag == irTypeFunc; }
static inline bool isIrVoidType(const IrType *type) { return type && type->tag == irTypeVoid; }

static inline bool isIrTypeDestructible(const IrType *type)
{
    if (type == NULL)
        return false;
    return hasFlag(type, ReferenceMembers) || type->category == irTypeCatClass;
}

static inline const IrType *getIrOptionalTypeTarget(const IrType *type)
{
    if (type && type->tag == irTypeStruct && type->strct.count == 2)
        return type->strct.fields[1].type;
    return NULL;
}

#ifdef __cplusplus
}
#endif
