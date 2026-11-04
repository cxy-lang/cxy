#include "internal.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/middle/builtins.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const IrType *findCachedType(LowerContext *ctx, const Type *type)
{
    TypeMapping *found = findInHashTable(&ctx->typeMap,
                                         &(TypeMapping){.astType = type},
                                         hashPtr(hashInit(), type),
                                         sizeof(TypeMapping),
                                         comparePointers);
    return found ? found->irType : NULL;
}

static void cacheType(LowerContext *ctx, const Type *type, const IrType *irType)
{
    insertInHashTable(&ctx->typeMap,
                      &(TypeMapping){.astType = type, .irType = irType},
                      hashPtr(hashInit(), type),
                      sizeof(TypeMapping),
                      comparePointers);
}

static cstring getIrTypeName(LowerContext *ctx, const IrType *type)
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
static cstring readableTypeName(LowerContext *ctx, const Type *type)
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

       static cstring makeDescriptiveTupleName(LowerContext *ctx,
                                        attr(unused) const Type *type,
                                        IrField *fields,
                                        u64 count)
{
    return makeJoinedIrTypeName(ctx, "Tuple", fields, count);
}

       static cstring makeDescriptiveOptionalName(LowerContext *ctx,
                                           attr(unused) const Type *type,
                                           const IrType *targetType)
{
    return formatBoundedIrTypeName(
        ctx, "Optional_%s", getIrTypeName(ctx, targetType));
}

       static cstring makeDescriptiveResultName(LowerContext *ctx,
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

       static cstring makeDescriptiveUnionName(LowerContext *ctx,
                                        attr(unused) const Type *type,
                                        IrField *fields,
                                        u64 count)
{
    return makeJoinedIrTypeName(ctx, "Union", fields, count);
}

       static cstring makeDescriptiveUnionDisplay(LowerContext *ctx,
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

       static cstring makeDescriptiveTupleDisplay(LowerContext *ctx,
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

       static cstring makeDescriptiveOptionalDisplay(LowerContext *ctx,
                                              const IrType *targetType)
{
    cstring targetDisplay =
        targetType ? (targetType->display ?: targetType->name) : "void";
    return makeStringf(ctx->strings, "%s?", targetDisplay);
}

       static cstring makeDescriptiveResultDisplay(LowerContext *ctx,
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

// `@nocopy` on a class applies to every class derived from it
static bool isNoCopyClassType(const Type *type)
{
    for (u32 depth = 0; type && typeIs(type, Class) && depth < 64; depth++) {
        if (type->tClass.decl && findAttribute(type->tClass.decl, S_nocopy))
            return true;
        type = type->tClass.inheritance ? type->tClass.inheritance->base : NULL;
    }
    return false;
}

static const IrType *lowerPrimitiveType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    switch (type->primitive.id) {
    case prtBool:
        result = getIrPrimitiveType(ctx->types, irTypeBool);
        break;
    case prtCChar:
        result = getIrPrimitiveType(ctx->types, irTypeChar);
        break;
    case prtChar:
        result = getIrPrimitiveType(ctx->types, irTypeI32);
        break;
    case prtI8:
        result = getIrPrimitiveType(ctx->types, irTypeI8);
        break;
    case prtI16:
        result = getIrPrimitiveType(ctx->types, irTypeI16);
        break;
    case prtI32:
        result = getIrPrimitiveType(ctx->types, irTypeI32);
        break;
    case prtI64:
        result = getIrPrimitiveType(ctx->types, irTypeI64);
        break;
    case prtI128:
        result = getIrPrimitiveType(ctx->types, irTypeI128);
        break;
    case prtU8:
        result = getIrPrimitiveType(ctx->types, irTypeU8);
        break;
    case prtU16:
        result = getIrPrimitiveType(ctx->types, irTypeU16);
        break;
    case prtU32:
        result = getIrPrimitiveType(ctx->types, irTypeU32);
        break;
    case prtU64:
        result = getIrPrimitiveType(ctx->types, irTypeU64);
        break;
    case prtU128:
        result = getIrPrimitiveType(ctx->types, irTypeU128);
        break;
    case prtF32:
        result = getIrPrimitiveType(ctx->types, irTypeF32);
        break;
    case prtF64:
        result = getIrPrimitiveType(ctx->types, irTypeF64);
        break;
    default:
        result = getIrPrimitiveType(ctx->types, irTypeI32);
        break;
    }
    return result;
}

static const IrType *lowerPointerType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const Type *pointed = type->pointer.pointed;
    const IrType *pointedIr = lowerType(ctx, pointed);
    result = makeIrPointerType(ctx->types, flgNone, irTypeCatNone, pointedIr);
    return result;
}

static const IrType *lowerReferenceType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const Type *referred = stripReference(type);
    const IrType *referredIr = lowerType(ctx, referred);
    const IrType *pointee = (referredIr && referredIr->tag == irTypePointer && isClassType(referred))
                                ? referredIr->pointer.pointee
                                : referredIr;
    // Like a class handle, a reference to an array is the array itself (an
    // element pointer), never a pointer to the array
    if (referredIr && referredIr->tag == irTypeArray)
        pointee = referredIr->array.elem;

    result = makeIrPointerType(
        ctx->types, flgNone, irTypeCatReference, pointee);
    return result;
}

static const IrType *lowerArrayType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    if (hasFlag(type, Slice) || type->array.len == UINT64_MAX) {
        const IrType *elemType = lowerType(ctx, type->array.elementType);
        IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
        fields[0] = (IrField){
            .name = S_ptr,
            .type = makeIrPointerType(ctx->types, flgNone, irTypeCatNone, elemType),
            .bits = 0};
        fields[1] =
            (IrField){.name = S_len,
                      .type = getIrPrimitiveType(ctx->types, irTypeU64),
                      .bits = 0};
        cstring elemName = getIrTypeName(ctx, elemType);
        cstring name = makeStringf(ctx->strings, "Slice_%s", elemName);
        cstring display = makeStringf(
            ctx->strings, "[%s]", elemType->display ?: elemName);
        result = makeIrStructType(
            ctx->types,
            flgNone,
            (IrStructOpts){.category = irTypeCatSlice, .structural = true},
            name,
            display,
            fields,
            2);
        ((IrType *)result)->index = type->index;
    }
    else {
        Flags flags = isDestructible(type) ? flgReferenceMembers : flgNone;
        result = makeIrArrayType(ctx->types,
                                 flags,
                                 irTypeCatArray,
                                 lowerType(ctx, type->array.elementType),
                                 type->array.len);
        ((IrType *)result)->index = type->index;
        if (isDestructible(type->array.elementType)) {
            const Type *dctorFunc = type->array.destructorFunc;
            if (dctorFunc == NULL) {
                const Type *sliceType = makeSliceType(ctx->driver->types, type->array.elementType);
                dctorFunc = sliceType ? sliceType->array.destructorFunc : NULL;
            }
            csAssert(dctorFunc && dctorFunc->func.decl,
                     "destructible array of element '%s' missing destructor declaration",
                     getIrTypeName(ctx, ((IrType *)result)->array.elem));
            ((IrType *)result)->array.dctorAst =
                dctorFunc->func.decl;
        }
    }
    return result;
}

