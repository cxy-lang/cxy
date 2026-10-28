#include "type.h"
#include "core/hash.h"

#include <string.h>

// The reference-members flag of a slice is derived from how it was spelled in
// the source and may be OR-ed on after interning, so it is not part of identity.
static u64 irTypeIdentityFlags(const IrType *type)
{
    if (type->category == irTypeCatSlice)
        return type->flags & ~(u64)flgReferenceMembers;
    return type->flags;
}

static HashCode hashIrType(HashCode hash, const IrType *type)
{
    hash = hashUint32(hash, type->tag);
    hash = hashUint32(hash, (u32)type->category);
    hash = hashUint64(hash, irTypeIdentityFlags(type));
    if (type->category == irTypeCatEnum && type->name)
        return hashStr(hash, type->name);
    switch (type->tag) {
    case irTypePointer:
        return hashPtr(hash, type->pointer.pointee);
    case irTypeArray:
        hash = hashPtr(hash, type->array.elem);
        return hashUint64(hash, type->array.len);
    case irTypeStruct:
    case irTypeUntaggedUnion:
        if (type->name)
            hash = hashStr(hash, type->name);
        hash = hashUint64(hash, type->strct.count);
        for (u64 i = 0; i < type->strct.count; i++) {
            if (type->strct.fields[i].name)
                hash = hashStr(hash, type->strct.fields[i].name);
            hash = hashPtr(hash, type->strct.fields[i].type);
        }
        return hash;
    case irTypeFunc:
        hash = hashPtr(hash, type->func.ret);
        hash = hashUint64(hash, type->func.count);
        hash = hashUint32(hash, type->func.variadic);
        for (u64 i = 0; i < type->func.count; i++)
            hash = hashPtr(hash, type->func.params[i]);
        return hash;
    default:
        return hash;
    }
}

static bool compareIrTypes(const void *lhs, const void *rhs)
{
    const IrType *l = *(const IrType **)lhs;
    const IrType *r = *(const IrType **)rhs;
    if (l == r)
        return true;
    if (l->tag != r->tag || l->category != r->category ||
        irTypeIdentityFlags(l) != irTypeIdentityFlags(r))
        return false;

    switch (l->tag) {
    case irTypePointer:
        return l->pointer.pointee == r->pointer.pointee;
    case irTypeArray:
        return l->array.len == r->array.len && l->array.elem == r->array.elem;
    case irTypeStruct:
    case irTypeUntaggedUnion:
        if (l->name != r->name && (!l->name || !r->name || strcmp(l->name, r->name) != 0))
            return false;
        if (l->strct.count != r->strct.count)
            return false;
        for (u64 i = 0; i < l->strct.count; i++) {
            if (l->strct.fields[i].name != r->strct.fields[i].name)
                return false;
            if (l->strct.fields[i].type != r->strct.fields[i].type)
                return false;
        }
        return true;
    case irTypeFunc:
        if (l->func.ret != r->func.ret || l->func.count != r->func.count ||
            l->func.variadic != r->func.variadic)
            return false;
        for (u64 i = 0; i < l->func.count; i++) {
            if (l->func.params[i] != r->func.params[i])
                return false;
        }
        return true;
    default:
        return true;
    }
}

static const IrType *getOrInsertIrType(IrTypeTable *table, IrType *type)
{
    HashCode hash = hashIrType(hashInit(), type);
    const IrType **found = findInHashTable(
        &table->types, &type, hash, sizeof(IrType *), compareIrTypes);
    if (found)
        return *found;

    IrType *allocated = allocFromMemPool(table->pool, sizeof(IrType));
    memcpy(allocated, type, sizeof(IrType));
    allocated->index = table->count++;
    insertInHashTable(&table->types, &allocated, hash, sizeof(IrType *), compareIrTypes);
    return allocated;
}

static bool compareIrTypeNames(const void *lhs, const void *rhs)
{
    return strcmp(*(cstring *)lhs, *(cstring *)rhs) == 0;
}

static bool isIrTypeNameTaken(IrTypeTable *table, cstring name)
{
    return findInHashTable(&table->names,
                           &name,
                           hashStr(hashInit(), name),
                           sizeof(cstring),
                           compareIrTypeNames) != NULL;
}

static void reserveIrTypeName(IrTypeTable *table, cstring name)
{
    insertInHashTable(&table->names,
                      &name,
                      hashStr(hashInit(), name),
                      sizeof(cstring),
                      compareIrTypeNames);
}

