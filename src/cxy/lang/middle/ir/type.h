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
    irTypeCatArray,
    irTypeCatResult,
    irTypeCatClosure,
    irTypeCatEnum
} IrTypeCategory;

typedef struct IrType IrType;

typedef struct IrField {
    cstring name;
    const IrType *type;
    u64 offset;
    u32 index;
    u32 bits;
} IrField;

typedef struct IrEnumOption {
    cstring name;
    i64 value;
} IrEnumOption;

struct IrType {
    IrTypeTag tag;
    IrTypeCategory category;
    Flags flags;
    u64 size;
    u64 align;
    bool isPacked;
    bool noCopy; // declared `@nocopy`: a move-only type
    u64 index;
    cstring name;
    cstring display;
    cstring typeName; // classes: the readable name, `Vector[Node]`, for tools
    union {
        struct {
            const IrType *pointee;
        } pointer;
        struct {
            const IrType *elem;
            u64 len;
            union {
                const struct AstNode *dctorAst;
                struct IrNode *dctor;
            };
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
        struct {
            IrEnumOption *options;
            u64 count;
        } enm;
    };
};

typedef struct IrTypeTable {
    MemPool *pool;
    StrPool *strings;
    HashTable types;
    HashTable names; // cstring, every emitted struct/union name
    u64 count;
    const IrType *primitives[irTypeCOUNT];
} IrTypeTable;

// Everything that affects identity or layout of a struct/union type
typedef struct IrStructOpts {
    IrTypeCategory category;
    u64 align;       // 0 = natural alignment
    bool isPacked;
    bool structural; // same name + different fields gets a unique suffix
} IrStructOpts;

IrTypeTable *newIrTypeTable(MemPool *pool, StrPool *strings);
void freeIrTypeTable(IrTypeTable *table);

const IrType *getIrPrimitiveType(IrTypeTable *table, IrTypeTag tag);
const IrType *makeIrPointerType(IrTypeTable *table, Flags flags, IrTypeCategory category, const IrType *pointee);
const IrType *makeIrClassType(IrTypeTable *table, Flags flags, const IrType *structType);
const IrType *makeIrArrayType(IrTypeTable *table, Flags flags, IrTypeCategory category, const IrType *elem, u64 len);
const IrType *makeIrStructType(IrTypeTable *table, Flags flags, IrStructOpts opts, cstring name, cstring display, IrField *fields, u64 count);
const IrType *makeIrUntaggedUnionType(IrTypeTable *table, Flags flags, IrStructOpts opts, cstring name, cstring display, IrField *fields, u64 count);
const IrType *makeIrFuncType(IrTypeTable *table, Flags flags, const IrType **params, u64 count, const IrType *ret, bool variadic);
const IrType *makeIrEnumType(IrTypeTable *table,
                             Flags flags,
                             cstring name,
                             cstring display,
                             const IrType *baseType,
                             IrEnumOption *options,
                             u64 optionsCount);

static inline bool isIrPointerType(const IrType *type) { return type && type->tag == irTypePointer; }
static inline bool isIrArrayType(const IrType *type) { return type && type->tag == irTypeArray; }
static inline bool isIrStructType(const IrType *type) { return type && type->tag == irTypeStruct; }
static inline bool isIrFuncType(const IrType *type) { return type && type->tag == irTypeFunc; }
static inline bool isIrVoidType(const IrType *type) { return type && type->tag == irTypeVoid; }

// A `@nocopy` type, or a value type that holds one by value. A class handle is
// only move-only if the class itself is declared so.
static inline bool isIrTypeNoCopy(const IrType *type)
{
    for (u32 depth = 0; type && depth < 8; depth++) {
        if (type->noCopy)
            return true;
        if (type->tag != irTypeStruct || type->category == irTypeCatClass)
            return false;
        for (u64 i = 0; i < type->strct.count; i++) {
            const IrType *field = type->strct.fields[i].type;
            if (field && field->tag == irTypeStruct && field->category != irTypeCatClass) {
                if (isIrTypeNoCopy(field))
                    return true;
            }
            else if (field && field->noCopy)
                return true;
        }
        return false;
    }
    return false;
}

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