static const IrType *lowerTupleType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    u64 count = type->tuple.count;
    IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * count);
    for (u64 i = 0; i < count; i++) {
        fields[i].name = makeStringf(ctx->strings, "_%u", (unsigned)i);
        fields[i].type = lowerType(ctx, type->tuple.members[i]);
        fields[i].bits = 0;
    }
    cstring name = makeDescriptiveTupleName(ctx, type, fields, count);
    cstring display = makeDescriptiveTupleDisplay(ctx, fields, count);

    Flags flags = isDestructible(type) ? flgReferenceMembers : flgNone;
    result = makeIrStructType(
        ctx->types,
        flags,
        (IrStructOpts){.category = irTypeCatTuple, .structural = true},
        name,
        display,
        fields,
        count);
    ((IrType *)result)->index = type->index;
    if (isDestructible(type)) {
        csAssert(type->tuple.destructorFunc && type->tuple.destructorFunc->func.decl,
                 "destructible tuple %u missing destructor declaration",
                 (unsigned)type->index);
        ((IrType *)result)->strct.dctorAst =
            type->tuple.destructorFunc->func.decl;
    }
    if (type->tuple.copyFunc && type->tuple.copyFunc->func.decl)
        ((IrType *)result)->strct.copyAst = type->tuple.copyFunc->func.decl;
    return result;
}