// Named struct/union interning. Structural types whose name collides with a
// different shape probe name, name_2, name_3... deterministically.
static const IrType *getOrInsertNamedIrType(IrTypeTable *table,
                                            IrType *type,
                                            bool structural)
{
    if (type->name == NULL)
        return getOrInsertIrType(table, type);

    cstring base = type->name;
    bool keepDisplay = type->display == base;
    for (u32 n = 1;; n++) {
        if (n > 1) {
            type->name = makeStringf(table->strings, "%s_%u", base, n);
            if (keepDisplay)
                type->display = type->name;
        }
        HashCode hash = hashIrType(hashInit(), type);
        const IrType **found = findInHashTable(
            &table->types, &type, hash, sizeof(IrType *), compareIrTypes);
        if (found)
            return *found;
        if (!structural || !isIrTypeNameTaken(table, type->name))
            break;
    }

    reserveIrTypeName(table, type->name);
    return getOrInsertIrType(table, type);
}

static void initPrimitive(IrTypeTable *table, IrTypeTag tag, cstring name, u64 size, u64 align)
{
    IrType type = {.tag = tag, .size = size, .align = align, .name = name, .display = name};
    table->primitives[tag] = getOrInsertIrType(table, &type);
}

IrTypeTable *newIrTypeTable(MemPool *pool, StrPool *strings)
{
    IrTypeTable *table = allocFromMemPool(pool, sizeof(IrTypeTable));
    table->pool = pool;
    table->strings = strings;
    table->types = newHashTable(sizeof(IrType *), pool);
    table->names = newHashTable(sizeof(cstring), pool);

    initPrimitive(table, irTypeVoid, "void", 0, 1);
    initPrimitive(table, irTypeBool, "bool", 1, 1);
    initPrimitive(table, irTypeChar, "char", 1, 1);
    initPrimitive(table, irTypeI8, "i8", 1, 1);
    initPrimitive(table, irTypeI16, "i16", 2, 2);
    initPrimitive(table, irTypeI32, "i32", 4, 4);
    initPrimitive(table, irTypeI64, "i64", 8, 8);
    initPrimitive(table, irTypeI128, "i128", 16, 16);
    initPrimitive(table, irTypeU8, "u8", 1, 1);
    initPrimitive(table, irTypeU16, "u16", 2, 2);
    initPrimitive(table, irTypeU32, "u32", 4, 4);
    initPrimitive(table, irTypeU64, "u64", 8, 8);
    initPrimitive(table, irTypeU128, "u128", 16, 16);
    initPrimitive(table, irTypeF32, "f32", 4, 4);
    initPrimitive(table, irTypeF64, "f64", 8, 8);
    initPrimitive(table, irTypeOpaque, "opaque", 0, 1);

    return table;
}

void freeIrTypeTable(IrTypeTable *table)
{
    if (table) {
        freeHashTable(&table->types);
        freeHashTable(&table->names);
    }
}

const IrType *getIrPrimitiveType(IrTypeTable *table, IrTypeTag tag)
{
    csAssert0(tag < irTypeCOUNT);
    return table->primitives[tag];
}

