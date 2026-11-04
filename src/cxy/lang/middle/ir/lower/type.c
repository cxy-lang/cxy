#include "internal.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const IrType *findCachedType(LowerContext *ctx, const Type *type)
{
    TypeMapping *found = findInHashTable(&ctx->typeMap,
                                         &(TypeMapping){.astType = type},
                                         hashPtr(hashInit(), type),
                                         sizeof(TypeMapping),
                                         comparePointers);
    return found ? found->irType : NULL;
}

void cacheType(LowerContext *ctx, const Type *type, const IrType *irType)
{
    insertInHashTable(&ctx->typeMap,
                      &(TypeMapping){.astType = type, .irType = irType},
                      hashPtr(hashInit(), type),
                      sizeof(TypeMapping),
                      comparePointers);
}

cstring getIrTypeName(LowerContext *ctx, const IrType *type)
{
    if (type == NULL)
        return S_void;
    if (type->name)
        return type->name;
    if (type->category == irTypeCatClass && type->pointer.pointee) {
        if (type->pointer.pointee->name)
            return type->pointer.pointee->name;
        if (type->pointer.pointee->display)
            return type->pointer.pointee->display;
    }
    if (type->tag == irTypePointer && type->pointer.pointee) {
        cstring inner = getIrTypeName(ctx, type->pointer.pointee);
        cstring prefix = (type->category == irTypeCatReference) ? "ref" : "ptr";
        return makeStringf(ctx->strings, "%s_%s", prefix, inner);
    }
    switch (type->tag) {
    case irTypeVoid:
        return S_void;
    case irTypeBool:
        return S_bool;
    case irTypeChar:
        return S_char;
    case irTypeI8:
        return S_i8;
    case irTypeI16:
        return S_i16;
    case irTypeI32:
        return S_i32;
    case irTypeI64:
        return S_i64;
    case irTypeI128:
        return S_i128;
    case irTypeU8:
        return S_u8;
    case irTypeU16:
        return S_u16;
    case irTypeU32:
        return S_u32;
    case irTypeU64:
        return S_u64;
    case irTypeU128:
        return S_u128;
    case irTypeF32:
        return S_f32;
    case irTypeF64:
        return S_f64;
    case irTypePointer:
        return S_ptr;
    case irTypeArray:
        return S_arr;
    case irTypeStruct:
        return S_struct;
    case irTypeUntaggedUnion:
        return S_union;
    case irTypeFunc:
        return S_func;
    default:
        return S_val;
    }
}

// The name the user knows a type by (`Vector[Node]`), for tools. The IR names
// are mangled and also feed the names of other generated types.
cstring readableTypeName(LowerContext *ctx, const Type *type)
{
    FormatState state = newFormatState(NULL, true);
    printType_(&state, type, false);
    char *str = formatStateToString(&state);
    cstring result = makeString(ctx->strings, str);
    free(str);
    freeFormatState(&state);
    return result;
}

#define IR_TYPE_NAME_MAX 64
#define IR_TYPE_NAME_PREFIX 44

// Interns `raw` as-is when short, else a capped prefix plus a hash of the
// full structural name, so identical shapes always get identical names.
static cstring boundIrTypeName(LowerContext *ctx, const char *raw)
{
    if (strlen(raw) <= IR_TYPE_NAME_MAX)
        return makeString(ctx->strings, raw);
    return makeStringf(ctx->strings,
                       "%.*s_%08x",
                       IR_TYPE_NAME_PREFIX,
                       raw,
                       (unsigned)hashStr(hashInit(), raw));
}

static cstring formatBoundedIrTypeName(LowerContext *ctx, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    char *raw = mallocOrDie((size_t)len + 1);
    va_start(args, fmt);
    vsnprintf(raw, (size_t)len + 1, fmt, args);
    va_end(args);

    cstring name = boundIrTypeName(ctx, raw);
    free(raw);
    return name;
}

static cstring makeJoinedIrTypeName(LowerContext *ctx,
                                    const char *prefix,
                                    IrField *fields,
                                    u64 count)
{
    size_t total = strlen(prefix) + 1;
    for (u64 i = 0; i < count; i++)
        total += 1 + strlen(getIrTypeName(ctx, fields[i].type));

    char *raw = mallocOrDie(total);
    size_t len = strlen(prefix);
    memcpy(raw, prefix, len);
    for (u64 i = 0; i < count; i++) {
        cstring elemName = getIrTypeName(ctx, fields[i].type);
        size_t elemLen = strlen(elemName);
        raw[len++] = '_';
        memcpy(raw + len, elemName, elemLen);
        len += elemLen;
    }
    raw[len] = '\0';

    cstring name = boundIrTypeName(ctx, raw);
    free(raw);
    return name;
}