static const IrType *lowerOptionalType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const IrType *targetType = lowerType(ctx, type->optional.target);
    IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
    fields[0] =
        (IrField){.name = S_ok,
                  .type = getIrPrimitiveType(ctx->types, irTypeBool),
                  .bits = 0};
    fields[1] = (IrField){.name = S_val, .type = targetType, .bits = 0};
    cstring name = makeDescriptiveOptionalName(ctx, type, targetType);
    cstring display = makeDescriptiveOptionalDisplay(ctx, targetType);
    Flags flags = isDestructible(type) ? flgReferenceMembers : flgNone;
    result = makeIrStructType(
        ctx->types,
        flags,
        (IrStructOpts){.category = irTypeCatOptional, .structural = true},
        name,
        display,
        fields,
        2);
    ((IrType *)result)->index = type->index;
    return result;
}

static const IrType *lowerStructType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    cstring rawName =
        type->name
            ?: makeStringf(ctx->strings, "Struct%u", (unsigned)type->index);
    cstring name = type->ns
                       ? makeStringf(ctx->strings, "%s_%s", type->ns, rawName)
                       : rawName;
    u64 count = type->tStruct.members ? type->tStruct.members->count : 0;
    u64 fieldCount = 0;
    for (u64 i = 0; i < count; i++) {
        if (nodeIs(type->tStruct.members->members[i].decl, FieldDecl))
            fieldCount++;
    }
    IrField *fields =
        allocFromMemPool(ctx->pool, sizeof(IrField) * (fieldCount ?: 1));
    memset(fields, 0, sizeof(IrField) * (fieldCount ?: 1));
    Flags flags = isDestructible(type) ? flgReferenceMembers : flgNone;
    IrStructOpts sOpts = {.category = irTypeCatStruct};
    const AstNode *sDecl = type->tStruct.decl;
    if (sDecl) {
        sOpts.isPacked = findAttribute(sDecl, S_packed) != NULL;
        const AstNode *aligned = findAttribute(sDecl, S_align);
        if (aligned) {
            const AstNode *value = getAttributeArgument(NULL, NULL, aligned, 0);
            if (value)
                sOpts.align = (u64)value->intLiteral.uValue;
        }
    }
    const IrType *structType = makeIrStructType(
        ctx->types, flags, sOpts, name, type->name, fields, fieldCount);
    ((IrType *)structType)->index = type->index;
    ((IrType *)structType)->noCopy =
        sDecl && findAttribute(sDecl, S_nocopy) != NULL;
    const NamedTypeMember *dctorMember =
        findStructMember(type, S_DestructorOverload);
    if (dctorMember && dctorMember->decl)
        ((IrType *)structType)->strct.dctorAst = dctorMember->decl;
    const NamedTypeMember *copyMember =
        findStructMember(type, S_CopyOverload);
    if (copyMember && copyMember->decl)
        ((IrType *)structType)->strct.copyAst = copyMember->decl;
    cacheType(ctx, type, structType);

    u64 fi = 0;
    for (u64 i = 0; i < count; i++) {
        const NamedTypeMember *m = &type->tStruct.members->members[i];
        if (!nodeIs(m->decl, FieldDecl))
            continue;
        fields[fi].name = m->name;
        fields[fi].type = lowerType(ctx, m->type);
        fields[fi].bits = m->decl->structField.bits;
        fi++;
    }
    result = structType;
    return result;
}

static const IrType *lowerUntaggedUnionType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    u64 count = type->untaggedUnion.members
                    ? type->untaggedUnion.members->count
                    : 0;
    u64 fieldCount = 0;
    for (u64 i = 0; i < count; i++) {
        if (nodeIs(type->untaggedUnion.members->members[i].decl, FieldDecl))
            fieldCount++;
    }
    IrField *fields =
        allocFromMemPool(ctx->pool, sizeof(IrField) * (fieldCount ?: 1));
    u64 fi = 0;
    for (u64 i = 0; i < count; i++) {
        const NamedTypeMember *m = &type->untaggedUnion.members->members[i];
        if (!nodeIs(m->decl, FieldDecl))
            continue;
        fields[fi].name = m->name;
        fields[fi].type = lowerType(ctx, m->type);
        fields[fi].bits = m->decl->structField.bits;
        fi++;
    }
    cstring name =
        type->name
            ?: makeStringf(ctx->strings, "Union%u", (unsigned)type->index);
    result = makeIrUntaggedUnionType(
        ctx->types, flgNone, (IrStructOpts){0}, name, type->name, fields, fi);
    return result;
}