const IrType *makeIrClassType(IrTypeTable *table, Flags flags, const IrType *structType)
{
    cstring display = makeStringf(table->strings, "^%s", structType ? (structType->display ?: structType->name) : "void");
    IrType type = {
        .tag = irTypePointer,
        .category = irTypeCatClass,
        .flags = flags,
        .size = sizeof(void *),
        .align = sizeof(void *),
        .display = display,
        .pointer = {.pointee = structType}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrPointerType(IrTypeTable *table, Flags flags, IrTypeCategory category, const IrType *pointee)
{
    cstring display = makeStringf(table->strings, "^%s", pointee ? (pointee->display ?: pointee->name) : "void");
    IrType type = {
        .tag = irTypePointer,
        .category = category,
        .flags = flags,
        .size = sizeof(void *),
        .align = sizeof(void *),
        .display = display,
        .pointer = {.pointee = pointee}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrArrayType(IrTypeTable *table, Flags flags, IrTypeCategory category, const IrType *elem, u64 len)
{
    u64 size = (len == UINT64_MAX) ? 0 : (elem->size * len);
    cstring display = (len == UINT64_MAX)
                          ? makeStringf(table->strings, "[%s]", elem ? (elem->display ?: elem->name) : "void")
                          : makeStringf(table->strings, "[%s, %llu]", elem ? (elem->display ?: elem->name) : "void", (unsigned long long)len);
    IrType type = {
        .tag = irTypeArray,
        .category = category,
        .flags = flags,
        .size = size,
        .align = elem->align,
        .display = display,
        .array = {.elem = elem, .len = len}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrStructType(IrTypeTable *table, Flags flags, IrStructOpts opts, cstring name, cstring display, IrField *fields, u64 count)
{
    u64 offset = 0, maxAlign = 1;
    for (u64 i = 0; i < count; i++) {
        u64 align = opts.isPacked ? 1 : (fields[i].type ? fields[i].type->align : 1);
        if (align > maxAlign)
            maxAlign = align;
        offset = (offset + align - 1) & ~(align - 1);
        fields[i].offset = offset;
        fields[i].index = (u32)i;
        offset += fields[i].type ? fields[i].type->size : 0;
    }
    if (opts.align > maxAlign)
        maxAlign = opts.align;
    u64 totalSize = (offset + maxAlign - 1) & ~(maxAlign - 1);

    IrType type = {
        .tag = irTypeStruct,
        .category = opts.category,
        .flags = flags,
        .name = name,
        .display = display ?: name,
        .size = totalSize,
        .align = maxAlign,
        .isPacked = opts.isPacked,
        .strct = {.fields = fields, .count = count}};
    return getOrInsertNamedIrType(table, &type, opts.structural);
}

const IrType *makeIrUntaggedUnionType(IrTypeTable *table, Flags flags, IrStructOpts opts, cstring name, cstring display, IrField *fields, u64 count)
{
    u64 maxSize = 0, maxAlign = 1;
    for (u64 i = 0; i < count; i++) {
        fields[i].offset = 0;
        fields[i].index = (u32)i;
        if (fields[i].type) {
            if (fields[i].type->size > maxSize)
                maxSize = fields[i].type->size;
            if (fields[i].type->align > maxAlign)
                maxAlign = fields[i].type->align;
        }
    }
    if (opts.align > maxAlign)
        maxAlign = opts.align;
    u64 totalSize = (maxSize + maxAlign - 1) & ~(maxAlign - 1);

    IrType type = {
        .tag = irTypeUntaggedUnion,
        .category = opts.category,
        .flags = flags,
        .name = name,
        .display = display ?: name,
        .size = totalSize,
        .align = maxAlign,
        .isPacked = opts.isPacked,
        .strct = {.fields = fields, .count = count}};
    return getOrInsertNamedIrType(table, &type, opts.structural);
}

const IrType *makeIrFuncType(IrTypeTable *table, Flags flags, const IrType **params, u64 count, const IrType *ret, bool variadic)
{
    char buf[1024];
    int len = snprintf(buf, sizeof(buf), "func(");
    for (u64 i = 0; i < count && len < (int)sizeof(buf) - 64; i++) {
        if (i > 0)
            len += snprintf(buf + len, sizeof(buf) - len, ", ");
        cstring pName = params[i] ? (params[i]->display ?: params[i]->name) : "void";
        if (len < (int)sizeof(buf) - 32)
            len += snprintf(buf + len, sizeof(buf) - len, "%s", pName);
    }
    if (variadic && len < (int)sizeof(buf) - 32)
        len += snprintf(buf + len, sizeof(buf) - len, ", ...");
    if (len < (int)sizeof(buf) - 32)
        snprintf(buf + len, sizeof(buf) - len, "): %s", ret ? (ret->display ?: ret->name) : "void");
    else
        snprintf(buf + sizeof(buf) - 8, 8, ")...");
    cstring display = makeString(table->strings, buf);

    const IrType **savedParams = NULL;
    if (count > 0 && params != NULL) {
        savedParams = allocFromMemPool(table->pool, sizeof(IrType *) * count);
        memcpy(savedParams, params, sizeof(IrType *) * count);
    }

    IrType type = {
        .tag = irTypeFunc,
        .flags = flags,
        .size = sizeof(void *),
        .align = sizeof(void *),
        .display = display,
        .func = {.params = savedParams, .count = count, .ret = ret, .variadic = variadic}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrEnumType(IrTypeTable *table,
                             Flags flags,
                             cstring name,
                             cstring display,
                             const IrType *baseType,
                             IrEnumOption *options,
                             u64 optionsCount)
{
    IrType type = {
        .tag = baseType ? baseType->tag : irTypeI64,
        .category = irTypeCatEnum,
        .flags = flags,
        .size = baseType ? baseType->size : 8,
        .align = baseType ? baseType->align : 8,
        .name = name,
        .display = display ?: name,
        .enm = {.options = options, .count = optionsCount}};
    return getOrInsertIrType(table, &type);
}