cstring makeDescriptiveTupleName(LowerContext *ctx,
                                 attr(unused) const Type *type,
                                 IrField *fields,
                                 u64 count)
{
    return makeJoinedIrTypeName(ctx, "Tuple", fields, count);
}

cstring makeDescriptiveOptionalName(LowerContext *ctx,
                                    attr(unused) const Type *type,
                                    const IrType *targetType)
{
    return formatBoundedIrTypeName(
        ctx, "Optional_%s", getIrTypeName(ctx, targetType));
}

cstring makeDescriptiveResultName(LowerContext *ctx,
                                  const Type *type,
                                  const IrType *targetType)
{
    if (type && type->result.target) {
        const Type *targetAst = resolveType(type->result.target);
        if (targetAst && targetAst->name)
            return formatBoundedIrTypeName(ctx, "Result_%s", targetAst->name);
    }
    return formatBoundedIrTypeName(
        ctx, "Result_%s", getIrTypeName(ctx, targetType));
}

cstring makeDescriptiveUnionName(LowerContext *ctx,
                                 attr(unused) const Type *type,
                                 IrField *fields,
                                 u64 count)
{
    return makeJoinedIrTypeName(ctx, "Union", fields, count);
}

cstring makeDescriptiveUnionDisplay(LowerContext *ctx,
                                    IrField *fields,
                                    u64 count)
{
    char buf[1024];
    int len = 0;
    for (u64 i = 0; i < count && len < (int)sizeof(buf) - 64; i++) {
        if (i > 0)
            len += snprintf(buf + len, sizeof(buf) - len, " | ");
        cstring dName = fields[i].type
                            ? (fields[i].type->display ?: fields[i].type->name)
                            : "void";
        if (len < (int)sizeof(buf) - 32)
            len += snprintf(buf + len, sizeof(buf) - len, "%s", dName);
    }
    return makeString(ctx->strings, buf);
}

cstring makeDescriptiveTupleDisplay(LowerContext *ctx,
                                    IrField *fields,
                                    u64 count)
{
    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "(");
    for (u64 i = 0; i < count && len < (int)sizeof(buf) - 64; i++) {
        if (i > 0)
            len += snprintf(buf + len, sizeof(buf) - len, ", ");
        cstring dName = fields[i].type
                            ? (fields[i].type->display ?: fields[i].type->name)
                            : "void";
        if (len < (int)sizeof(buf) - 32)
            len += snprintf(buf + len, sizeof(buf) - len, "%s", dName);
    }
    if (len < (int)sizeof(buf) - 4)
        snprintf(buf + len, sizeof(buf) - len, ")");
    else
        snprintf(buf + sizeof(buf) - 4, 4, "...)");
    return makeString(ctx->strings, buf);
}

cstring makeDescriptiveOptionalDisplay(LowerContext *ctx,
                                       const IrType *targetType)
{
    cstring targetDisplay =
        targetType ? (targetType->display ?: targetType->name) : "void";
    return makeStringf(ctx->strings, "%s?", targetDisplay);
}

cstring makeDescriptiveResultDisplay(LowerContext *ctx,
                                     const IrType *targetType)
{
    cstring targetDisplay =
        targetType ? (targetType->display ?: targetType->name) : "void";
    return makeStringf(ctx->strings, "!%s", targetDisplay);
}

const IrType *lowerClosureFnPtrType(LowerContext *ctx,
                                    const Type *closureType)
{
    const IrType *voidPtrType = makeIrPointerType(
        ctx->types,
        flgNone, irTypeCatNone,
        getIrPrimitiveType(ctx->types, irTypeVoid));
    const Type *fnSig = closureType->closure.fn;
    u64 totalParams = 1 + fnSig->func.paramsCount;
    const IrType **fnParams =
        allocFromMemPool(ctx->pool, sizeof(IrType *) * totalParams);
    fnParams[0] = voidPtrType;
    for (u64 i = 0; i < fnSig->func.paramsCount; i++) {
        fnParams[i + 1] = lowerType(ctx, fnSig->func.params[i]);
    }
    const IrType *retType = lowerType(ctx, fnSig->func.retType);
    return makeIrFuncType(
        ctx->types, flgNone, fnParams, totalParams, retType, false);
}