static const IrType *lowerClassType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    cstring rawName =
        type->name
            ?: makeStringf(ctx->strings, "Class%u", (unsigned)type->index);
    cstring name = type->ns
                       ? makeStringf(ctx->strings, "%s_%s", type->ns, rawName)
                       : rawName;
    u64 count = type->tClass.members ? type->tClass.members->count : 0;
    u64 fieldCount = 0;
    for (u64 i = 0; i < count; i++) {
        if (nodeIs(type->tClass.members->members[i].decl, FieldDecl))
            fieldCount++;
    }
    IrField *fields =
        allocFromMemPool(ctx->pool, sizeof(IrField) * (fieldCount ?: 1));
    memset(fields, 0, sizeof(IrField) * (fieldCount ?: 1));
    IrStructOpts cOpts = {.category = irTypeCatClass};
    const AstNode *cDecl = type->tClass.decl;
    if (cDecl) {
        cOpts.isPacked = findAttribute(cDecl, S_packed) != NULL;
        const AstNode *aligned = findAttribute(cDecl, S_align);
        if (aligned) {
            const AstNode *value = getAttributeArgument(NULL, NULL, aligned, 0);
            if (value)
                cOpts.align = (u64)value->intLiteral.uValue;
        }
    }
    const IrType *structType = makeIrStructType(ctx->types,
                                                flgReferenceMembers,
                                                cOpts,
                                                name,
                                                type->name,
                                                fields,
                                                fieldCount);
    ((IrType *)structType)->index = type->index;
    ((IrType *)structType)->typeName = readableTypeName(ctx, type);
    ((IrType *)structType)->noCopy = isNoCopyClassType(type);
    const NamedTypeMember *classDctor =
        findClassMember(type, S_DestructorOverload);
    if (classDctor && classDctor->decl)
        ((IrType *)structType)->strct.dctorAst = classDctor->decl;
    const NamedTypeMember *classDctorFwd =
        findClassMember(type, S_DestructorFwd);
    if (classDctorFwd && classDctorFwd->decl)
        ((IrType *)structType)->strct.dctorFwdAst = classDctorFwd->decl;
    const NamedTypeMember *classCopy =
        findClassMember(type, S_CopyOverload);
    if (classCopy && classCopy->decl)
        ((IrType *)structType)->strct.copyAst = classCopy->decl;
    const IrType *ptrType =
        makeIrClassType(ctx->types, flgReferenceMembers, structType);
    ((IrType *)ptrType)->index = type->index;
    ((IrType *)ptrType)->typeName = ((IrType *)structType)->typeName;
    ((IrType *)ptrType)->noCopy = ((IrType *)structType)->noCopy;
    ((IrType *)ptrType)->strct.dctorAst =
        ((IrType *)structType)->strct.dctorAst;
    ((IrType *)ptrType)->strct.dctorFwdAst =
        ((IrType *)structType)->strct.dctorFwdAst;
    ((IrType *)ptrType)->strct.copyAst =
        ((IrType *)structType)->strct.copyAst;
    cacheType(ctx, type, ptrType);

    u64 fi = 0;
    for (u64 i = 0; i < count; i++) {
        const NamedTypeMember *m = &type->tClass.members->members[i];
        if (!nodeIs(m->decl, FieldDecl))
            continue;
        fields[fi].name = m->name;
        fields[fi].type = lowerType(ctx, m->type);
        fields[fi].bits = m->decl->structField.bits;
        fi++;
    }
    result = ptrType;
    return result;
}

static const IrType *lowerUnionType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    u64 count = type->tUnion.count;
    IrField *unionFields =
        allocFromMemPool(ctx->pool, sizeof(IrField) * count);
    for (u64 i = 0; i < count; i++) {
        unionFields[i].name = makeStringf(ctx->strings, "_%u", (unsigned)i);
        unionFields[i].type = lowerType(ctx, type->tUnion.members[i].type);
        unionFields[i].bits = 0;
    }
    // Payload named after the union shape, not the AST index
    cstring name = makeDescriptiveUnionName(ctx, type, unionFields, count);
    cstring display = makeDescriptiveUnionDisplay(ctx, unionFields, count);
    cstring unionName = makeStringConcat(ctx->strings, name, "_val");
    Flags flags = isDestructible(type) ? flgReferenceMembers : flgNone;
    const IrType *payloadType = makeIrUntaggedUnionType(
        ctx->types,
        flags,
        (IrStructOpts){.structural = true},
        unionName,
        unionName,
        unionFields,
        count);

    IrField *structFields =
        allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
    structFields[0] =
        (IrField){.name = S_tag,
                  .type = getIrPrimitiveType(ctx->types, irTypeU32),
                  .bits = 0};
    structFields[1] =
        (IrField){.name = S_val, .type = payloadType, .bits = 0};

    result = makeIrStructType(
        ctx->types,
        flags,
        (IrStructOpts){.category = irTypeCatUnion, .structural = true},
        name,
        display,
        structFields,
        2);
    ((IrType *)result)->index = type->index;
    if (isDestructible(type)) {
        csAssert(type->tUnion.destructorFunc && type->tUnion.destructorFunc->func.decl,
                 "destructible union %u missing destructor declaration",
                 (unsigned)type->index);
        ((IrType *)result)->strct.dctorAst =
            type->tUnion.destructorFunc->func.decl;
    }
    if (type->tUnion.copyFunc && type->tUnion.copyFunc->func.decl)
        ((IrType *)result)->strct.copyAst =
            type->tUnion.copyFunc->func.decl;
    return result;
}

