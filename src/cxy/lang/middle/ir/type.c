#include "type.h"
#include "core/hash.h"

#include <string.h>

static HashCode hashIrType(HashCode hash, const IrType *type)
{
    hash = hashUint32(hash, type->tag);
    hash = hashUint32(hash, (u32)type->category);
    hash = hashUint64(hash, type->flags);
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
    if (l->tag != r->tag || l->category != r->category || l->flags != r->flags)
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

const IrType *makeIrPointerType(IrTypeTable *table, Flags flags, const IrType *pointee)
{
    cstring display = makeStringf(table->strings, "^%s", pointee ? (pointee->display ?: pointee->name) : "void");
    IrType type = {
        .tag = irTypePointer,
        .flags = flags,
        .size = sizeof(void *),
        .align = sizeof(void *),
        .display = display,
        .pointer = {.pointee = pointee}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrArrayType(IrTypeTable *table, Flags flags, const IrType *elem, u64 len)
{
    u64 size = (len == UINT64_MAX) ? 0 : (elem->size * len);
    cstring display = (len == UINT64_MAX)
                          ? makeStringf(table->strings, "[%s]", elem ? (elem->display ?: elem->name) : "void")
                          : makeStringf(table->strings, "[%s, %llu]", elem ? (elem->display ?: elem->name) : "void", (unsigned long long)len);
    IrType type = {
        .tag = irTypeArray,
        .flags = flags,
        .size = size,
        .align = elem->align,
        .display = display,
        .array = {.elem = elem, .len = len}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrStructType(IrTypeTable *table, Flags flags, cstring name, cstring display, IrField *fields, u64 count)
{
    u64 offset = 0, maxAlign = 1;
    for (u64 i = 0; i < count; i++) {
        u64 align = fields[i].type ? fields[i].type->align : 1;
        if (align > maxAlign)
            maxAlign = align;
        offset = (offset + align - 1) & ~(align - 1);
        fields[i].offset = offset;
        fields[i].index = (u32)i;
        offset += fields[i].type ? fields[i].type->size : 0;
    }
    u64 totalSize = (offset + maxAlign - 1) & ~(maxAlign - 1);

    IrType type = {
        .tag = irTypeStruct,
        .flags = flags,
        .name = name,
        .display = display ?: name,
        .size = totalSize,
        .align = maxAlign,
        .strct = {.fields = fields, .count = count}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrUntaggedUnionType(IrTypeTable *table, Flags flags, cstring name, cstring display, IrField *fields, u64 count)
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
    u64 totalSize = (maxSize + maxAlign - 1) & ~(maxAlign - 1);

    IrType type = {
        .tag = irTypeUntaggedUnion,
        .flags = flags,
        .name = name,
        .display = display ?: name,
        .size = totalSize,
        .align = maxAlign,
        .strct = {.fields = fields, .count = count}};
    return getOrInsertIrType(table, &type);
}

const IrType *makeIrFuncType(IrTypeTable *table, Flags flags, const IrType **params, u64 count, const IrType *ret, bool variadic)
{
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "func(");
    for (u64 i = 0; i < count; i++) {
        if (i > 0)
            len += snprintf(buf + len, sizeof(buf) - len, ", ");
        cstring pName = params[i] ? (params[i]->display ?: params[i]->name) : "void";
        len += snprintf(buf + len, sizeof(buf) - len, "%s", pName);
    }
    if (variadic)
        len += snprintf(buf + len, sizeof(buf) - len, ", ...");
    snprintf(buf + len, sizeof(buf) - len, "): %s", ret ? (ret->display ?: ret->name) : "void");
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