static const IrType *lowerEnumType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const IrType *baseType = lowerType(
        ctx,
        type->tEnum.base ?: getPrimitiveType(ctx->driver->types, prtI32));
    cstring rawName =
        type->name
            ?: makeStringf(ctx->strings, "Enum%u", (unsigned)type->index);
    cstring name = type->ns
                       ? makeStringf(ctx->strings, "%s_%s", type->ns, rawName)
                       : rawName;
    u64 optCount = type->tEnum.optionsCount;
    IrEnumOption *irOptions =
        optCount ? allocFromMemPool(ctx->pool, sizeof(IrEnumOption) * optCount)
                 : NULL;
    for (u64 i = 0; i < optCount; i++) {
        irOptions[i] = (IrEnumOption){
            .name = type->tEnum.options[i].name,
            .value = (u64)type->tEnum.options[i].value,
        };
    }
    result = makeIrEnumType(
        ctx->types, flgNone, name, type->name, baseType, irOptions, optCount);
    return result;
}

static const IrType *lowerFuncType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const AstNode *decl = type->func.decl;
    const AstNode *astParams =
        (decl && nodeIs(decl, FuncDecl) && decl->funcDecl.signature)
            ? decl->funcDecl.signature->params
            : NULL;
    // Only the C varargs marker of an extern is not a parameter, the
    // expanded `...args` of a generic function are real parameters
    bool skipVariadic = decl && hasFlag(decl, Extern);
    u64 count = 0;
    if (astParams) {
        for (const AstNode *p = astParams; p; p = p->next)
            count += (skipVariadic && hasFlag(p, Variadic)) ? 0 : 1;
    }
    else {
        count = type->func.paramsCount;
    }
    const IrType **params =
        allocFromMemPool(ctx->pool, sizeof(IrType *) * (count ?: 1));
    if (astParams) {
        u64 i = 0;
        for (const AstNode *p = astParams; p; p = p->next) {
            if (skipVariadic && hasFlag(p, Variadic))
                continue;
            const Type *pType =
                p->type ? p->type
                        : (p->funcParam.type ? p->funcParam.type->type : NULL);
            params[i++] = lowerType(ctx, pType);
        }
    }
    else {
        for (u64 i = 0; i < count; i++) {
            params[i] = lowerType(ctx, type->func.params[i]);
        }
    }
    const IrType *ret = lowerType(ctx, type->func.retType);
    bool isVariadic =
        hasFlag(type, Variadic) ||
        (type->func.decl && hasFlag(type->func.decl, Variadic));
    result =
        makeIrFuncType(ctx->types, flgNone, params, count, ret, isVariadic);
    return result;
}

static const IrType *lowerClosureType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const IrType *voidPtrType = makeIrPointerType(
        ctx->types,
        flgNone, irTypeCatNone,
        getIrPrimitiveType(ctx->types, irTypeVoid));
    const IrType *fnType = lowerClosureFnPtrType(ctx, type);
    const IrType *dtorType = makeIrFuncType(
        ctx->types,
        flgNone,
        (const IrType *[]){voidPtrType},
        1,
        getIrPrimitiveType(ctx->types, irTypeVoid),
        false);
    IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 3);
    fields[0] = (IrField){.name = S_env, .type = voidPtrType, .bits = 0};
    fields[1] = (IrField){.name = S_fn, .type = fnType, .bits = 0};
    fields[2] = (IrField){.name = S_dtor, .type = dtorType, .bits = 0};
    cstring name =
        makeStringf(ctx->strings, "Closure_%u", (unsigned)type->index);
    cstring display =
        makeStringf(ctx->strings, "Closure_%u", (unsigned)type->index);
    Flags flags = isDestructible(type) ? flgReferenceMembers : flgNone;
    result = makeIrStructType(ctx->types,
                              flags,
                              (IrStructOpts){.category = irTypeCatClosure},
                              name,
                              display,
                              fields,
                              3);
    ((IrType *)result)->index = type->index;
    return result;
}

static const IrType *lowerResultType(LowerContext *ctx, const Type *type)
{
    const IrType *result = NULL;
    const IrType *targetType = lowerType(ctx, type->result.target);
    const Type *baseExceptionType = findBuiltinType(S_Exception);
    const IrType *exceptionType =
        baseExceptionType
            ? lowerType(ctx, baseExceptionType)
            : makeIrPointerType(ctx->types,
                                flgNone, irTypeCatNone,
                                getIrPrimitiveType(ctx->types, irTypeVoid));

    IrField *valFields = NULL;
    const IrType *valUnionType = NULL;

    if (isVoidType(type->result.target)) {
        valUnionType = exceptionType;
    }
    else {
        valFields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
        valFields[0] =
            (IrField){.name = makeString(ctx->strings, "_0"),
                      .type = targetType,
                      .index = 0};
        valFields[1] =
            (IrField){.name = makeString(ctx->strings, "_1"),
                      .type = exceptionType,
                      .index = 1};
        cstring unionName =
            makeStringConcat(ctx->strings,
                             makeDescriptiveResultName(ctx, type, targetType),
                             "_val");
        valUnionType = makeIrUntaggedUnionType(ctx->types,
                                               flgNone,
                                               (IrStructOpts){.structural = true},
                                               unionName,
                                               unionName,
                                               valFields,
                                               2);
    }

    IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
    fields[0] = (IrField){.name = S_tag,
                          .type = getIrPrimitiveType(ctx->types, irTypeU32),
                          .index = 0};
    fields[1] = (IrField){.name = S_val, .type = valUnionType, .index = 1};
    cstring name = makeDescriptiveResultName(ctx, type, targetType);
    cstring display = makeDescriptiveResultDisplay(ctx, targetType);
    result = makeIrStructType(
        ctx->types,
        flgReferenceMembers,
        (IrStructOpts){.category = irTypeCatResult, .structural = true},
        name,
        display,
        fields,
        2);
    return result;
}

const IrType *lowerType(LowerContext *ctx, const Type *type)
{
    if (type == NULL)
        return getIrPrimitiveType(ctx->types, irTypeVoid);

    type = resolveUnThisUnwrapType(type);
    const IrType *cached = findCachedType(ctx, type);
    if (cached)
        return cached;

    const IrType *result = NULL;
    switch (type->tag) {
    case typPrimitive:
        result = lowerPrimitiveType(ctx, type);
        break;
    case typVoid:
        result = getIrPrimitiveType(ctx->types, irTypeVoid);
        break;
    case typPointer:
        result = lowerPointerType(ctx, type);
        break;
    case typReference:
        result = lowerReferenceType(ctx, type);
        break;
    case typString:
        result = makeIrPointerType(
            ctx->types, flgNone, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeChar));
        break;
    case typArray:
        result = lowerArrayType(ctx, type);
        break;
    case typTuple:
        result = lowerTupleType(ctx, type);
        break;
    case typOptional:
        result = lowerOptionalType(ctx, type);
        break;
    case typStruct:
        result = lowerStructType(ctx, type);
        break;
    case typUntaggedUnion:
        result = lowerUntaggedUnionType(ctx, type);
        break;
    case typClass:
        result = lowerClassType(ctx, type);
        break;
    case typUnion:
        result = lowerUnionType(ctx, type);
        break;
    case typEnum:
        result = lowerEnumType(ctx, type);
        break;
    case typFunc:
        result = lowerFuncType(ctx, type);
        break;
    case typClosure:
        result = lowerClosureType(ctx, type);
        break;
    case typResult:
        result = lowerResultType(ctx, type);
        break;
    case typThis:
        result = lowerType(ctx, type->_this.that);
        break;
    default:
        result = getIrPrimitiveType(ctx->types, irTypeOpaque);
        break;
    }

    if (result && isDestructible(type)) {
        ((IrType *)result)->flags |= flgReferenceMembers;
    }

    cacheType(ctx, type, result);
    return result;
}
