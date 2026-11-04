#include "lower.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/mangle.h"
#include "lang/middle/memory/memory.h"
#include "lang/middle/builtins.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TypeMapping {
    const Type *astType;
    const IrType *irType;
} TypeMapping;

static inline bool isBlockTerminated(const IrNode *bb)
{
    if (bb == NULL || bb->tag != irBasicBlock || bb->basicBlock.last == NULL)
        return false;
    IrNode *last = bb->basicBlock.last;
    switch (bb->basicBlock.last->tag) {
    case irBranch:
    case irBranchIf:
    case irSwitch:
    case irReturn:
    case irUnreachable:
        return true;
    default:
        return false;
    }
}

typedef struct VarNameEntry {
    cstring name;
    u32 count;
} VarNameEntry;

static IrNode *getOrCreateScopeReturnBlock(LowerContext *ctx,
                                           u32 scopeId,
                                           const FileLoc *loc);

static bool compareVarNameEntries(const void *lhs, const void *rhs)
{
    const VarNameEntry *l = lhs;
    const VarNameEntry *r = rhs;
    return l->name == r->name;
}

static cstring makeUniqueVarName(LowerContext *ctx, cstring displayName)
{
    if (displayName == NULL)
        return displayName;

    VarNameEntry *found = findInHashTable(&ctx->varNames,
                                          &(VarNameEntry){.name = displayName},
                                          hashPtr(hashInit(), displayName),
                                          sizeof(VarNameEntry),
                                          compareVarNameEntries);

    if (found == NULL) {
        VarNameEntry entry = {.name = displayName, .count = 1};
        insertInHashTable(&ctx->varNames,
                          &entry,
                          hashPtr(hashInit(), displayName),
                          sizeof(VarNameEntry),
                          compareVarNameEntries);
        return displayName;
    }

    u32 suffix = found->count++;
    return makeStringf(ctx->strings, "%s_%u", displayName, suffix);
}

static inline void emitStmt(LowerContext *ctx, IrNode *stmt)
{
    if (ctx->currentBlock && stmt && !isBlockTerminated(ctx->currentBlock)) {
        irAppendBlockStmt(ctx->currentBlock, stmt);
        // Only scopes with destructible vars need an unwind path on return
        if (stmt->tag == irVarDecl && hasFlag(stmt, ReferenceMembers) &&
            stmt->varDecl.scopeId < ctx->scopes.size)
            dynArrayAt(ScopeInfo *, &ctx->scopes, stmt->varDecl.scopeId)
                .hasDrops = true;
    }
}

static inline IrNode *createBlock(LowerContext *ctx, cstring name)
{
    cstring blockName = makeStringf(ctx->strings, "_L_%s", name ?: "bb");
    IrNode *bb = makeIrBasicBlock(ctx->pool, NULL, blockName);
    if (ctx->currentFunc) {
        irAppendFunctionBlock(ctx->currentFunc, bb);
    }
    return bb;
}

static inline void setBlock(LowerContext *ctx, IrNode *bb)
{
    ctx->currentBlock = bb;
}

static inline void emitBranchIfNotTerminated(LowerContext *ctx, IrNode *target)
{
    if (ctx->currentBlock && !isBlockTerminated(ctx->currentBlock)) {
        emitStmt(ctx, makeIrBranch(ctx->pool, NULL, target));
    }
}


static IrNode *getExceptionPushCallee(LowerContext *ctx)
{
    if (ctx->exceptionPushCallee)
        return ctx->exceptionPushCallee;

    const Type *baseExType = findBuiltinType(S_Exception);
    if (baseExType == NULL)
        return NULL;

    const AstNode *pushDecl = NULL;
    const AstNode *decl = getTypeDecl(baseExType);
    if (decl && nodeIs(decl, ClassDecl)) {
        for (const AstNode *mem = decl->classDecl.members; mem; mem = mem->next) {
            if (nodeIs(mem, FuncDecl) && mem->funcDecl.name == S_push) {
                pushDecl = mem;
                break;
            }
        }
    }

    const IrType *pushType = pushDecl ? lowerType(ctx, pushDecl->type) : NULL;
    ctx->exceptionPushCallee = makeIrGlobalRef(
        ctx->pool,
        builtinLoc(),
        "builtins_Exception_push",
        pushDecl ? pushDecl->ir : NULL,
        pushType);

    return ctx->exceptionPushCallee;
}

static void emitExceptionTracePush(LowerContext *ctx,
                                   const FileLoc *loc,
                                   IrNode *errVal)
{
    if (errVal == NULL)
        return;

    IrNode *pushCallee = getExceptionPushCallee(ctx);
    if (pushCallee == NULL)
        return;

    const IrType *strType = makeIrPointerType(
        ctx->types, flgConst, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeChar));
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *voidType = getIrPrimitiveType(ctx->types, irTypeVoid);

    cstring modName = ctx->module ? ctx->module->module.name : "";
    cstring clsName = ctx->currentFunc && ctx->currentFunc->ast &&
                              ctx->currentFunc->ast->parentScope &&
                              isClassOrStructAstNode(ctx->currentFunc->ast->parentScope)
                          ? getDeclarationName(ctx->currentFunc->ast->parentScope)
                          : "";
    cstring funName = ctx->currentFunc
                          ? (ctx->currentFunc->function.display
                                 ?: ctx->currentFunc->function.name)
                          : "";
    cstring fileName = loc && loc->fileName ? loc->fileName : "";
    u64 row = loc ? loc->begin.row : 0;

    IrNode *arg0 = cloneIrNode(ctx->pool, errVal);
    IrNode *arg1 = makeIrStringConst(ctx->pool, loc, modName, strType);
    IrNode *arg2 = makeIrStringConst(ctx->pool, loc, clsName, strType);
    IrNode *arg3 = makeIrStringConst(ctx->pool, loc, funName, strType);
    IrNode *arg4 = makeIrStringConst(ctx->pool, loc, fileName, strType);
    IrNode *arg5 = makeIrIntConst(ctx->pool, loc, row, false, u64Type);

    arg0->next = arg1;
    arg1->next = arg2;
    arg2->next = arg3;
    arg3->next = arg4;
    arg4->next = arg5;

    IrNode *pushCall =
        makeIrCall(ctx->pool, loc, pushCallee, arg0, 6, voidType);
    emitStmt(ctx, pushCall);
}

static inline bool isBackendCopyExpr(const AstNode *node)
{
    return node && nodeIs(node, BackendCall) &&
           node->backendCallExpr.func == bfiCopy &&
           node->backendCallExpr.args &&
           node->backendCallExpr.args->next == NULL;
}

static inline bool isIrVariableDecl(const IrNode *node)
{
    return node && (node->tag == irVarDecl || node->tag == irFuncParam ||
                    node->tag == irGlobalVar);
}

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

static const IrType *lowerClosureFnPtrType(LowerContext *ctx,
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
        break;
    case typVoid:
        result = getIrPrimitiveType(ctx->types, irTypeVoid);
        break;
    case typPointer: {
        const Type *pointed = type->pointer.pointed;
        const IrType *pointedIr = lowerType(ctx, pointed);
        result = makeIrPointerType(ctx->types, flgNone, irTypeCatNone, pointedIr);
        break;
    }
    case typReference: {
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
        break;
    }
    case typString:
        result = makeIrPointerType(
            ctx->types, flgNone, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeChar));
        break;
    case typArray:
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
        break;
    case typTuple: {
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
        break;
    }
    case typOptional: {
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
        break;
    }
    case typStruct: {
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
        break;
    }
    case typUntaggedUnion: {
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
        break;
    }
    case typClass: {
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
        break;
    }
    case typUnion: {
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
        break;
    }
    case typEnum: {
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
        break;
    }
    case typFunc: {
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
        break;
    }
    case typClosure: {
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
        break;
    }
    case typResult: {
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
        break;
    }
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

static inline bool isIrLValueOrConst(const IrNode *node)
{
    if (node == NULL)
        return false;
    switch (node->tag) {
    case irVarRef:
    case irGlobalRef:
    case irMember:
    case irIndex:
    case irDeref:
    case irAddrOf:
    case irCast:
    case irMove:
    case irNullConst:
    case irBoolConst:
    case irCharConst:
    case irIntConst:
    case irFloatConst:
    case irStringConst:
        return true;
    case irAlias:
        return isIrLValueOrConst(node->alias.expr);
    default:
        return false;
    }
}

static bool isIrPureValueExpr(const IrNode *node)
{
    if (node == NULL)
        return false;
    if (isIrLValueOrConst(node))
        return true;
    if (node->tag == irUnary) {
        switch (node->unary.op) {
        case opPreInc:
        case opPostInc:
        case opPreDec:
        case opPostDec:
        case opDelete:
            return false;
        default:
            return isIrPureValueExpr(node->unary.operand);
        }
    }
    if (node->tag == irAlloca)
        return true;
    if (node->tag == irBinary)
        return isIrPureValueExpr(node->binary.lhs) && isIrPureValueExpr(node->binary.rhs);
    return false;
}

// Stores `val` in a fresh temporary and returns a reference to it.
// `borrowed` consumers only read the value, so an owned rvalue must be
// tracked (and dropped) as a regular local instead of an unowned temporary.
static IrNode *flattenIrValue(LowerContext *ctx,
                              AstNode *node,
                              IrNode *val,
                              bool borrowed)
{
    cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    Flags flags = flgTemporary;
    if (isIrTypeDestructible(val->type))
        flags |= flgReferenceMembers;
    if (borrowed && node->type) {
        const Type *astType = resolveUnThisUnwrapType(node->type);
        if (astType && !isReferenceType(astType) && !isPointerType(astType) &&
            isDestructible(astType))
            flags = flgReferenceMembers;
    }
    IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    tempName,
                                    tempName,
                                    val->type,
                                    ctx->currentScopeId);
    emitStmt(ctx, tempVar);
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          val));
    return makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static IrNode *lowerFlattenedOperandEx(AstVisitor *visitor, AstNode *node, bool borrowed)
{
    if (node == NULL)
        return NULL;
    astVisit(visitor, node);
    IrNode *val = (IrNode *)node->codegen;
    if (val == NULL)
        return NULL;

    if (isIrLValueOrConst(val) || (isIrPureValueExpr(val) && !isIrTypeDestructible(val->type)))
        return val;

    return flattenIrValue(getAstVisitorContext(visitor), node, val, borrowed);
}

// An element of a struct, tuple or array literal. A call, or a nested literal
// that owns something, is hoisted into its own statement like a call argument,
// so the memory pass sees the moves and copies inside it.
static IrNode *lowerCompositeElement(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node);
    IrNode *val = (IrNode *)node->codegen;
    if (val && ctx->currentFunc &&
        (val->tag == irCall ||
         (val->tag == irStructConst && isIrTypeDestructible(val->type))))
        return flattenIrValue(ctx, node, val, false);
    return val;
}

static IrNode *lowerFlattenedOperand(AstVisitor *visitor, AstNode *node)
{
    return lowerFlattenedOperandEx(visitor, node, false);
}

static IrNode *lowerBorrowedOperand(AstVisitor *visitor, AstNode *node)
{
    return lowerFlattenedOperandEx(visitor, node, true);
}

static IrNode *lowerConditionOperand(LowerContext *ctx,
                                     AstVisitor *visitor,
                                     AstNode *condNode)
{
    const Type *astType = condNode->type;
    IrNode *cond = lowerBorrowedOperand(visitor, condNode);
    if (cond == NULL) {
        return makeIrBoolConst(ctx->pool,
                               &condNode->loc,
                               false,
                               getIrPrimitiveType(ctx->types, irTypeBool));
    }

    if (isOptionalType(astType)) {
        return makeIrMember(ctx->pool,
                            &condNode->loc,
                            cond,
                            0,
                            "ok",
                            getIrPrimitiveType(ctx->types, irTypeBool));
    }
    if (isClosureType(astType) && cond->type && cond->type->tag == irTypeStruct &&
        cond->type->strct.count >= 2) {
        const IrType *fnPtrType = cond->type->strct.fields[1].type;
        IrNode *fnMember =
            makeIrMember(ctx->pool, &condNode->loc, cond, 1, S_fn, fnPtrType);
        IrNode *nullConst =
            makeIrNullConst(ctx->pool, &condNode->loc, fnPtrType);
        return makeIrBinary(ctx->pool,
                            &condNode->loc,
                            opNe,
                            fnMember,
                            nullConst,
                            getIrPrimitiveType(ctx->types, irTypeBool));
    }
    return cond;
}

static void lowerSliceIntoTarget(LowerContext *ctx,
                                 AstVisitor *visitor,
                                 const AstNode *expr,
                                 IrNode *targetLValue)
{
    // `[]` is the empty slice, all zero
    if (nodeIs(expr, ArrayExpr) && expr->arrayExpr.elements == NULL) {
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &expr->loc,
                              targetLValue,
                              makeIrNullConst(
                                  ctx->pool, &expr->loc, targetLValue->type)));
        return;
    }

    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const Type *exprAstType = expr->type;
    const IrType *elemIrType = targetLValue->type->strct.fields[0].type;
    u64 len =
        (exprAstType && isArrayType(exprAstType)) ? exprAstType->array.len : 0;

    IrNode *ptrTarget = makeIrMember(
        ctx->pool, &targetLValue->loc, targetLValue, 0, S_ptr, elemIrType);
    IrNode *lenTarget = makeIrMember(
        ctx->pool, &targetLValue->loc, targetLValue, 1, S_len, u64Type);

    if (nodeIs(expr, ArrayExpr)) {
        const IrType *fixedArrType = makeIrArrayType(ctx->types,
                                                     flgNone,
                                                     irTypeCatNone,
                                                     elemIrType->pointer.pointee,
                                                     len);
        cstring arrTempName =
            makeStringf(ctx->strings, "__arr%u", ctx->tempCounter++);
        IrNode *arrVar = makeIrVarDecl(ctx->pool,
                                       &expr->loc,
                                       flgTemporary,
                                       arrTempName,
                                       arrTempName,
                                       fixedArrType,
                                       ctx->currentScopeId);
        emitStmt(ctx, arrVar);

        u64 idx = 0;
        for (AstNode *elem = expr->arrayExpr.elements; elem;
             elem = elem->next, idx++) {
            IrNode *val = lowerFlattenedOperand(visitor, elem);
            IrNode *idxNode =
                makeIrIntConst(ctx->pool, &elem->loc, idx, false, u64Type);
            IrNode *elemSlot =
                makeIrIndex(ctx->pool,
                            &elem->loc,
                            makeIrVarRef(ctx->pool, &expr->loc, arrVar),
                            idxNode,
                            val->type);
            emitStmt(ctx, makeIrAssign(ctx->pool, &elem->loc, elemSlot, val));
        }

        IrNode *ptrVal = makeIrVarRef(ctx->pool, &expr->loc, arrVar);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, ptrTarget, ptrVal));
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &expr->loc,
                              lenTarget,
                              makeIrIntConst(
                                  ctx->pool, &expr->loc, len, false, u64Type)));
    }
    else if (nodeIs(expr, SliceExpr)) {
        astVisit(visitor, (AstNode *)expr);
        IrNode *sliceVal = (IrNode *)expr->codegen;
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, sliceVal));
    }
    else {
        IrNode *arrVal = lowerFlattenedOperand(visitor, (AstNode *)expr);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, ptrTarget, arrVal));
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &expr->loc,
                              lenTarget,
                              makeIrIntConst(
                                  ctx->pool, &expr->loc, len, false, u64Type)));
    }
}


static void lowerOptionalIntoTarget(LowerContext *ctx,
                                    AstVisitor *visitor,
                                    const AstNode *expr,
                                    IrNode *targetLValue)
{
    if (expr == NULL || nodeIs(expr, NullLit)) {
        IrNode *optVal = makeIrOptionalValue(ctx->pool, &targetLValue->loc, NULL, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &targetLValue->loc, targetLValue, optVal));
        return;
    }

    IrNode *val = lowerFlattenedOperand(visitor, (AstNode *)expr);
    IrNode *optVal = makeIrOptionalValue(ctx->pool, &expr->loc, val, targetLValue->type);
    emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, optVal));
}

static void lowerTaggedUnionIntoTarget(LowerContext *ctx,
                                       AstVisitor *visitor,
                                       const AstNode *expr,
                                       IrNode *targetLValue,
                                       const Type *targetAstType)
{
    if (expr == NULL) {
        IrNode *uVal = makeIrUnionValue(ctx->pool, &targetLValue->loc, 0, NULL, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &targetLValue->loc, targetLValue, uVal));
        return;
    }

    if (nodeIs(expr, UnionValueExpr)) {
        u32 idx = expr->unionValue.idx;
        IrNode *innerVal = lowerFlattenedOperand(visitor, expr->unionValue.value);
        IrNode *uVal = makeIrUnionValue(ctx->pool, &expr->loc, idx, innerVal, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, uVal));
    }
    else if (expr->type && isUnionType(expr->type)) {
        astVisit(visitor, (AstNode *)expr);
        IrNode *rhsVal = (IrNode *)expr->codegen;
        if (rhsVal)
            emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, rhsVal));
    }
    else {
        u32 idx = findUnionTypeIndex(targetAstType, expr->type);
        if (idx == UINT32_MAX) {
            logError(ctx->driver->L, &expr->loc, "type '{t}' is not a member of union type '{t}'",
                     (FormatArg[]){{.t = expr->type}, {.t = targetAstType}});
            return;
        }
        IrNode *innerVal = lowerFlattenedOperand(visitor, (AstNode *)expr);
        IrNode *uVal = makeIrUnionValue(ctx->pool, &expr->loc, idx, innerVal, targetLValue->type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, targetLValue, uVal));
    }
}

static void visitNullLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen =
        makeIrNullConst(ctx->pool, &node->loc, lowerType(ctx, node->type));
}

static void visitBoolLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrBoolConst(ctx->pool,
                                    &node->loc,
                                    node->boolLiteral.value,
                                    lowerType(ctx, node->type));
}

static void visitCharLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrCharConst(ctx->pool,
                                    &node->loc,
                                    node->charLiteral.value,
                                    lowerType(ctx, node->type));
}

static void visitIntegerLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrIntConst(ctx->pool,
                                   &node->loc,
                                   node->intLiteral.uValue,
                                   node->intLiteral.isNegative,
                                   lowerType(ctx, node->type));
}

static void visitFloatLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrFloatConst(ctx->pool,
                                     &node->loc,
                                     node->floatLiteral.value,
                                     lowerType(ctx, node->type));
}

static void visitStringLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrStringConst(ctx->pool,
                                      &node->loc,
                                      node->stringLiteral.value,
                                      lowerType(ctx, node->type));
}

static void appendNormalizedName(FormatState *state, const AstNode *node)
{
    if (node == NULL)
        return;

    switch (node->tag) {
    case astIdentifier:
        format(state, "{s}", (FormatArg[]){{.s = node->ident.value}});
        break;
    case astPath: {
        for (const AstNode *elem = node->path.elements; elem;
             elem = elem->next) {
            if (elem != node->path.elements)
                format(state, "_", NULL);
            format(state, "{s}", (FormatArg[]){{.s = elem->pathElement.name}});
        }
        break;
    }
    case astPathElem:
        format(state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
        break;
    case astMemberExpr:
        appendNormalizedName(state, node->memberExpr.target);
        format(state, "_", NULL);
        appendNormalizedName(state, node->memberExpr.member);
        break;
    case astTypeRef:
        if (node->type && node->type->name)
            format(state, "{s}", (FormatArg[]){{.s = node->type->name}});
        else
            format(state, "{s}", (FormatArg[]){{.s = node->_name}});
        break;
    default:
        if (node->_name)
            format(state, "{s}", (FormatArg[]){{.s = node->_name}});
        break;
    }
}

static cstring makeScopedSymbolName(LowerContext *ctx,
                                    const AstNode *decl,
                                    cstring baseName)
{
    if (decl && (hasFlag(decl, Main) || hasFlag(decl, Extern)))
        return baseName;

    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    cstring ns = (decl && decl->type && decl->type->ns) ? decl->type->ns : modName;
    if (ns == NULL || ns == S_main || baseName == NULL)
        return baseName;

    if (baseName == ns || strncmp(baseName, ns, strlen(ns)) == 0)
        return baseName;

    return makeStringf(ctx->strings, "%s_%s", ns, baseName);
}

// Compiler generated names use '$' (not writable in source), C can't have it
static cstring toCSymbolName(LowerContext *ctx, cstring name)
{
    if (name == NULL || strchr(name, '$') == NULL)
        return name;
    FormatState state = newFormatState(NULL, true);
    for (const char *p = name; *p; p++) {
        if (*p == '$')
            format(&state, "__", NULL);
        else
            append(&state, p, 1);
    }
    char *str = formatStateToString(&state);
    cstring res = makeString(ctx->strings, str);
    free(str);
    freeFormatState(&state);
    return res;
}

static cstring getFunctionSymbolNameRaw(LowerContext *ctx,
                                        const AstNode *funcDecl);

static cstring getFunctionSymbolName(LowerContext *ctx, const AstNode *funcDecl)
{
    if (funcDecl && funcDecl->ir && funcDecl->ir->tag == irFunction)
        return funcDecl->ir->function.name;
    return toCSymbolName(ctx, getFunctionSymbolNameRaw(ctx, funcDecl));
}

static cstring getFunctionSymbolNameRaw(LowerContext *ctx,
                                        const AstNode *funcDecl)
{
    if (funcDecl == NULL)
        return NULL;

    cstring funcName = getDeclarationName(funcDecl);
    if (hasFlag(funcDecl, Main) || funcName == S_main)
        return S__main;

    AstNode *parent = funcDecl->parentScope;
    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    cstring ns = (funcDecl->type && funcDecl->type->ns)
                     ? funcDecl->type->ns
                     : ((parent && parent->type && parent->type->ns) ? parent->type->ns : modName);
    if (parent && isClassOrStructAstNode(parent)) {
        cstring typeName = getDeclarationName(parent);
        if (ns == NULL || ns == S_main)
            return makeStringf(ctx->strings, "%s_%s", typeName, funcName);
        return makeStringf(
            ctx->strings, "%s_%s_%s", ns, typeName, funcName);
    }

    return makeScopedSymbolName(ctx, funcDecl, funcName);
}

static cstring getNormalizedName(LowerContext *ctx, const AstNode *node)
{
    if (node == NULL)
        return NULL;
    FormatState state = newFormatState(NULL, true);
    appendNormalizedName(&state, node);
    char *str = formatStateToString(&state);
    cstring res = (str && str[0]) ? makeString(ctx->strings, str) : NULL;
    free(str);
    freeFormatState(&state);
    return toCSymbolName(ctx, res);
}

static void lowerResolvedDecl(LowerContext *ctx,
                              AstNode *node,
                              const AstNode *decl,
                              const IrType *irType)
{
    if (nodeIs(decl, ExternDecl) && decl->externDecl.func != NULL) {
        decl = decl->externDecl.func;
    }

    if (decl && nodeIs(decl, EnumOptionDecl)) {
        const IrType *enumIrType = decl->type ? lowerType(ctx, decl->type) : irType;
        cstring optName = decl->enumOption.name;
        cstring cName = (enumIrType && enumIrType->name)
                            ? makeStringf(ctx->strings, "%s_%s", enumIrType->name, optName)
                            : optName;
        node->codegen =
            makeIrGlobalRef(ctx->pool, &node->loc, cName, NULL, enumIrType);
        return;
    }

    if (decl && decl->ir) {
        if (decl->ir->tag == irAlias) {
            node->codegen = cloneIrNode(ctx->pool, decl->ir->alias.expr);
            return;
        }
        if (isIrVariableDecl(decl->ir)) {
            node->codegen = makeIrVarRef(ctx->pool, &node->loc, decl->ir);
            return;
        }
        if (decl->ir->tag == irFunction) {
            node->codegen = makeIrGlobalRef(ctx->pool,
                                            &node->loc,
                                            decl->ir->function.name,
                                            decl->ir,
                                            decl->ir->type);
            return;
        }
    }

    if (decl && decl->codegen) {
        node->codegen = decl->codegen;
        return;
    }

    if (decl) {
        if (nodeIs(decl, FuncDecl)) {
            cstring cName = getFunctionSymbolName(ctx, decl);
            node->codegen =
                makeIrGlobalRef(ctx->pool, &node->loc, cName, decl->ir, irType);
            return;
        }
        if (nodeIs(decl, FieldDecl) || hasFlag(node, Member) ||
            hasFlag(node, AddThis)) {
            IrNode *thisParam =
                ctx->currentFunc ? ctx->currentFunc->function.params : NULL;
            if (thisParam == NULL) {
                logError(
                    ctx->driver->L,
                    &node->loc,
                    "cannot access member field '{s}' without 'this' context",
                    (FormatArg[]){{.s = getDeclarationName(decl)}});
                return;
            }
            IrNode *thisRef = makeIrVarRef(ctx->pool, &node->loc, thisParam);
            cstring fieldName = getDeclarationName(decl);
            u32 fieldIdx = decl->structField.index;
            node->codegen = makeIrMember(
                ctx->pool, &node->loc, thisRef, fieldIdx, fieldName, irType);
            return;
        }

        cstring name = getDeclarationName(decl) ?: getNormalizedName(ctx, node);
        cstring cName = makeScopedSymbolName(ctx, decl, name);
        node->codegen =
            makeIrGlobalRef(ctx->pool, &node->loc, cName, decl->ir, irType);
        return;
    }

    cstring identName =
        nodeIs(node, Identifier) ? node->ident.value : getDeclarationName(node);
    if (ctx->currentFunc && identName) {
        for (IrNode *p = ctx->currentFunc->function.params; p; p = p->next) {
            if (p->funcParam.name == identName) {
                node->codegen = makeIrVarRef(ctx->pool, &node->loc, p);
                return;
            }
        }
    }

    logError(ctx->driver->L,
             &node->loc,
             "unresolved symbol '{s}' during IR lowering",
             (FormatArg[]){{.s = identName ?: "unknown"}});
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    lowerResolvedDecl(ctx, node, node->ident.resolvesTo, irType);
}

static void visitPath(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    const AstNode *lastElem =
        node->path.elements ? getLastAstNode(node->path.elements) : NULL;
    const AstNode *decl = lastElem ? lastElem->pathElement.resolvesTo : NULL;
    lowerResolvedDecl(ctx, node, decl, irType);
}

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    AstNode *mem = node->memberExpr.member;

    if (nodeIsMemberFunctionReference(node) && nodeIs(mem, Identifier)) {
        const AstNode *resolvesTo = mem->ident.resolvesTo;
        // Simplify wraps functions of other modules in an ExternDecl
        if (resolvesTo && nodeIs(resolvesTo, ExternDecl) &&
            resolvesTo->externDecl.func != NULL)
            resolvesTo = resolvesTo->externDecl.func;
        if (resolvesTo == NULL) {
            logError(ctx->driver->L,
                     &node->loc,
                     "unresolved symbol '{s}' during IR lowering",
                     (FormatArg[]){{.s = mem->ident.value ?: "unknown"}});
            return;
        }
        if (nodeIs(resolvesTo, FuncDecl)) {
            cstring cName = getFunctionSymbolName(ctx, resolvesTo);
            node->codegen = makeIrGlobalRef(ctx->pool,
                                            &node->loc,
                                            cName,
                                            resolvesTo->ir,
                                            lowerType(ctx, node->type));
            return;
        }
        cstring normName = getNormalizedName(ctx, node);
        cstring cName = makeScopedSymbolName(ctx, resolvesTo, normName);
        node->codegen = makeIrGlobalRef(
            ctx->pool, &node->loc, cName, NULL, lowerType(ctx, node->type));
        return;
    }

    if (nodeIs(mem, Identifier) && mem->ident.resolvesTo &&
        nodeIs(mem->ident.resolvesTo, EnumOptionDecl)) {
        const AstNode *opt = mem->ident.resolvesTo;
        const IrType *enumIrType = lowerType(ctx, opt->type ?: (node->type ?: NULL));
        cstring optName = opt->enumOption.name;
        cstring cName = (enumIrType && enumIrType->name)
                            ? makeStringf(ctx->strings, "%s_%s", enumIrType->name, optName)
                            : optName;
        node->codegen =
            makeIrGlobalRef(ctx->pool, &node->loc, cName, NULL, enumIrType);
        return;
    }

    IrNode *target = lowerBorrowedOperand(visitor, node->memberExpr.target);
    const Type *targetAstType = node->memberExpr.target->type;
    if (node->memberExpr.isRedirect && isOptionalType(targetAstType)) {
        const IrType *valType = target->type->strct.fields[1].type;
        target = makeIrMember(ctx->pool, &node->loc, target, 1, S_val, valType);
    }
    if (mem == NULL) {
        node->codegen = target;
        return;
    }
    u32 index = 0;
    cstring name = NULL;
    if (nodeIs(mem, IntegerLit)) {
        index = (u32)mem->intLiteral.uValue;
    }
    else {
        csAssert0(nodeIs(mem, Identifier));
        name = mem->ident.value;
    }

    if (node->memberExpr.isOptional && isOptionalType(node->type)) {
        const IrType *resIrType = lowerType(ctx, node->type);
        const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
        const IrType *memberValType = getIrOptionalTypeTarget(resIrType);

        cstring tempName =
            makeStringf(ctx->strings, "__opt%u", ctx->tempCounter++);
        IrNode *resVar = makeIrVarDecl(ctx->pool,
                                       &node->loc,
                                       flgTemporary,
                                       tempName,
                                       tempName,
                                       resIrType,
                                       ctx->currentScopeId);
        emitStmt(ctx, resVar);
        IrNode *resVarRef = makeIrVarRef(ctx->pool, &node->loc, resVar);

        IrNode *cond = NULL;
        IrNode *targetObj = NULL;
        if (isOptionalType(targetAstType)) {
            cond =
                makeIrMember(ctx->pool, &node->loc, target, 0, S_ok, boolType);
            targetObj = makeIrMember(ctx->pool,
                                     &node->loc,
                                     target,
                                     1,
                                     S_val,
                                     getIrOptionalTypeTarget(target->type));
        }
        else {
            IrNode *nullConst =
                makeIrNullConst(ctx->pool, &node->loc, target->type);
            cond = makeIrBinary(
                ctx->pool, &node->loc, opNe, target, nullConst, boolType);
            targetObj = target;
        }

        u32 optId = ctx->nextOptId++;
        IrNode *thenBB =
            createBlock(ctx, makeStringf(ctx->strings, "OPT%u_then", optId));
        IrNode *elseBB =
            createBlock(ctx, makeStringf(ctx->strings, "OPT%u_else", optId));
        IrNode *mergeBB =
            createBlock(ctx, makeStringf(ctx->strings, "OPT%u_merge", optId));

        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

        setBlock(ctx, thenBB);
        IrNode *memberVal = makeIrMember(
            ctx->pool, &node->loc, targetObj, index, name, memberValType);
        IrNode *okField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 0, S_ok, boolType);
        IrNode *valField = makeIrMember(
            ctx->pool, &node->loc, resVarRef, 1, S_val, memberValType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     okField,
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valField, memberVal));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, elseBB);
        IrNode *elseOkField =
            makeIrMember(ctx->pool,
                         &node->loc,
                         makeIrVarRef(ctx->pool, &node->loc, resVar),
                         0,
                         S_ok,
                         boolType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     elseOkField,
                     makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, resVar);
        return;
    }

    node->codegen = makeIrMember(
        ctx->pool, &node->loc, target, index, name, lowerType(ctx, node->type));
}

static void visitSliceExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *target = lowerFlattenedOperand(visitor, node->sliceExpr.target);
    const Type *targetAstType = node->sliceExpr.target->type;
    const IrType *sliceIrType = lowerType(ctx, node->type);
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *elemPtrType = sliceIrType->strct.fields[0].type;

    AstNode *rng = node->sliceExpr.range;
    IrNode *startNode = rng->rangeExpr.start
                            ? lowerFlattenedOperand(visitor, rng->rangeExpr.start)
                            : makeIrIntConst(ctx->pool, &node->loc, 0, false, u64Type);

    IrNode *endNode = NULL;
    if (rng->rangeExpr.end) {
        endNode = lowerFlattenedOperand(visitor, rng->rangeExpr.end);
    }
    else {
        if (isArrayType(targetAstType) && !isSliceType(targetAstType)) {
            endNode = makeIrIntConst(ctx->pool, &node->loc, targetAstType->array.len, false, u64Type);
        }
        else {
            endNode = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, target), 1, S_len, u64Type);
        }
    }

    IrNode *ptrVal = NULL;
    if (isSliceType(targetAstType)) {
        IrNode *basePtr = makeIrMember(ctx->pool, &node->loc, target, 0, S_ptr, elemPtrType);
        ptrVal = makeIrBinary(ctx->pool, &node->loc, opAdd, basePtr, startNode, elemPtrType);
    }
    else {
        IrNode *slot = makeIrIndex(ctx->pool, &node->loc, target, startNode, elemPtrType->pointer.pointee);
        ptrVal = makeIrAddrOf(ctx->pool, &node->loc, slot, elemPtrType);
    }

    IrNode *lenVal = NULL;
    if (startNode->tag == irIntConst && endNode->tag == irIntConst && !startNode->intConst.isNegative && !endNode->intConst.isNegative) {
        u64 s = (u64)startNode->intConst.value;
        u64 e = (u64)endNode->intConst.value;
        lenVal = makeIrIntConst(ctx->pool, &node->loc, (e >= s) ? (e - s) : 0, false, u64Type);
    }
    else {
        lenVal = makeIrBinary(ctx->pool, &node->loc, opSub, endNode, cloneIrNode(ctx->pool, startNode), u64Type);
    }

    ptrVal->next = lenVal;
    node->codegen = makeIrStructConst(ctx->pool, &node->loc, ptrVal, 2, sliceIrType);
}

static void visitIndexExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *target = lowerFlattenedOperand(visitor, node->indexExpr.target);
    IrNode *index = lowerFlattenedOperand(visitor, node->indexExpr.index);
    const Type *targetAstType = node->indexExpr.target->type;

    if (isSliceType(targetAstType)) {
        IrNode *ptrTarget = makeIrMember(ctx->pool,
                                         &node->loc,
                                         target,
                                         0,
                                         S_ptr,
                                         target->type->strct.fields[0].type);
        node->codegen = makeIrIndex(ctx->pool,
                                    &node->loc,
                                    ptrTarget,
                                    index,
                                    lowerType(ctx, node->type));
        return;
    }

    node->codegen = makeIrIndex(
        ctx->pool, &node->loc, target, index, lowerType(ctx, node->type));
}

static void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->binaryExpr.op;

    if (op == opLAnd) {
        const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
        IrNode *lhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.lhs);

        cstring condName =
            makeStringf(ctx->strings, "__cond%u", ctx->tempCounter++);
        IrNode *condVar = makeIrVarDecl(ctx->pool,
                                        &node->loc,
                                        flgTemporary,
                                        condName,
                                        condName,
                                        boolType,
                                        ctx->currentScopeId);
        emitStmt(ctx, condVar);

        u32 logicId = ctx->nextLogicId++;
        IrNode *rhsBB =
            createBlock(ctx, makeStringf(ctx->strings, "AND%u_rhs", logicId));
        IrNode *falseBB =
            createBlock(ctx, makeStringf(ctx->strings, "AND%u_false", logicId));
        IrNode *mergeBB =
            createBlock(ctx, makeStringf(ctx->strings, "AND%u_merge", logicId));

        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, lhs, rhsBB, falseBB));

        setBlock(ctx, rhsBB);
        IrNode *rhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.rhs);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, condVar),
                              rhs));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, falseBB);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     makeIrVarRef(ctx->pool, &node->loc, condVar),
                     makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, condVar);
        return;
    }

    if (op == opLOr) {
        const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
        IrNode *lhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.lhs);

        cstring condName =
            makeStringf(ctx->strings, "__cond%u", ctx->tempCounter++);
        IrNode *condVar = makeIrVarDecl(ctx->pool,
                                        &node->loc,
                                        flgTemporary,
                                        condName,
                                        condName,
                                        boolType,
                                        ctx->currentScopeId);
        emitStmt(ctx, condVar);

        u32 logicId = ctx->nextLogicId++;
        IrNode *trueBB =
            createBlock(ctx, makeStringf(ctx->strings, "OR%u_true", logicId));
        IrNode *rhsBB =
            createBlock(ctx, makeStringf(ctx->strings, "OR%u_rhs", logicId));
        IrNode *mergeBB =
            createBlock(ctx, makeStringf(ctx->strings, "OR%u_merge", logicId));

        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, lhs, trueBB, rhsBB));

        setBlock(ctx, trueBB);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     makeIrVarRef(ctx->pool, &node->loc, condVar),
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, rhsBB);
        IrNode *rhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.rhs);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, condVar),
                              rhs));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, condVar);
        return;
    }

    if (op == opCoalesce) {
        IrNode *lhs = lowerFlattenedOperand(visitor, node->binaryExpr.lhs);
        const Type *lhsAstType = node->binaryExpr.lhs->type;
        const IrType *resIrType = lowerType(ctx, node->type);

        cstring tempName =
            makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
        IrNode *resVar = makeIrVarDecl(ctx->pool,
                                       &node->loc,
                                       flgTemporary,
                                       tempName,
                                       tempName,
                                       resIrType,
                                       ctx->currentScopeId);
        emitStmt(ctx, resVar);

        u32 optId = ctx->nextOptId++;
        IrNode *thenBB = createBlock(
            ctx, makeStringf(ctx->strings, "COALESCE%u_then", optId));
        IrNode *elseBB = createBlock(
            ctx, makeStringf(ctx->strings, "COALESCE%u_else", optId));
        IrNode *mergeBB = createBlock(
            ctx, makeStringf(ctx->strings, "COALESCE%u_merge", optId));

        IrNode *cond = NULL;
        IrNode *thenVal = NULL;
        if (isOptionalType(lhsAstType)) {
            cond = makeIrMember(ctx->pool,
                                &node->binaryExpr.lhs->loc,
                                lhs,
                                0,
                                S_ok,
                                getIrPrimitiveType(ctx->types, irTypeBool));
            thenVal = makeIrMember(ctx->pool,
                                   &node->binaryExpr.lhs->loc,
                                   lhs,
                                   1,
                                   S_val,
                                   resIrType);
        }
        else {
            IrNode *nullConst = makeIrNullConst(
                ctx->pool, &node->binaryExpr.lhs->loc, lhs->type);
            cond = makeIrBinary(ctx->pool,
                                &node->binaryExpr.lhs->loc,
                                opNe,
                                lhs,
                                nullConst,
                                getIrPrimitiveType(ctx->types, irTypeBool));
            thenVal = lhs;
        }

        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

        setBlock(ctx, thenBB);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, resVar),
                              thenVal));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, elseBB);
        astVisit(visitor, node->binaryExpr.rhs);
        IrNode *rhsVal = (IrNode *)node->binaryExpr.rhs->codegen;
        if (rhsVal)
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, resVar),
                                  rhsVal));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, resVar);
        return;
    }

    if (op == opIs) {
        const Type *sumType =
            stripPointerOrReferenceOnce(node->binaryExpr.lhs->type, NULL);
        if (isUnionType(sumType)) {
            const Type *targetType = node->binaryExpr.rhs->type;
            if (targetType && typeIs(targetType, Info))
                targetType = targetType->info.target;
            u32 idx = findUnionTypeIndex(sumType, targetType);
            if (idx == UINT32_MAX) {
                node->codegen =
                    makeIrBoolConst(ctx->pool,
                                    &node->loc,
                                    false,
                                    getIrPrimitiveType(ctx->types, irTypeBool));
                return;
            }
            IrNode *lhs = lowerFlattenedOperand(visitor, node->binaryExpr.lhs);
            const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
            IrNode *tagMember =
                makeIrMember(ctx->pool, &node->loc, lhs, 0, "tag", u32Type);
            IrNode *idxConst =
                makeIrIntConst(ctx->pool, &node->loc, idx, false, u32Type);
            node->codegen =
                makeIrBinary(ctx->pool,
                             &node->loc,
                             opEq,
                             tagMember,
                             idxConst,
                             getIrPrimitiveType(ctx->types, irTypeBool));
            return;
        }
    }

    IrNode *lhs = lowerBorrowedOperand(visitor, node->binaryExpr.lhs);
    IrNode *rhs = lowerBorrowedOperand(visitor, node->binaryExpr.rhs);

    if (op == opEq || op == opNe) {
        const Type *lhsAstType = node->binaryExpr.lhs->type;
        const Type *rhsAstType = node->binaryExpr.rhs->type;
        if (isClosureType(lhsAstType) && (rhsAstType == NULL || typeIs(rhsAstType, Null) || nodeIs(node->binaryExpr.rhs, NullLit))) {
            const IrType *fnPtrType = lhs->type && lhs->type->tag == irTypeStruct && lhs->type->strct.count >= 2
                                          ? lhs->type->strct.fields[1].type
                                          : makeIrPointerType(ctx->types, flgNone, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeVoid));
            IrNode *fnMember = makeIrMember(ctx->pool, &node->binaryExpr.lhs->loc, lhs, 1, S_fn, fnPtrType);
            IrNode *nullConst = makeIrNullConst(ctx->pool, &node->binaryExpr.rhs->loc, fnPtrType);
            node->codegen = makeIrBinary(ctx->pool, &node->loc, op, fnMember, nullConst, getIrPrimitiveType(ctx->types, irTypeBool));
            return;
        }
        if (isClosureType(rhsAstType) && (lhsAstType == NULL || typeIs(lhsAstType, Null) || nodeIs(node->binaryExpr.lhs, NullLit))) {
            const IrType *fnPtrType = rhs->type && rhs->type->tag == irTypeStruct && rhs->type->strct.count >= 2
                                          ? rhs->type->strct.fields[1].type
                                          : makeIrPointerType(ctx->types, flgNone, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeVoid));
            IrNode *fnMember = makeIrMember(ctx->pool, &node->binaryExpr.rhs->loc, rhs, 1, S_fn, fnPtrType);
            IrNode *nullConst = makeIrNullConst(ctx->pool, &node->binaryExpr.lhs->loc, fnPtrType);
            node->codegen = makeIrBinary(ctx->pool, &node->loc, op, nullConst, fnMember, getIrPrimitiveType(ctx->types, irTypeBool));
            return;
        }
    }

    if (node->binaryExpr.lhs->type &&
        isReferenceType(node->binaryExpr.lhs->type) &&
        !isClassType(stripReference(node->binaryExpr.lhs->type))) {
        const IrType *valType =
            lowerType(ctx, stripReference(node->binaryExpr.lhs->type));
        lhs = makeIrDeref(ctx->pool, &node->binaryExpr.lhs->loc, lhs, valType);
    }
    if (node->binaryExpr.rhs->type &&
        isReferenceType(node->binaryExpr.rhs->type) &&
        !isClassType(stripReference(node->binaryExpr.rhs->type))) {
        const IrType *valType =
            lowerType(ctx, stripReference(node->binaryExpr.rhs->type));
        rhs = makeIrDeref(ctx->pool, &node->binaryExpr.rhs->loc, rhs, valType);
    }
    node->codegen = makeIrBinary(
        ctx->pool, &node->loc, op, lhs, rhs, lowerType(ctx, node->type));
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *operand = lowerFlattenedOperand(visitor, node->unaryExpr.operand);
    const IrType *irType = lowerType(ctx, node->type);
    if (node->unaryExpr.op == opPtrof || node->unaryExpr.op == opRefof) {
        const Type *opType = node->unaryExpr.operand->type;
        bool isRefOf = node->unaryExpr.op == opRefof;
        bool operandIsRef =
            isReferenceType(opType) ||
            (operand && operand->type &&
             operand->type->category == irTypeCatReference);
        // `&array` and `&class` are the value itself, `ptrof` takes the address
        bool isArrayRef = isRefOf && operand && operand->type &&
                          operand->type->tag == irTypeArray;
        if (operandIsRef || isArrayRef || (isRefOf && isClassType(opType))) {
            node->codegen = (node->unaryExpr.op == opRefof)
                                ? makeIrCast(ctx->pool, &node->loc, operand, irType)
                                : operand;
        }
        else
            node->codegen =
                makeIrAddrOf(ctx->pool, &node->loc, operand, irType);
    }
    else if (node->unaryExpr.op == opMove)
        node->codegen = makeIrMove(ctx->pool, &node->loc, operand);
    else if (node->unaryExpr.op == opDelete) {
        node->codegen = makeIrDrop(ctx->pool, &node->loc, operand);
    }
    else if (node->unaryExpr.op == opDeref) {
        const Type *opType = node->unaryExpr.operand->type;
        if (isOptionalType(opType))
            node->codegen =
                makeIrMember(ctx->pool, &node->loc, operand, 1, S_val, irType);
        else
            node->codegen = makeIrDeref(ctx->pool, &node->loc, operand, irType);
    }
    else if (node->unaryExpr.op == opNot) {
        const Type *opType = node->unaryExpr.operand->type;
        if (isOptionalType(opType)) {
            IrNode *okMember =
                makeIrMember(ctx->pool,
                             &node->loc,
                             operand,
                             0,
                             S_ok,
                             getIrPrimitiveType(ctx->types, irTypeBool));
            node->codegen =
                makeIrUnary(ctx->pool, &node->loc, opNot, okMember, irType);
        }
        else {
            if (opType && isReferenceType(opType) &&
                !isClassType(stripReference(opType))) {
                const IrType *valType = lowerType(ctx, stripReference(opType));
                operand = makeIrDeref(
                    ctx->pool, &node->unaryExpr.operand->loc, operand, valType);
            }
            node->codegen = makeIrUnary(
                ctx->pool, &node->loc, node->unaryExpr.op, operand, irType);
        }
    }
    else {
        const Type *opType = node->unaryExpr.operand->type;
        if (opType && isReferenceType(opType) &&
            !isClassType(stripReference(opType))) {
            const IrType *valType = lowerType(ctx, stripReference(opType));
            operand = makeIrDeref(
                ctx->pool, &node->unaryExpr.operand->loc, operand, valType);
        }
        node->codegen = makeIrUnary(
            ctx->pool, &node->loc, node->unaryExpr.op, operand, irType);
    }
}

static void visitPointerOf(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *operand = lowerFlattenedOperand(visitor, node->unaryExpr.operand);
    const Type *opType = node->unaryExpr.operand->type;
    const IrType *irType = lowerType(ctx, node->type);
    bool isRefOf = nodeIs(node, ReferenceOf);
    bool operandIsRef =
        isReferenceType(opType) ||
        (operand && operand->type &&
         operand->type->category == irTypeCatReference);
    // `&array` and `&class` are the value itself, `ptrof` takes the address
    bool isArrayRef = isRefOf && operand && operand->type &&
                      operand->type->tag == irTypeArray;
    if (operandIsRef || isArrayRef || (isRefOf && isClassType(opType))) {
        node->codegen = isRefOf
                            ? makeIrCast(ctx->pool, &node->loc, operand, irType)
                            : operand;
        return;
    }
    node->codegen = makeIrAddrOf(
        ctx->pool, &node->loc, operand, irType);
}

static void lowerTypeCoercion(AstVisitor *visitor,
                              AstNode *node,
                              AstNode *subExpr)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *targetType = node->type;
    const Type *srcType = subExpr->type;

    if (isOptionalType(targetType)) {
        const IrType *optIrType = lowerType(ctx, targetType);
        if (subExpr == NULL || nodeIs(subExpr, NullLit) || (srcType && typeIs(srcType, Null))) {
            node->codegen = makeIrOptionalValue(ctx->pool, &node->loc, NULL, optIrType);
            return;
        }
        if (!isOptionalType(srcType)) {
            IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
            node->codegen = makeIrOptionalValue(ctx->pool, &node->loc, expr, optIrType);
            return;
        }
    }
    else if (isSliceType(targetType) && isArrayType(srcType)) {
        const IrType *sliceIrType = lowerType(ctx, targetType);
        cstring tempName =
            makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
        IrNode *sliceTemp = makeIrVarDecl(ctx->pool,
                                          &node->loc,
                                          flgNone,
                                          tempName,
                                          tempName,
                                          sliceIrType,
                                          ctx->currentScopeId);
        emitStmt(ctx, sliceTemp);
        lowerSliceIntoTarget(ctx,
                             visitor,
                             subExpr,
                             makeIrVarRef(ctx->pool, &node->loc, sliceTemp));
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, sliceTemp);
        return;
    }

    if (isOptionalType(srcType) && isBooleanType(targetType)) {
        IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
        node->codegen =
            makeIrMember(ctx->pool,
                         &node->loc,
                         expr,
                         0,
                         S_ok,
                         getIrPrimitiveType(ctx->types, irTypeBool));
        return;
    }

    if (srcType && isUnionType(srcType)) {
        u32 idx = findUnionTypeIndex(srcType, stripReference(targetType));
        if (idx != UINT32_MAX) {
            IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
            const IrType *unionIrType = expr->type;
            if (unionIrType && unionIrType->tag == irTypeStruct &&
                unionIrType->strct.count == 2) {
                if (!hasFlag(node, BitUnionCast)) {
                    const IrType *u32Type =
                        getIrPrimitiveType(ctx->types, irTypeU32);
                    const IrType *boolType =
                        getIrPrimitiveType(ctx->types, irTypeBool);
                    const IrType *u64Type =
                        getIrPrimitiveType(ctx->types, irTypeU64);
                    const IrType *strType = makeIrPointerType(
                        ctx->types,
                        flgNone, irTypeCatNone,
                        getIrPrimitiveType(ctx->types, irTypeChar));
                    const IrType *voidType =
                        getIrPrimitiveType(ctx->types, irTypeVoid);

                    IrNode *tagMember = makeIrMember(
                        ctx->pool, &node->loc, expr, 0, S_tag, u32Type);
                    IrNode *idxConst = makeIrIntConst(
                        ctx->pool, &node->loc, idx, false, u32Type);
                    IrNode *cond = makeIrBinary(ctx->pool,
                                                &node->loc,
                                                opEq,
                                                tagMember,
                                                idxConst,
                                                boolType);

                    IrNode *file = makeIrStringConst(ctx->pool,
                                                     &node->loc,
                                                     node->loc.fileName ?: "",
                                                     strType);
                    IrNode *line = makeIrIntConst(ctx->pool,
                                                  &node->loc,
                                                  node->loc.begin.row,
                                                  false,
                                                  u64Type);
                    IrNode *col = makeIrIntConst(ctx->pool,
                                                 &node->loc,
                                                 node->loc.begin.col,
                                                 false,
                                                 u64Type);
                    cond->next = file;
                    file->next = line;
                    line->next = col;

                    const IrType *params[] = {
                        boolType, strType, u64Type, u64Type};
                    const IrType *assertFuncType = makeIrFuncType(
                        ctx->types, flgNone, params, 4, voidType, false);
                    IrNode *callee = makeIrGlobalRef(ctx->pool,
                                                     &node->loc,
                                                     S___cxy_assert,
                                                     NULL,
                                                     assertFuncType);
                    IrNode *assertCall = makeIrCall(
                        ctx->pool, &node->loc, callee, cond, 4, voidType);
                    emitStmt(ctx, assertCall);
                }

                const IrType *payloadUnionType =
                    unionIrType->strct.fields[1].type;
                IrNode *valMember = makeIrMember(
                    ctx->pool, &node->loc, expr, 1, S_val, payloadUnionType);
                cstring fieldName = makeStringf(ctx->strings, "_%u", idx);
                const IrType *memberIrType =
                    (payloadUnionType && idx < payloadUnionType->strct.count)
                        ? payloadUnionType->strct.fields[idx].type
                        : lowerType(ctx, targetType);
                node->codegen = makeIrMember(ctx->pool,
                                             &node->loc,
                                             valMember,
                                             idx,
                                             fieldName,
                                             memberIrType);
                return;
            }
        }
    }

    IrNode *expr = lowerFlattenedOperand(visitor, subExpr);
    node->codegen =
        makeIrCast(ctx->pool, &node->loc, expr, lowerType(ctx, node->type));
}

static void visitCastExpr(AstVisitor *visitor, AstNode *node)
{
    lowerTypeCoercion(visitor, node, node->castExpr.expr);
}

static void visitTypedExpr(AstVisitor *visitor, AstNode *node)
{
    lowerTypeCoercion(visitor, node, node->typedExpr.expr);
}

static const Type *getCalleeType(const AstNode *calleeNode)
{
    const Type *calleeType = resolveUnThisUnwrapType(calleeNode->type);
    if (calleeType == NULL) {
        const AstNode *calleeDecl =
            nodeIs(calleeNode, Identifier)
                ? calleeNode->ident.resolvesTo
                : (calleeNode->path.elements
                       ? calleeNode->path.elements->pathElement.resolvesTo
                       : NULL);
        if (calleeDecl && calleeDecl->type)
            calleeType = resolveUnThisUnwrapType(calleeDecl->type);
    }
    if (typeIs(calleeType, Pointer))
        calleeType = resolveUnThisUnwrapType(calleeType->pointer.pointed);
    return calleeType;
}

static IrNode *lowerCallArguments(LowerContext *ctx,
                                  AstVisitor *visitor,
                                  AstNode *args,
                                  const Type *calleeType,
                                  u64 startParamIdx,
                                  IrNode *initialArg,
                                  u64 *outCount)
{
    IrNode *argsFirst = initialArg;
    IrNode *argsLast = initialArg;
    u64 count = initialArg ? 1 : 0;
    u64 argIdx = startParamIdx;
    if (isClosureType(calleeType)) {
        calleeType = calleeType->closure.fn;
    }
    AstNode *func = calleeType->func.decl;
    AstNode *params = nodeIs(func, FuncDecl) ? func->funcDecl.signature->params
                                             : func->funcType.params,
            *param = params;
    for (AstNode *arg = args; arg; arg = arg->next, count++) {
        // Past the named params (or at the varargs marker): lower as-is
        bool isVarArg = param == NULL || hasFlag(param, Variadic);
        const Type *paramAstType = isVarArg ? NULL : param->type;
        if (!isVarArg)
            param = param->next;
        IrNode *loweredArg = NULL;
        if (isSliceType(paramAstType) && isArrayType(arg->type) &&
            !isSliceType(arg->type)) {
            const IrType *sliceIrType = lowerType(ctx, paramAstType);
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *sliceTemp = makeIrVarDecl(ctx->pool,
                                              &arg->loc,
                                              flgTemporary,
                                              tempName,
                                              tempName,
                                              sliceIrType,
                                              ctx->currentScopeId);
            emitStmt(ctx, sliceTemp);
            lowerSliceIntoTarget(ctx,
                                 visitor,
                                 arg,
                                 makeIrVarRef(ctx->pool, &arg->loc, sliceTemp));
            loweredArg = makeIrVarRef(ctx->pool, &arg->loc, sliceTemp);
        }
        else {
            loweredArg = lowerFlattenedOperand(visitor, arg);
        }

        if (argsLast) {
            argsLast->next = loweredArg;
            argsLast = loweredArg;
        }
        else {
            argsFirst = argsLast = loweredArg;
        }
    }

    if (outCount)
        *outCount = count;
    return argsFirst;
}

static void lowerOptionalCallExpr(AstVisitor *visitor,
                                  AstNode *node,
                                  const Type *calleeType)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    AstNode *targetAstNode = node->callExpr.args;
    IrNode *target = lowerFlattenedOperand(visitor, targetAstNode);
    const Type *targetAstType = targetAstNode->type;
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
    const IrType *resIrType = lowerType(ctx, node->type);

    IrNode *cond = NULL;
    IrNode *thisVal = NULL;
    if (isOptionalType(targetAstType)) {
        cond = makeIrMember(ctx->pool, &node->loc, target, 0, S_ok, boolType);
        thisVal = makeIrMember(ctx->pool,
                               &node->loc,
                               target,
                               1,
                               S_val,
                               getIrOptionalTypeTarget(target->type));
    }
    else {
        IrNode *nullConst =
            makeIrNullConst(ctx->pool, &node->loc, target->type);
        cond = makeIrBinary(
            ctx->pool, &node->loc, opNe, target, nullConst, boolType);
        thisVal = target;
    }

    cstring tempName = makeStringf(ctx->strings, "__opt%u", ctx->tempCounter++);
    IrNode *resVar = makeIrVarDecl(ctx->pool,
                                   &node->loc,
                                   flgTemporary,
                                   tempName,
                                   tempName,
                                   resIrType,
                                   ctx->currentScopeId);
    emitStmt(ctx, resVar);
    IrNode *resVarRef = makeIrVarRef(ctx->pool, &node->loc, resVar);

    u32 optId = ctx->nextOptId++;
    IrNode *thenBB =
        createBlock(ctx, makeStringf(ctx->strings, "OPTCALL%u_then", optId));
    IrNode *elseBB =
        createBlock(ctx, makeStringf(ctx->strings, "OPTCALL%u_else", optId));
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "OPTCALL%u_merge", optId));

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->callExpr.callee);
    IrNode *callee = (IrNode *)node->callExpr.callee->codegen;

    u64 argsCount = 0;
    IrNode *argsFirst = lowerCallArguments(ctx,
                                           visitor,
                                           node->callExpr.args->next,
                                           calleeType,
                                           1,
                                           thisVal,
                                           &argsCount);

    const IrType *valType = getIrOptionalTypeTarget(resIrType)
                                ?: getIrPrimitiveType(ctx->types, irTypeVoid);
    IrNode *callNode = makeIrCall(
        ctx->pool, &node->loc, callee, argsFirst, argsCount, valType);

    if (valType && valType->tag != irTypeVoid) {
        IrNode *okField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 0, S_ok, boolType);
        IrNode *valField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 1, S_val, valType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     okField,
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valField, callNode));
    }
    else {
        emitStmt(ctx, callNode);
        IrNode *okField =
            makeIrMember(ctx->pool, &node->loc, resVarRef, 0, S_ok, boolType);
        emitStmt(ctx,
                 makeIrAssign(
                     ctx->pool,
                     &node->loc,
                     okField,
                     makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
    }
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    IrNode *elseOkField =
        makeIrMember(ctx->pool,
                     &node->loc,
                     makeIrVarRef(ctx->pool, &node->loc, resVar),
                     0,
                     S_ok,
                     boolType);
    emitStmt(
        ctx,
        makeIrAssign(ctx->pool,
                     &node->loc,
                     elseOkField,
                     makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, resVar);
}

static IrNode *emitResultCheckAndUnwrap(LowerContext *ctx,
                                         AstNode *node,
                                         IrNode *resVar,
                                         const Type *resAstType)
{
    const IrType *resIrType = resVar->type;
    cstring errName = makeStringf(ctx->strings, "call_err_%u", ctx->nextIfId++);
    IrNode *errBB = createBlock(ctx, errName);
    cstring contName = makeStringf(ctx->strings, "call_cont_%u", ctx->nextIfId);
    IrNode *contBB = createBlock(ctx, contName);

    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, resVar), 0, S_tag, u32Type);
    IrNode *cond = makeIrBinary(
        ctx->pool,
        &node->loc,
        opEq,
        tagVal,
        makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type),
        getIrPrimitiveType(ctx->types, irTypeBool));
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, errBB, contBB));

    setBlock(ctx, errBB);
    if (ctx->funcRetVar && isResultType(ctx->funcRetAstType)) {
        IrNode *retVarRef = makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        IrNode *tagSlot = makeIrMember(
            ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              tagSlot,
                              makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));

        IrNode *resVarRef = makeIrVarRef(ctx->pool, &node->loc, resVar);
        const IrType *resValFieldType = resIrType->strct.fields[1].type;
        IrNode *resValMember = makeIrMember(
            ctx->pool, &node->loc, resVarRef, 1, S_val, resValFieldType);
        IrNode *errVal = resValMember;
        if (resValFieldType->tag == irTypeUntaggedUnion) {
            const Type *baseExType = findBuiltinType(S_Exception);
            const IrType *exIrType =
                baseExType ? lowerType(ctx, baseExType)
                           : getIrPrimitiveType(ctx->types, irTypeOpaque);
            errVal = makeIrMember(
                ctx->pool, &node->loc, resValMember, 1, NULL, exIrType);
        }

        emitExceptionTracePush(ctx, &node->loc, errVal);

        const IrType *retIrType = ctx->funcRetVar->type;
        const IrType *retValFieldType = retIrType->strct.fields[1].type;
        IrNode *retValMember = makeIrMember(
            ctx->pool, &node->loc, retVarRef, 1, S_val, retValFieldType);
        IrNode *assignStmt = NULL;
        if (retValFieldType->tag == irTypeUntaggedUnion) {
            const Type *baseExType = findBuiltinType(S_Exception);
            const IrType *exIrType =
                baseExType ? lowerType(ctx, baseExType)
                           : getIrPrimitiveType(ctx->types, irTypeOpaque);
            IrNode *errSlot = makeIrMember(
                ctx->pool, &node->loc, retValMember, 1, NULL, exIrType);
            assignStmt = makeIrAssign(ctx->pool, &node->loc, errSlot, errVal);
        }
        else {
            assignStmt = makeIrAssign(ctx->pool, &node->loc, retValMember, errVal);
        }
        assignStmt->flags |= flgUnmanaged;
        emitStmt(ctx, assignStmt);

        IrNode *resTagSlot = makeIrMember(
            ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, resVar), 0, S_tag, u32Type);
        IrNode *defuseStmt = makeIrAssign(
            ctx->pool, &node->loc, resTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);

        IrNode *targetBB = getOrCreateScopeReturnBlock(
            ctx, ctx->currentScopeId, &node->loc);
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, targetBB));
    }
    else {
        emitStmt(ctx, makeIrUnreachable(ctx->pool, &node->loc));
    }

    setBlock(ctx, contBB);
    if (isVoidResultType(resAstType)) {
        return NULL;
    }

    const Type *targetType = getResultTargetType(resAstType);
    const IrType *irTargetType = lowerType(ctx, targetType);
    IrNode *valMember = makeIrMember(
        ctx->pool,
        &node->loc,
        makeIrVarRef(ctx->pool, &node->loc, resVar),
        1,
        S_val,
        resIrType->strct.fields[1].type);
    IrNode *unwrapped = valMember;
    if (valMember->type && valMember->type->tag == irTypeUntaggedUnion) {
        unwrapped = makeIrMember(
            ctx->pool, &node->loc, valMember, 0, NULL, irTargetType);
    }
    return unwrapped;
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const AstNode *calleeNode = node->callExpr.callee;
    bool isOptCall =
        node->callExpr.isOptional ||
        (nodeIs(calleeNode, MemberExpr) && calleeNode->memberExpr.isOptional);

    const Type *calleeType = getCalleeType(calleeNode);
    if (isOptCall && node->callExpr.args) {
        lowerOptionalCallExpr(visitor, node, calleeType);
        return;
    }

    astVisit(visitor, node->callExpr.callee);
    IrNode *callee = (IrNode *)node->callExpr.callee->codegen;
    u64 argsCount = 0;
    IrNode *argsFirst = lowerCallArguments(
        ctx, visitor, node->callExpr.args, calleeType, 0, NULL, &argsCount);

    if (calleeType && isClosureType(calleeType)) {
        // the callee is read twice (`.fn` and `.env`), simplify hoists any
        // expression that is not already a value that can be read again
        csAssert0(callee->tag != irClosureValue && callee->tag != irCall);
        const IrType *voidPtrType = makeIrPointerType(
            ctx->types,
            flgNone, irTypeCatNone,
            getIrPrimitiveType(ctx->types, irTypeVoid));
        const IrType *fnPtrType = lowerClosureFnPtrType(ctx, calleeType);
        IrNode *fnCallee = makeIrMember(
            ctx->pool, &node->loc, cloneIrNode(ctx->pool, callee), 1, S_fn, fnPtrType);
        IrNode *envArg =
            makeIrMember(ctx->pool, &node->loc, callee, 0, S_env, voidPtrType);
        envArg->next = argsFirst;
        argsFirst = envArg;
        argsCount++;
        callee = fnCallee;
    }

    const Type *calleeRetAstType =
        calleeType
            ? (isClosureType(calleeType) ? calleeType->closure.fn->func.retType
                                         : calleeType->func.retType)
            : node->type;
    calleeRetAstType = resolveUnThisUnwrapType(calleeRetAstType);
    const IrType *callRetIrType = lowerType(ctx, calleeRetAstType);

    IrNode *callNode = makeIrCall(ctx->pool,
                                  &node->loc,
                                  callee,
                                  argsFirst,
                                  argsCount,
                                  callRetIrType);

    if (!ctx->explicitCatch && calleeRetAstType && isResultType(calleeRetAstType)) {
        const IrType *resIrType = callRetIrType;
        cstring tempName =
            makeStringf(ctx->strings, "__call_res_%u", ctx->tempCounter++);
        Flags flags = flgTemporary;
        if (isIrTypeDestructible(resIrType))
            flags |= flgReferenceMembers;
        IrNode *resVar = makeIrVarDecl(ctx->pool,
                                       &node->loc,
                                       flags,
                                       tempName,
                                       tempName,
                                       resIrType,
                                       ctx->currentScopeId);
        emitStmt(ctx, resVar);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, resVar),
                              callNode));

        node->codegen =
            emitResultCheckAndUnwrap(ctx, node, resVar, calleeRetAstType);
    }
    else {
        node->codegen = callNode;
    }
}

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    astVisit(visitor, node->ternaryExpr.cond);
    IrNode *cond = (IrNode *)node->ternaryExpr.cond->codegen;

    cstring tempName =
        makeStringf(ctx->strings, "__ternary_%u", ctx->tempCounter++);
    Flags flags = flgTemporary;
    if (isIrTypeDestructible(irType))
        flags |= flgReferenceMembers;
    IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    tempName,
                                    tempName,
                                    irType,
                                    ctx->currentScopeId);
    emitStmt(ctx, tempVar);

    u32 ternaryId = ctx->nextTernaryId++;
    IrNode *thenBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_then", ternaryId));
    IrNode *elseBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_else", ternaryId));
    IrNode *mergeBB = createBlock(
        ctx, makeStringf(ctx->strings, "TERNARY%u_merge", ternaryId));

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ternaryExpr.body);
    IrNode *thenVal = (IrNode *)node->ternaryExpr.body->codegen;
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          thenVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    astVisit(visitor, node->ternaryExpr.otherwise);
    IrNode *elseVal = (IrNode *)node->ternaryExpr.otherwise->codegen;
    emitStmt(ctx,
             makeIrAssign(ctx->pool,
                          &node->loc,
                          makeIrVarRef(ctx->pool, &node->loc, tempVar),
                          elseVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *arrType = lowerType(ctx, node->type);
    IrNode *elemsFirst = NULL, *elemsLast = NULL;
    u64 count = 0;
    for (AstNode *elem = node->arrayExpr.elements; elem;
         elem = elem->next, count++) {
        IrNode *elemVal = lowerCompositeElement(visitor, elem);
        if (elemsLast) {
            elemsLast->next = elemVal;
            elemsLast = elemVal;
        }
        else {
            elemsFirst = elemsLast = elemVal;
        }
    }
    node->codegen =
        makeIrArrayConst(ctx->pool, &node->loc, elemsFirst, count, arrType);
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *tupType = lowerType(ctx, node->type);
    const Type *tupAstType = resolveUnThisUnwrapType(node->type);
    IrNode *elemsFirst = NULL, *elemsLast = NULL;
    u64 count = 0;
    for (AstNode *elem = node->tupleExpr.elements; elem;
         elem = elem->next, count++) {
        IrNode *elemVal = lowerCompositeElement(visitor, elem);
        // `null` or a plain `T` stored in a `T?` member becomes an optional
        const Type *member = typeIs(tupAstType, Tuple) &&
                                     count < tupAstType->tuple.count
                                 ? tupAstType->tuple.members[count]
                                 : NULL;
        if (member && elemVal && isOptionalType(member) &&
            (nodeIs(elem, NullLit) || !isOptionalType(elem->type))) {
            elemVal = makeIrOptionalValue(
                ctx->pool,
                &elem->loc,
                nodeIs(elem, NullLit) ? NULL : elemVal,
                lowerType(ctx, member));
        }
        // see visitStructExpr, hoisted so the memory pass sees the value's move
        if (elemVal && elemVal->tag == irOptionalValue &&
            elemVal->optionalValue.hasValue && ctx->currentFunc &&
            isIrTypeDestructible(elemVal->type))
            elemVal = flattenIrValue(ctx, elem, elemVal, false);
        if (elemsLast) {
            elemsLast->next = elemVal;
            elemsLast = elemVal;
        }
        else {
            elemsFirst = elemsLast = elemVal;
        }
    }
    node->codegen =
        makeIrStructConst(ctx->pool, &node->loc, elemsFirst, count, tupType);
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *strctType = lowerType(ctx, node->type);
    IrNode *fieldsFirst = NULL, *fieldsLast = NULL;
    u64 count = 0;
    for (AstNode *f = node->structExpr.fields; f; f = f->next) {
        IrNode *fVal = lowerCompositeElement(visitor, f->fieldExpr.value);
        cstring name = f->fieldExpr.name;
        u32 index = UINT32_MAX;
        if (f->fieldExpr.structField && nodeIs(f->fieldExpr.structField, FieldDecl)) {
            index = f->fieldExpr.structField->structField.index;
            if (name == NULL)
                name = f->fieldExpr.structField->structField.name;
        }
        count++;
        // `T` assigned to a `T?` field is wrapped into the optional here
        const Type *fieldAstType =
            f->type ? resolveUnThisUnwrapType(f->type) : NULL;
        const Type *valueAstType = f->fieldExpr.value->type;
        if (fVal && fieldAstType && isOptionalType(fieldAstType) &&
            !nodeIs(f->fieldExpr.value, NullLit) &&
            !(valueAstType && isOptionalType(valueAstType))) {
            fVal = makeIrOptionalValue(
                ctx->pool, &f->loc, fVal, lowerType(ctx, fieldAstType));
        }
        // an optional holding an owned value gets its own statement, so the
        // memory pass sees the move or copy of the value it wraps
        if (fVal && fVal->tag == irOptionalValue &&
            fVal->optionalValue.hasValue && ctx->currentFunc &&
            isIrTypeDestructible(fVal->type))
            fVal = flattenIrValue(ctx, f->fieldExpr.value, fVal, false);
        const IrType *fType = fVal ? fVal->type : NULL;
        IrNode *fieldNode = makeIrFieldConst(ctx->pool, &f->loc, name, index, fVal, fType);
        if (fieldsLast) {
            fieldsLast->next = fieldNode;
            fieldsLast = fieldNode;
        }
        else {
            fieldsFirst = fieldsLast = fieldNode;
        }
    }
    node->codegen =
        makeIrStructConst(ctx->pool, &node->loc, fieldsFirst, count, strctType);
}

static void visitClosureValue(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *closureType =
        lowerType(ctx, node->closureValue.canonicalType ?: node->type);

    IrNode *envVal = NULL;
    IrNode *dtorVal = NULL;
    if (node->closureValue.env && !nodeIs(node->closureValue.env, NullLit)) {
        IrNode *envStruct =
            lowerFlattenedOperand(visitor, node->closureValue.env);

        if (node->type && node->type->closure.isEscaping) {
            const IrType *envPtrType =
                makeIrPointerType(ctx->types, flgNone, irTypeCatNone, envStruct->type);
            IrNode *allocNode =
                makeIrAlloc(ctx->pool, &node->loc, envStruct->type);
            cstring tempName =
                makeStringf(ctx->strings, "__env%u", ctx->tempCounter++);
            IrNode *envVar = makeIrVarDecl(ctx->pool,
                                           &node->loc,
                                           flgTemporary,
                                           tempName,
                                           tempName,
                                           envPtrType,
                                           ctx->currentScopeId);
            emitStmt(ctx, envVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, envVar),
                                  allocNode));
            emitStmt(
                ctx,
                makeIrAssign(
                    ctx->pool,
                    &node->loc,
                    makeIrDeref(ctx->pool,
                                &node->loc,
                                makeIrVarRef(ctx->pool, &node->loc, envVar),
                                envStruct->type),
                    envStruct));
            envVal = makeIrVarRef(ctx->pool, &node->loc, envVar);
        }
        else {
            const IrType *envStructType = lowerType(ctx, node->closureValue.env->type);
            cstring envVarName =
                makeStringf(ctx->strings, "__env%u", ctx->tempCounter++);
            Flags envFlags = flgNone;
            if (isIrTypeDestructible(envStructType))
                envFlags |= flgReferenceMembers;
            IrNode *envVar = makeIrVarDecl(ctx->pool,
                                           &node->loc,
                                           envFlags,
                                           envVarName,
                                           envVarName,
                                           envStructType,
                                           ctx->currentScopeId);
            emitStmt(ctx, envVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, envVar),
                                  envStruct));
            envVal = makeIrAddrOf(
                ctx->pool,
                &node->loc,
                makeIrVarRef(ctx->pool, &node->loc, envVar),
                makeIrPointerType(ctx->types, flgNone, irTypeCatNone, envStructType));
        }
    }

    IrNode *fnVal = lowerFlattenedOperand(visitor, node->closureValue.func);

    node->codegen = makeIrClosureValue(
        ctx->pool, &node->loc, envVal, fnVal, dtorVal, closureType);
}

static void visitUnionValueExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *unionIrType = lowerType(ctx, node->type);
    IrNode *innerVal = lowerFlattenedOperand(visitor, node->unionValue.value);
    node->codegen = makeIrUnionValue(
        ctx->pool, &node->loc, node->unionValue.idx, innerVal, unionIrType);
}

static void visitBackendCall(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    BackendFuncId func = node->backendCallExpr.func;
    AstNode *args = node->backendCallExpr.args;

    switch (func) {
    case bfiMemAlloc: {
        AstNode *typeArg = args;
        const Type *allocType = typeArg->type;
        if (typeIs(allocType, Pointer))
            allocType = allocType->pointer.pointed;
        const IrType *elemIrType = lowerType(ctx, allocType);
        node->codegen = makeIrAlloc(ctx->pool, &node->loc, elemIrType);
        break;
    }
    case bfiZeromem: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrZeromem(ctx->pool, &node->loc, target);
        break;
    }
    case bfiSizeOf: {
        const IrType *targetType = lowerType(ctx, args->type);
        node->codegen = makeIrSizeof(ctx->pool,
                                     &node->loc,
                                     targetType,
                                     getIrPrimitiveType(ctx->types, irTypeU64));
        break;
    }
    case bfiLen: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
        const Type *argAstType = stripReference(resolveUnThisUnwrapType(args->type));
        if (isSliceType(argAstType)) {
            node->codegen = makeIrMember(
                ctx->pool, &node->loc, target, 1, S_len, u64Type);
        }
        else if (isArrayType(argAstType)) {
            node->codegen = makeIrIntConst(
                ctx->pool, &node->loc, argAstType->array.len, false, u64Type);
        }
        else {
            const IrType *charPtrType = makeIrPointerType(
                ctx->types, flgConst, irTypeCatNone, getIrPrimitiveType(ctx->types, irTypeChar));
            const IrType *fnType = makeIrFuncType(
                ctx->types, flgNone, (const IrType *[]){charPtrType}, 1, u64Type, false);
            IrNode *fn = makeIrGlobalRef(ctx->pool, &node->loc, "strlen", NULL, fnType);
            node->codegen = makeIrCall(ctx->pool, &node->loc, fn, target, 1, u64Type);
        }
        break;
    }
    case bfiSlice: {
        IrNode *ptrArg = lowerFlattenedOperand(visitor, args);
        IrNode *lenArg = lowerFlattenedOperand(visitor, args->next);
        const IrType *sliceType = lowerType(ctx, node->type);
        ptrArg->next = lenArg;
        node->codegen = makeIrStructConst(ctx->pool, &node->loc, ptrArg, 2, sliceType);
        break;
    }
    case bfiAlloca: {
        const Type *allocType = args ? args->type : NULL;
        if (allocType == NULL && node->type) {
            allocType = isPointerType(node->type) ? node->type->pointer.pointed : node->type;
        }
        else if (allocType && isPointerType(allocType)) {
            allocType = allocType->pointer.pointed;
        }
        const IrType *elemType = lowerType(ctx, allocType);
        IrNode *count =
            (args && args->next) ? lowerFlattenedOperand(visitor, args->next) : NULL;
        const IrType *ptrType =
            node->type ? lowerType(ctx, node->type)
                       : makeIrPointerType(ctx->types, flgNone, irTypeCatNone, elemType);
        node->codegen = makeIrAlloca(ctx->pool, &node->loc, elemType, count, ptrType);
        break;
    }
    case bfiAtomicLoad: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *order = lowerFlattenedOperand(visitor, args->next);
        ptr->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn =
            makeIrGlobalRef(ctx->pool, &node->loc, "__atomic_load_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 2, retType);
        break;
    }
    case bfiAtomicStore: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *val = lowerFlattenedOperand(visitor, args->next);
        IrNode *order = lowerFlattenedOperand(visitor, args->next->next);
        ptr->next = val;
        val->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn =
            makeIrGlobalRef(ctx->pool, &node->loc, "__atomic_store_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 3, retType);
        break;
    }
    case bfiAtomicExchange: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *val = lowerFlattenedOperand(visitor, args->next);
        IrNode *order = lowerFlattenedOperand(visitor, args->next->next);
        ptr->next = val;
        val->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_exchange_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 3, retType);
        break;
    }
    case bfiAtomicCompareExchange: {
        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *expected = lowerFlattenedOperand(visitor, args->next);
        IrNode *desired = lowerFlattenedOperand(visitor, args->next->next);
        IrNode *weak = lowerFlattenedOperand(visitor, args->next->next->next);
        IrNode *succOrder =
            lowerFlattenedOperand(visitor, args->next->next->next->next);
        IrNode *failOrder =
            lowerFlattenedOperand(visitor, args->next->next->next->next->next);
        ptr->next = expected;
        expected->next = desired;
        desired->next = weak;
        weak->next = succOrder;
        succOrder->next = failOrder;
        failOrder->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_compare_exchange_n", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 6, retType);
        break;
    }
    case bfiAtomicFetchAdd:
    case bfiAtomicFetchSub:
    case bfiAtomicFetchOr:
    case bfiAtomicFetchAnd:
    case bfiAtomicFetchXor: {
        cstring fnName = "__atomic_fetch_add";
        if (func == bfiAtomicFetchSub)
            fnName = "__atomic_fetch_sub";
        else if (func == bfiAtomicFetchOr)
            fnName = "__atomic_fetch_or";
        else if (func == bfiAtomicFetchAnd)
            fnName = "__atomic_fetch_and";
        else if (func == bfiAtomicFetchXor)
            fnName = "__atomic_fetch_xor";

        IrNode *ptr = lowerFlattenedOperand(visitor, args);
        IrNode *val = lowerFlattenedOperand(visitor, args->next);
        IrNode *order = lowerFlattenedOperand(visitor, args->next->next);
        ptr->next = val;
        val->next = order;
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(ctx->pool, &node->loc, fnName, NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, ptr, 3, retType);
        break;
    }
    case bfiAtomicThreadFence: {
        IrNode *order = lowerFlattenedOperand(visitor, args);
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_thread_fence", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, order, 1, retType);
        break;
    }
    case bfiAtomicSignalFence: {
        IrNode *order = lowerFlattenedOperand(visitor, args);
        order->next = NULL;
        const IrType *retType = lowerType(ctx, node->type);
        IrNode *fn = makeIrGlobalRef(
            ctx->pool, &node->loc, "__atomic_signal_fence", NULL, NULL);
        node->codegen = makeIrCall(ctx->pool, &node->loc, fn, order, 1, retType);
        break;
    }
    case bfiCopy: {
        IrNode *src = lowerFlattenedOperand(visitor, args);
        if (args && args->next) {
            IrNode *dst = lowerFlattenedOperand(visitor, args->next);
            node->codegen = makeIrCopy(ctx->pool, &node->loc, src, dst);
        }
        else {
            const IrType *elemType =
                lowerType(ctx, node->type ?: (args ? args->type : NULL));
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                            &node->loc,
                                            flgTemporary,
                                            tempName,
                                            tempName,
                                            elemType,
                                            ctx->currentScopeId);
            emitStmt(ctx, tempVar);
            IrNode *dst = makeIrVarRef(ctx->pool, &node->loc, tempVar);
            emitStmt(ctx, makeIrCopy(ctx->pool, &node->loc, src, dst));
            node->codegen = dst;
        }
        break;
    }
    case bfiMove: {
        IrNode *src = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrMove(ctx->pool, &node->loc, src);
        break;
    }
    case bfiDrop: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrDrop(ctx->pool, &node->loc, target);
        break;
    }
    case bfiCurrentException: {
        // Borrowed: the exception stays owned by the catch result temporary
        if (ctx->currentCatchErr == NULL) {
            logError(ctx->driver->L,
                     &node->loc,
                     "`ex!` used outside of a catch block during IR lowering",
                     NULL);
            node->codegen = NULL;
            break;
        }
        node->codegen = cloneIrNode(ctx->pool, ctx->currentCatchErr);
        break;
    }
    default:
        node->codegen = NULL;
        break;
    }
}

static void visitGroupExpr(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->groupExpr.expr);
    node->codegen = node->groupExpr.expr->codegen;
}

static void visitAliasExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *expr = lowerFlattenedOperand(visitor, node->aliasExpr.expr);
    cstring name = node->aliasExpr.name ?: getDeclarationName(node);
    node->codegen = expr;
    node->ir = makeIrAlias(ctx->pool, &node->loc, name, expr);
}

// Module globals get the declaring module's prefix; references reuse it via
// decl->ir, so only the declaration decides the C name.
static cstring makeGlobalVarSymbolName(LowerContext *ctx,
                                       const AstNode *node,
                                       cstring name)
{
    if (name == NULL || hasFlag(node, Extern))
        return name;
    cstring modName = ctx->module ? ctx->module->module.name : NULL;
    if (modName == NULL || modName == S_main)
        return name;
    return makeStringf(ctx->strings, "%s_%s", modName, name);
}

// `@register("x0")` pins a local to a machine register, see inline asm operands
static cstring getRegisterAttr(const AstNode *node)
{
    const AstNode *attr = findAttribute(node, S_register);
    const AstNode *arg = attr ? attr->attr.args : NULL;
    return arg && nodeIs(arg, StringLit) ? arg->stringLiteral.value : NULL;
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    if (hasFlag(node, Comptime))
        return;

    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *varAstType =
        (node->varDecl.names && node->varDecl.names->type)
            ? node->varDecl.names->type
            : (node->varDecl.type ? node->varDecl.type->type : node->type);
    varAstType = resolveUnThisUnwrapType(varAstType);
    const IrType *irType = lowerType(ctx, varAstType);
    cstring name = getDeclarationName(node);

    if (ctx->currentFunc == NULL) {
        IrNode *init = NULL;
        if (node->varDecl.init) {
            astVisit(visitor, node->varDecl.init);
            init = (IrNode *)node->varDecl.init->codegen;
        }
        IrNode *glob = makeIrGlobalVar(ctx->driver->pool,
                                       &node->loc,
                                       node->flags,
                                       makeGlobalVarSymbolName(ctx, node, name),
                                       irType,
                                       init);
        // @thread: one instance per thread, not shared by the whole program
        glob->globalVar.isThreadLocal = findAttribute(node, S_thread) != NULL;
        node->ir = glob;
        if (node->varDecl.names)
            node->varDecl.names->ir = glob;

        if (ctx->module->module.globals) {
            IrNode *last = ctx->module->module.globals;
            while (last->next)
                last = last->next;
            last->next = glob;
        }
        else {
            ctx->module->module.globals = glob;
        }
        return;
    }

    cstring displayName = getDeclarationName(node);
    cstring cName = makeUniqueVarName(ctx, displayName);
    Flags flags = node->flags;
    if (!isReferenceType(varAstType) && !isPointerType(varAstType) &&
        isDestructible(varAstType)) {
        flags |= flgReferenceMembers;
    }
    IrNode *varDecl = makeIrVarDecl(ctx->pool,
                                    &node->loc,
                                    flags,
                                    cName,
                                    displayName,
                                    irType,
                                    ctx->currentScopeId);
    varDecl->varDecl.reg = getRegisterAttr(node);
    node->ir = varDecl;
    if (node->varDecl.names)
        node->varDecl.names->ir = varDecl;
    emitStmt(ctx, varDecl);

    if (node->varDecl.init) {
        AstNode *initNode = node->varDecl.init;
        if (hasFlag(varAstType, Slice) || isSliceType(varAstType)) {
            if (initNode->type && isSliceType(initNode->type)) {
                astVisit(visitor, initNode);
                IrNode *val = (IrNode *)initNode->codegen;
                if (val)
                    emitStmt(ctx,
                             makeIrAssign(
                                 ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
            }
            else {
                lowerSliceIntoTarget(ctx,
                                     visitor,
                                     initNode,
                                     makeIrVarRef(ctx->pool, &node->loc, varDecl));
            }
        }
        else if (isOptionalType(varAstType)) {
            if (initNode->type && isOptionalType(initNode->type)) {
                astVisit(visitor, initNode);
                IrNode *val = (IrNode *)initNode->codegen;
                if (val)
                    emitStmt(ctx,
                             makeIrAssign(
                                 ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
            }
            else {
                lowerOptionalIntoTarget(
                    ctx,
                    visitor,
                    initNode,
                    makeIrVarRef(ctx->pool, &node->loc, varDecl));
            }
        }
        else if (typeIs(varAstType, Union)) {
            lowerTaggedUnionIntoTarget(
                ctx,
                visitor,
                initNode,
                makeIrVarRef(ctx->pool, &node->loc, varDecl),
                varAstType);
        }
        else if (nodeIs(initNode, BackendCall) &&
                 initNode->backendCallExpr.func == bfiCopy &&
                 initNode->backendCallExpr.args &&
                 initNode->backendCallExpr.args->next == NULL) {
            IrNode *src =
                lowerFlattenedOperand(visitor, initNode->backendCallExpr.args);
            emitStmt(ctx,
                     makeIrCopy(ctx->pool,
                                &node->loc,
                                src,
                                makeIrVarRef(ctx->pool, &node->loc, varDecl)));
        }
        else {
            astVisit(visitor, initNode);
            IrNode *val = (IrNode *)initNode->codegen;
            if (val)
                emitStmt(
                    ctx,
                    makeIrAssign(ctx->pool,
                                 &node->loc,
                                 makeIrVarRef(ctx->pool, &node->loc, varDecl),
                                 val));
        }
    }
    else if (isOptionalType(varAstType)) {
        lowerOptionalIntoTarget(
            ctx, visitor, NULL, makeIrVarRef(ctx->pool, &node->loc, varDecl));
    }
}

static inline Operator assignOpToBinaryOp(Operator op)
{
    switch (op) {
#define f(O, ...)                                                              \
    case op##O##Equal:                                                         \
        return op##O;
        AST_ASSIGN_EXPR_LIST(f)
#undef f
    default:
        return opInvalid;
    }
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *lhs = lowerFlattenedOperand(visitor, node->assignExpr.lhs);
    AstNode *rhsNode = node->assignExpr.rhs;
    const Type *lhsAstType = node->assignExpr.lhs->type;
    const Type *rhsAstType = rhsNode->type;

    if (lhsAstType && isReferenceType(lhsAstType) &&
        !isClassType(stripReference(lhsAstType)) &&
        !isReferenceType(rhsAstType) &&
        !isClassType(stripReference(rhsAstType))) {
        lhs = makeIrDeref(ctx->pool,
                          &node->assignExpr.lhs->loc,
                          lhs,
                          lowerType(ctx, stripReference(lhsAstType)));
    }

    if (lhsAstType && (hasFlag(lhsAstType, Slice) || isSliceType(lhsAstType)) &&
        (!rhsAstType ||
         (!hasFlag(rhsAstType, Slice) && !isSliceType(rhsAstType)))) {
        lowerSliceIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else if (isOptionalType(lhsAstType) && !isOptionalType(rhsAstType)) {
        lowerOptionalIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else if (lhsAstType && typeIs(lhsAstType, Union) &&
             (nodeIs(rhsNode, UnionValueExpr) ||
              (rhsAstType && !typeIs(rhsAstType, Union)))) {
        lowerTaggedUnionIntoTarget(ctx, visitor, rhsNode, lhs, lhsAstType);
    }
    else if (isBackendCopyExpr(rhsNode)) {
        IrNode *src =
            lowerFlattenedOperand(visitor, rhsNode->backendCallExpr.args);
        emitStmt(ctx, makeIrCopy(ctx->pool, &node->loc, src, lhs));
    }
    else {
        astVisit(visitor, rhsNode);
        IrNode *rhs = (IrNode *)rhsNode->codegen;
        if (rhs) {
            Operator op = node->assignExpr.op;
            if (op != opAssign && op != opInvalid) {
                rhs = makeIrBinary(ctx->pool,
                                   &node->loc,
                                   op,
                                   cloneIrNode(ctx->pool, lhs),
                                   rhs,
                                   lowerType(ctx, lhsAstType));
            }
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, lhs, rhs));
        }
    }
}

// The payload of a `!T` call result is owned by the caller, the result
// temporary itself is never dropped
static bool isOwnedResultPayload(const IrNode *expr)
{
    if (expr->tag != irMember || !isIrTypeDestructible(expr->type))
        return false;
    const IrNode *root = expr;
    while (root->tag == irMember)
        root = root->member.target;
    const IrNode *decl = root->tag == irVarRef ? root->varRef.target : NULL;
    return decl && hasFlag(decl, Temporary) && decl->type &&
           decl->type->category == irTypeCatResult;
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->exprStmt.expr);
    IrNode *expr = (IrNode *)node->exprStmt.expr->codegen;
    node->codegen = expr;
    if (expr) {
        const Type *astType =
            node->exprStmt.expr->type
                ? resolveUnThisUnwrapType(node->exprStmt.expr->type)
                : NULL;
        // the payload already lives in the result temporary, drop it in place
        if (isOwnedResultPayload(expr)) {
            emitStmt(ctx, makeIrDrop(ctx->pool, &node->loc, expr));
            return;
        }
        // the value of a discarded catch lives in its temporary, drop it in place
        if (nodeIs(node->exprStmt.expr, CatchExpr) && expr->tag == irVarRef &&
            isIrTypeDestructible(expr->type)) {
            emitStmt(ctx, makeIrDrop(ctx->pool, &node->loc, expr));
            return;
        }
        if (expr->tag == irVarRef || isIrConst(expr) ||
            (isIrPureValueExpr(expr) && expr->tag != irCall)) {
            return;
        }
        if (expr->tag == irCall && astType && !isReferenceType(astType) &&
            !isPointerType(astType) && isDestructible(astType)) {
            cstring tempName =
                makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *tempVar = makeIrVarDecl(ctx->pool,
                                            &node->loc,
                                            flgEphemeral | flgReferenceMembers,
                                            tempName,
                                            tempName,
                                            expr->type,
                                            ctx->currentScopeId);
            emitStmt(ctx, tempVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, tempVar),
                                  expr));
        }
        else {
            emitStmt(ctx, expr);
        }
    }
}

static void visitStmtExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *stmt = node->stmtExpr.stmt;
    if (stmt == NULL)
        return;

    if (nodeIs(stmt, BlockStmt)) {
        IrNode *lastVal = NULL;
        for (AstNode *s = stmt->blockStmt.stmts; s; s = s->next) {
            astVisit(visitor, s);
            if (s->codegen) {
                lastVal = (IrNode *)s->codegen;
            }
            else if (nodeIs(s, ExprStmt) && s->exprStmt.expr && s->exprStmt.expr->codegen) {
                lastVal = (IrNode *)s->exprStmt.expr->codegen;
            }
        }
        node->codegen = lastVal;
    }
    else {
        astVisit(visitor, stmt);
        node->codegen = stmt->codegen;
    }
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 scopeId = ctx->nextScopeId++;
    u32 prevScopeId = ctx->currentScopeId;
    csAssert0(scopeId == ctx->scopes.size);
    pushOnDynArray(&ctx->scopes,
                   &(ScopeInfo){.parent = prevScopeId, .hasDrops = false});
    ctx->currentScopeId = scopeId;
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, scopeId));

    for (AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        astVisit(visitor, stmt);
    }

    if (!isBlockTerminated(ctx->currentBlock))
        emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, scopeId));
    ctx->currentScopeId = prevScopeId;
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->ifStmt.cond);
    u32 ifId = ctx->nextIfId++;
    IrNode *thenBB =
        createBlock(ctx, makeStringf(ctx->strings, "IF%u_then", ifId));
    IrNode *elseBB =
        node->ifStmt.otherwise
            ? createBlock(ctx, makeStringf(ctx->strings, "IF%u_else", ifId))
            : NULL;
    IrNode *mergeBB =
        createBlock(ctx, makeStringf(ctx->strings, "IF%u_end", ifId));

    emitStmt(
        ctx,
        makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB ?: mergeBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ifStmt.body);
    emitBranchIfNotTerminated(ctx, mergeBB);

    if (elseBB) {
        setBlock(ctx, elseBB);
        astVisit(visitor, node->ifStmt.otherwise);
        emitBranchIfNotTerminated(ctx, mergeBB);
    }

    setBlock(ctx, mergeBB);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 whileId = ctx->nextWhileId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_cond", whileId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_body", whileId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "WHILE%u_exit", whileId));

    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, condBB);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->whileStmt.cond);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = condBB,
                    .exitBlock = exitBB,
                    .scopeId = ctx->currentScopeId,
                    .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->whileStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

static void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);

    if (nodeIs(node->forStmt.range, RangeExpr)) {
        AstNode *startNode = node->forStmt.range->rangeExpr.start;
        AstNode *endNode = node->forStmt.range->rangeExpr.end;
        AstNode *stepNode = node->forStmt.range->rangeExpr.step;

        IrNode *startVal = lowerFlattenedOperand(visitor, startNode);
        IrNode *endVal = lowerFlattenedOperand(visitor, endNode);

        // sema typed the variable with the common type of the range, the
        // start value alone may be a small literal (`0..size`)
        AstNode *loopVarNode = node->forStmt.var;
        const IrType *varType =
            loopVarNode->type ? lowerType(ctx, loopVarNode->type)
                              : (startVal ? startVal->type : u64Type);
        cstring displayName = getDeclarationName(loopVarNode);
        cstring cName = makeUniqueVarName(ctx, displayName);
        IrNode *varDecl = makeIrVarDecl(ctx->pool,
                                        &loopVarNode->loc,
                                        loopVarNode->flags,
                                        cName,
                                        displayName,
                                        varType,
                                        ctx->currentScopeId);
        loopVarNode->ir = varDecl;
        emitStmt(ctx, varDecl);
        emitStmt(
            ctx,
            makeIrAssign(ctx->pool,
                         &startNode->loc,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         startVal));

        u32 forId = ctx->nextForId++;
        IrNode *condBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_cond", forId));
        IrNode *bodyBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_body", forId));
        IrNode *updateBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_update", forId));
        IrNode *exitBB =
            createBlock(ctx, makeStringf(ctx->strings, "FOR%u_exit", forId));

        emitBranchIfNotTerminated(ctx, condBB);
        setBlock(ctx, condBB);

        IrNode *cond =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opLt,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         endVal,
                         boolType);
        emitStmt(ctx,
                 makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

        setBlock(ctx, bodyBB);
        LoopCtx loop = {.condBlock = updateBB,
                        .exitBlock = exitBB,
                        .scopeId = ctx->currentScopeId,
                        .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
        ctx->loopCtx = &loop;
        astVisit(visitor, node->forStmt.body);
        ctx->loopCtx = loop.prev;
        emitBranchIfNotTerminated(ctx, updateBB);

        setBlock(ctx, updateBB);
        IrNode *stepVal =
            stepNode ? lowerFlattenedOperand(visitor, stepNode)
                     : makeIrIntConst(ctx->pool, &node->loc, 1, false, varType);
        IrNode *nextVal =
            makeIrBinary(ctx->pool,
                         &node->loc,
                         opAdd,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         stepVal,
                         varType);
        emitStmt(
            ctx,
            makeIrAssign(ctx->pool,
                         &node->loc,
                         makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl),
                         nextVal));
        emitBranchIfNotTerminated(ctx, condBB);

        setBlock(ctx, exitBB);
        return;
    }

    u32 forId = ctx->nextForId++;
    IrNode *condBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_cond", forId));
    IrNode *bodyBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_body", forId));
    IrNode *updateBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_update", forId));
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "FOR%u_exit", forId));

    emitBranchIfNotTerminated(ctx, condBB);
    setBlock(ctx, condBB);

    IrNode *cond = lowerConditionOperand(ctx, visitor, node->forStmt.range);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = updateBB,
                    .exitBlock = exitBB,
                    .scopeId = ctx->currentScopeId,
                    .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->forStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, updateBB);

    setBlock(ctx, updateBB);
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

// Drops a caught exception and defuses its result so it is not dropped twice
static void emitReleaseCaught(LowerContext *ctx,
                              const FileLoc *loc,
                              const CatchCtx *c)
{
    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    emitStmt(ctx, makeIrDrop(ctx->pool, loc, cloneIrNode(ctx->pool, c->err)));
    IrNode *tagSlot = makeIrMember(
        ctx->pool, loc, cloneIrNode(ctx->pool, c->res), 0, S_tag, u32Type);
    IrNode *defuse = makeIrAssign(
        ctx->pool,
        loc,
        tagSlot,
        makeIrIntConst(ctx->pool, loc, 0, false, u32Type));
    defuse->flags |= flgUnmanaged;
    emitStmt(ctx, defuse);
}

// Releases the catch bodies entered since `outer`, innermost first
static void emitReleaseCaughtUntil(LowerContext *ctx,
                                   const FileLoc *loc,
                                   const CatchCtx *outer)
{
    for (const CatchCtx *c = ctx->catchCtx; c != outer; c = c->prev)
        emitReleaseCaught(ctx, loc, c);
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->exitBlock) {
        emitReleaseCaughtUntil(ctx, &node->loc, ctx->loopCtx->catchCtx);
        for (u32 s = ctx->currentScopeId; s > ctx->loopCtx->scopeId; s--) {
            emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, s));
        }
        emitStmt(ctx,
                 makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->exitBlock));
    }
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->condBlock) {
        emitReleaseCaughtUntil(ctx, &node->loc, ctx->loopCtx->catchCtx);
        for (u32 s = ctx->currentScopeId; s > ctx->loopCtx->scopeId; s--) {
            emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, s));
        }
        emitStmt(ctx,
                 makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->condBlock));
    }
}

static u32 nearestDirtyScope(LowerContext *ctx, u32 scopeId)
{
    while (scopeId != 0 && scopeId < ctx->scopes.size &&
           !dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).hasDrops)
        scopeId = dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).parent;
    return scopeId;
}

static IrNode *getOrCreateScopeReturnBlock(LowerContext *ctx,
                                           u32 scopeId,
                                           const FileLoc *loc)
{
    scopeId = nearestDirtyScope(ctx, scopeId);
    if (scopeId == 0)
        return ctx->funcExitBlock;

    cstring name = makeStringf(ctx->strings, "_L_UNWIND%u_return", scopeId);
    if (ctx->currentFunc) {
        for (IrNode *b = ctx->currentFunc->function.firstBlock; b;
             b = b->next) {
            if (b->tag == irBasicBlock && b->basicBlock.name == name)
                return b;
        }
    }

    IrNode *savedBlock = ctx->currentBlock;
    IrNode *unwindBB =
        createBlock(ctx, makeStringf(ctx->strings, "UNWIND%u_return", scopeId));
    setBlock(ctx, unwindBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, loc, scopeId));
    IrNode *parentUnwindBB = getOrCreateScopeReturnBlock(
        ctx, dynArrayAt(ScopeInfo *, &ctx->scopes, scopeId).parent, loc);
    emitStmt(ctx, makeIrBranch(ctx->pool, loc, parentUnwindBB));
    ctx->currentBlock = savedBlock;
    return unwindBB;
}

static void visitYieldStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->yieldStmt.expr && ctx->catchTargetVar) {
        IrNode *val = lowerFlattenedOperand(visitor, node->yieldStmt.expr);
        emitStmt(ctx,
                 makeIrAssign(ctx->pool,
                              &node->loc,
                              makeIrVarRef(ctx->pool, &node->loc, ctx->catchTargetVar),
                              val));
    }
    if (ctx->currentCatchErr) {
        emitStmt(ctx,
                 makeIrDrop(ctx->pool,
                            &node->loc,
                            cloneIrNode(ctx->pool, ctx->currentCatchErr)));
    }
    if (ctx->currentCatchRes) {
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *resTagSlot = makeIrMember(
            ctx->pool,
            &node->loc,
            cloneIrNode(ctx->pool, ctx->currentCatchRes),
            0,
            S_tag,
            u32Type);
        IrNode *defuseStmt = makeIrAssign(
            ctx->pool,
            &node->loc,
            resTagSlot,
            makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);
    }
    if (ctx->catchMergeBB) {
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, ctx->catchMergeBB));
    }
}

// The success payload of a result value, `targetType` is the unwrapped type
static IrNode *makeResultPayloadRef(LowerContext *ctx,
                                    const FileLoc *loc,
                                    const IrNode *res,
                                    const IrType *resIrType,
                                    const Type *targetType)
{
    IrNode *valMember = makeIrMember(ctx->pool,
                                     loc,
                                     cloneIrNode(ctx->pool, res),
                                     1,
                                     S_val,
                                     resIrType->strct.fields[1].type);
    if (valMember->type && valMember->type->tag == irTypeUntaggedUnion) {
        return makeIrMember(
            ctx->pool, loc, valMember, 0, NULL, lowerType(ctx, targetType));
    }
    return valMember;
}

static void visitCatchExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    bool prevExplicitCatch = ctx->explicitCatch;
    ctx->explicitCatch = true;

    IrNode *res = lowerFlattenedOperand(visitor, node->catchExpr.expr);
    ctx->explicitCatch = prevExplicitCatch;

    const Type *exprType = node->catchExpr.expr->type;
    const Type *targetType = node->type;
    const IrType *resIrType = lowerType(ctx, exprType);

    cstring catchName = makeStringf(ctx->strings, "catch_%u", ctx->nextIfId++);
    IrNode *catchBB = createBlock(ctx, catchName);
    IrNode *continueBB = createBlock(ctx, makeStringf(ctx->strings, "cont_%u", ctx->nextIfId));
    IrNode *mergeBB = createBlock(ctx, makeStringf(ctx->strings, "merge_%u", ctx->nextIfId));

    IrNode *targetVar = NULL;
    bool hasResultValue = targetType && !isVoidType(targetType);
    if (hasResultValue) {
        cstring varName =
            makeStringf(ctx->strings, "__catch_res%u", ctx->tempCounter++);
        const IrType *irTargetType = lowerType(ctx, targetType);
        Flags flags = flgTemporary;
        if (isIrTypeDestructible(irTargetType))
            flags |= flgReferenceMembers;
        targetVar = makeIrVarDecl(ctx->pool,
                                  &node->loc,
                                  flags,
                                  varName,
                                  varName,
                                  irTargetType,
                                  ctx->currentScopeId);
        emitStmt(ctx, targetVar);
    }

    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->loc, cloneIrNode(ctx->pool, res), 0, S_tag, u32Type);
    IrNode *cond = makeIrBinary(
        ctx->pool,
        &node->loc,
        opEq,
        tagVal,
        makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type),
        getIrPrimitiveType(ctx->types, irTypeBool));
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, catchBB, continueBB));

    // Continue BB (success: tag == 0)
    setBlock(ctx, continueBB);
    if (hasResultValue) {
        IrNode *unwrapped =
            makeResultPayloadRef(ctx, &node->loc, res, resIrType, targetType);
        IrNode *assignStmt = makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, targetVar), unwrapped);
        assignStmt->flags |= flgUnmanaged;
        emitStmt(ctx, assignStmt);

        IrNode *resTagSlot = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, res), 0, S_tag, u32Type);
        IrNode *defuseStmt = makeIrAssign(ctx->pool, &node->loc, resTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
        defuseStmt->flags |= flgUnmanaged;
        emitStmt(ctx, defuseStmt);
    }
    else if (!isVoidResultType(exprType)) {
        // nobody consumes the payload of a discarded result, drop it in place
        const Type *payloadType = getResultTargetType(exprType);
        if (isIrTypeDestructible(lowerType(ctx, payloadType))) {
            emitStmt(ctx,
                     makeIrDrop(ctx->pool,
                                &node->loc,
                                makeResultPayloadRef(ctx,
                                                     &node->loc,
                                                     res,
                                                     resIrType,
                                                     payloadType)));
        }
    }
    emitBranchIfNotTerminated(ctx, mergeBB);

    // Catch BB (error: tag == 1)
    setBlock(ctx, catchBB);
    IrNode *valMember = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, res), 1, S_val, resIrType->strct.fields[1].type);
    IrNode *errVal = valMember;
    if (valMember->type && valMember->type->tag == irTypeUntaggedUnion) {
        const Type *baseExType = findBuiltinType(S_Exception);
        const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
        errVal = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
    }

    if (node->catchExpr.var) {
        node->catchExpr.var->codegen = errVal;
        node->catchExpr.var->ir = makeIrAlias(
            ctx->pool,
            &node->loc,
            node->catchExpr.var ? node->catchExpr.var->_name : S_ex,
            errVal);
    }

    AstNode *body = node->catchExpr.body;
    if (body) {
        IrNode *prevCatchErr = ctx->currentCatchErr;
        IrNode *prevCatchRes = ctx->currentCatchRes;
        ctx->currentCatchErr = errVal;
        ctx->currentCatchRes = res;
        CatchCtx catchCtx = {.err = errVal, .res = res, .prev = ctx->catchCtx};
        ctx->catchCtx = &catchCtx;

        if (nodeIs(body, BlockStmt)) {
            IrNode *prevCatchTarget = ctx->catchTargetVar;
            IrNode *prevCatchMerge = ctx->catchMergeBB;
            ctx->catchTargetVar = targetVar;
            ctx->catchMergeBB = mergeBB;

            astVisit(visitor, body);

            ctx->catchTargetVar = prevCatchTarget;
            ctx->catchMergeBB = prevCatchMerge;

            if (ctx->currentBlock && !isBlockTerminated(ctx->currentBlock)) {
                emitStmt(ctx,
                         makeIrDrop(ctx->pool,
                                    &node->loc,
                                    cloneIrNode(ctx->pool, errVal)));
                IrNode *resTagSlot = makeIrMember(
                    ctx->pool,
                    &node->loc,
                    cloneIrNode(ctx->pool, res),
                    0,
                    S_tag,
                    u32Type);
                IrNode *defuseStmt = makeIrAssign(
                    ctx->pool,
                    &node->loc,
                    resTagSlot,
                    makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
                defuseStmt->flags |= flgUnmanaged;
                emitStmt(ctx, defuseStmt);
            }
            emitBranchIfNotTerminated(ctx, mergeBB);
        }
        else {
            IrNode *defVal = lowerFlattenedOperand(visitor, body);
            if (hasResultValue && defVal) {
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, targetVar), defVal));
            }
            if (ctx->currentBlock && !isBlockTerminated(ctx->currentBlock)) {
                emitStmt(ctx,
                         makeIrDrop(ctx->pool,
                                    &node->loc,
                                    cloneIrNode(ctx->pool, errVal)));
                IrNode *resTagSlot = makeIrMember(
                    ctx->pool,
                    &node->loc,
                    cloneIrNode(ctx->pool, res),
                    0,
                    S_tag,
                    u32Type);
                IrNode *defuseStmt = makeIrAssign(
                    ctx->pool,
                    &node->loc,
                    resTagSlot,
                    makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
                defuseStmt->flags |= flgUnmanaged;
                emitStmt(ctx, defuseStmt);
            }
            emitBranchIfNotTerminated(ctx, mergeBB);
        }

        ctx->catchCtx = catchCtx.prev;
        ctx->currentCatchErr = prevCatchErr;
        ctx->currentCatchRes = prevCatchRes;
    }

    setBlock(ctx, mergeBB);
    if (hasResultValue) {
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, targetVar);
    }
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->returnStmt.expr && ctx->funcRetVar) {
        AstNode *retExpr = node->returnStmt.expr;
        IrNode *retVarRef =
            makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        const Type *retAstType = ctx->funcRetAstType;
        const Type *exprAstType = retExpr->type;

        if (isResultType(retAstType)) {
            const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
            IrNode *tagSlot = makeIrMember(ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
            const IrType *retIrType = ctx->funcRetVar->type;
            const IrType *valFieldType = retIrType->strct.fields[1].type;
            IrNode *valMember = makeIrMember(ctx->pool, &node->loc, retVarRef, 1, S_val, valFieldType);

            if (node->returnStmt.isRaise) {
                IrNode *errVal = lowerFlattenedOperand(visitor, retExpr);
                emitExceptionTracePush(ctx, &node->loc, errVal);
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));
                if (valFieldType->tag == irTypeUntaggedUnion) {
                    const Type *baseExType = findBuiltinType(S_Exception);
                    const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
                    IrNode *errSlot = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
                    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, errSlot, errVal));
                }
                else {
                    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valMember, errVal));
                }
            }
            else {
                emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type)));
                if (retExpr && valFieldType->tag == irTypeUntaggedUnion) {
                    const IrType *elemType = valFieldType->strct.fields[0].type;
                    IrNode *valSlot = makeIrMember(ctx->pool, &node->loc, valMember, 0, NULL, elemType);
                    const Type *resTargetType = getResultTargetType(retAstType);
                    if (isSliceType(resTargetType) && (exprAstType == NULL || !isSliceType(exprAstType))) {
                        lowerSliceIntoTarget(ctx, visitor, retExpr, valSlot);
                    }
                    else if (isOptionalType(resTargetType) && !isOptionalType(exprAstType)) {
                        lowerOptionalIntoTarget(ctx, visitor, retExpr, valSlot);
                    }
                    else {
                        IrNode *val = lowerFlattenedOperand(visitor, retExpr);
                        if (val)
                            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, valSlot, val));
                    }
                }
            }
        }
        else if (isSliceType(retAstType) && (exprAstType == NULL ||!isSliceType(exprAstType))) {
            lowerSliceIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else if (isOptionalType(retAstType) && !isOptionalType(exprAstType)) {
            lowerOptionalIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else if (retAstType && typeIs(retAstType, Union) &&
                 (nodeIs(retExpr, UnionValueExpr) ||
                  (exprAstType && !typeIs(exprAstType, Union)))) {
            lowerTaggedUnionIntoTarget(
                ctx, visitor, retExpr, retVarRef, retAstType);
        }
        else if (isBackendCopyExpr(retExpr)) {
            IrNode *src =
                lowerFlattenedOperand(visitor, retExpr->backendCallExpr.args);
            emitStmt(ctx, makeIrCopy(ctx->pool, &node->loc, src, retVarRef));
        }
        else {
            astVisit(visitor, retExpr);
            IrNode *val = (IrNode *)retExpr->codegen;
            if (val)
                emitStmt(ctx,
                         makeIrAssign(ctx->pool, &node->loc, retVarRef, val));
        }
    }
    else if (ctx->funcRetVar && isResultType(ctx->funcRetAstType)) {
        IrNode *retVarRef = makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
        IrNode *tagSlot = makeIrMember(ctx->pool, &node->loc, retVarRef, 0, S_tag, u32Type);
        if (node->returnStmt.isRaise) {
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 1, false, u32Type)));
            if (ctx->currentCatchErr) {
                emitExceptionTracePush(ctx, &node->loc, ctx->currentCatchErr);
                const IrType *retIrType = ctx->funcRetVar->type;
                const IrType *valFieldType = retIrType->strct.fields[1].type;
                IrNode *valMember = makeIrMember(ctx->pool, &node->loc, retVarRef, 1, S_val, valFieldType);
                IrNode *assignStmt = NULL;
                if (valFieldType->tag == irTypeUntaggedUnion) {
                    const Type *baseExType = findBuiltinType(S_Exception);
                    const IrType *exIrType = baseExType ? lowerType(ctx, baseExType) : getIrPrimitiveType(ctx->types, irTypeOpaque);
                    IrNode *errSlot = makeIrMember(ctx->pool, &node->loc, valMember, 1, NULL, exIrType);
                    assignStmt = makeIrAssign(ctx->pool, &node->loc, errSlot, cloneIrNode(ctx->pool, ctx->currentCatchErr));
                }
                else {
                    assignStmt = makeIrAssign(ctx->pool, &node->loc, valMember, cloneIrNode(ctx->pool, ctx->currentCatchErr));
                }
                assignStmt->flags |= flgUnmanaged;
                emitStmt(ctx, assignStmt);
                if (ctx->currentCatchRes) {
                    IrNode *srcTagSlot = makeIrMember(ctx->pool, &node->loc, cloneIrNode(ctx->pool, ctx->currentCatchRes), 0, S_tag, u32Type);
                    IrNode *defuseStmt = makeIrAssign(ctx->pool, &node->loc, srcTagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type));
                    defuseStmt->flags |= flgUnmanaged;
                    emitStmt(ctx, defuseStmt);
                }
            }
        }
        else {
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, tagSlot, makeIrIntConst(ctx->pool, &node->loc, 0, false, u32Type)));
        }
    }
    // `raise` moves the caught exception, any other return leaves it owned
    if (!node->returnStmt.isRaise)
        emitReleaseCaughtUntil(ctx, &node->loc, NULL);
    IrNode *targetBB =
        getOrCreateScopeReturnBlock(ctx, ctx->currentScopeId, &node->loc);
    emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, targetBB));
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerFlattenedOperand(visitor, node->switchStmt.cond);
    u32 switchId = ctx->nextSwitchId++;
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "SWITCH%u_exit", switchId));

    u64 totalCases = 0;
    for (const AstNode *c = node->switchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match)
            totalCases += countAstNodes(c->caseStmt.match);
        else
            totalCases++;
    }

    IrCase *cases =
        allocFromMemPool(ctx->pool, sizeof(IrCase) * (totalCases ?: 1));
    u64 caseIdx = 0;
    IrNode *defaultBB = NULL;

    u64 caseStmtCount = countAstNodes(node->switchStmt.cases);
    IrNode **caseBBs =
        allocFromMemPool(ctx->pool, sizeof(IrNode *) * (caseStmtCount ?: 1));

    // Pass 1: Create a single basic block per shared body
    u32 cIdx = 0;
    for (AstNode *c = node->switchStmt.cases; c; c = c->next, cIdx++) {
        if (c->caseStmt.body != NULL || c->next == NULL) {
            caseBBs[cIdx] = createBlock(
                ctx, makeStringf(ctx->strings, "SWITCH%u_case_%u", switchId, cIdx));
        }
    }
    // Propagate the target block backwards to preceding cases sharing the same body
    IrNode *currentGroupBB = NULL;
    for (i64 i = (i64)caseStmtCount - 1; i >= 0; i--) {
        if (caseBBs[i] != NULL)
            currentGroupBB = caseBBs[i];
        else
            caseBBs[i] = currentGroupBB;
    }

    // Pass 2: Lower cases and emit bodies
    cIdx = 0;
    for (AstNode *c = node->switchStmt.cases; c; c = c->next, cIdx++) {
        IrNode *caseBB = caseBBs[cIdx];
        if (c->caseStmt.match == NULL) {
            defaultBB = caseBB;
        }
        else {
            for (const AstNode *m = c->caseStmt.match; m; m = m->next) {
                if (nodeIs(m, RangeExpr)) {
                    IrNode *startNode = lowerFlattenedOperand(visitor, m->rangeExpr.start);
                    IrNode *endNode = lowerFlattenedOperand(visitor, m->rangeExpr.end);
                    cases[caseIdx++] = (IrCase){
                        .valNode = startNode, .endNode = endNode, .target = caseBB};
                }
                else {
                    IrNode *valNode = lowerFlattenedOperand(visitor, (AstNode *)m);
                    cases[caseIdx++] = (IrCase){
                        .valNode = valNode, .endNode = NULL, .target = caseBB};
                }
            }
        }

        if (c->caseStmt.body) {
            IrNode *savedBlock = ctx->currentBlock;
            setBlock(ctx, caseBB);
            LoopCtx loop = {
                .condBlock = NULL, .exitBlock = exitBB, .catchCtx = ctx->catchCtx, .prev = ctx->loopCtx};
            ctx->loopCtx = &loop;
            astVisit(visitor, c->caseStmt.body);
            ctx->loopCtx = loop.prev;
            emitBranchIfNotTerminated(ctx, exitBB);
            ctx->currentBlock = savedBlock;
        }
    }

    IrNode *switchNode = makeIrSwitch(
        ctx->pool, &node->loc, cond, cases, caseIdx, defaultBB ?: exitBB);
    emitStmt(ctx, switchNode);

    setBlock(ctx, exitBB);
}

static void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *unionVal = lowerFlattenedOperand(visitor, node->matchStmt.expr);
    const IrType *u32Type = getIrPrimitiveType(ctx->types, irTypeU32);
    IrNode *tagVal = makeIrMember(
        ctx->pool, &node->matchStmt.expr->loc, unionVal, 0, "tag", u32Type);
    u32 matchId = ctx->nextMatchId++;
    IrNode *exitBB =
        createBlock(ctx, makeStringf(ctx->strings, "MATCH%u_exit", matchId));

    u64 casesCount = 0;
    AstNode *defaultCase = node->matchStmt.defaultCase;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match != NULL)
            casesCount++;
        else if (defaultCase == NULL)
            defaultCase = c;
    }

    IrCase *cases =
        allocFromMemPool(ctx->pool, sizeof(IrCase) * (casesCount ?: 1));
    IrNode **caseBBs =
        allocFromMemPool(ctx->pool, sizeof(IrNode *) * (casesCount ?: 1));

    const Type *sumType =
        stripPointerOrReferenceOnce(node->matchStmt.expr->type, NULL);
    u64 ci = 0;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match == NULL)
            continue;
        IrNode *bb = createBlock(
            ctx,
            makeStringf(
                ctx->strings, "MATCH%u_case_%u", matchId, (unsigned)ci));
        u32 caseIdx = c->caseStmt.idx;
        if (caseIdx == 0 && c->caseStmt.match && sumType &&
            isUnionType(sumType)) {
            u32 found = findUnionTypeIndex(sumType, c->caseStmt.match->type);
            if (found != UINT32_MAX)
                caseIdx = found;
        }
        cases[ci].valNode =
            makeIrIntConst(ctx->pool, &c->loc, caseIdx, false, u32Type);
        cases[ci].target = bb;
        caseBBs[ci] = bb;
        ci++;
    }

    IrNode *defaultBB =
        defaultCase
            ? createBlock(ctx,
                          makeStringf(ctx->strings, "MATCH%u_default", matchId))
            : exitBB;
    emitStmt(ctx,
             makeIrSwitch(
                 ctx->pool, &node->loc, tagVal, cases, casesCount, defaultBB));

    ci = 0;
    for (AstNode *c = node->matchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match == NULL)
            continue;
        u32 caseIdx = cases[ci].valNode->intConst.value;
        setBlock(ctx, caseBBs[ci++]);
        if (c->caseStmt.alias) {
            const IrType *aliasIrType = lowerType(ctx, c->caseStmt.match->type);
            const IrType *unionStructType = unionVal->type;
            if (unionStructType && unionStructType->tag == irTypePointer)
                unionStructType = unionStructType->pointer.pointee;
            const IrType *payloadUnionType =
                (unionStructType && unionStructType->tag == irTypeStruct &&
                 unionStructType->strct.count > 1)
                    ? unionStructType->strct.fields[1].type
                    : NULL;
            IrNode *valMember = makeIrMember(
                ctx->pool, &c->loc, unionVal, 1, "val", payloadUnionType);
            cstring fieldName = makeStringf(ctx->strings, "_%u", caseIdx);
            IrNode *payloadMember = makeIrMember(
                ctx->pool, &c->loc, valMember, caseIdx, fieldName, aliasIrType);
            cstring aliasName = nodeIs(c->caseStmt.alias, AliasExpr)
                                    ? c->caseStmt.alias->aliasExpr.name
                                    : getDeclarationName(c->caseStmt.alias);
            c->caseStmt.alias->ir =
                makeIrAlias(ctx->pool, &c->loc, aliasName, payloadMember);
        }
        if (c->caseStmt.body) {
            astVisit(visitor, c->caseStmt.body);
        }
        emitBranchIfNotTerminated(ctx, exitBB);
    }

    if (defaultCase) {
        setBlock(ctx, defaultBB);
        if (defaultCase->caseStmt.body) {
            astVisit(visitor, defaultCase->caseStmt.body);
        }
        emitBranchIfNotTerminated(ctx, exitBB);
    }

    setBlock(ctx, exitBB);
}

static u8 lowerExternFunctionAttrs(const AstNode *node)
{
    u8 attrs = 0;
    if (findAttribute(node, S_naked))
        attrs |= irFuncAttrNaked;
    if (findAttribute(node, S_noreturn))
        attrs |= irFuncAttrNoReturn;
    if (findAttribute(node, S_returns_twice))
        attrs |= irFuncAttrReturnsTwice;
    return attrs;
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Extern)) {
        bool isHint = findAttribute(node, S_hint) != NULL;

        const IrType *funcType = lowerType(ctx, node->type);
        u64 paramsCount = 0;
        IrNode *paramsFirst = NULL, *paramsLast = NULL;
        u32 pidx = 0;
        for (AstNode *p = node->funcDecl.signature->params; p;
             p = p->next, pidx++) {
            // C varargs marker, emitted as `...` from the function flag
            if (hasFlag(p, Variadic))
                continue;
            paramsCount++;
            const IrType *pType = lowerType(ctx, p->type);
            cstring pname =
                p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
            IrNode *param = makeIrFuncParam(
                ctx->pool, &p->loc, p->flags, pname, pidx, pType);
            p->ir = param;
            if (paramsLast) {
                paramsLast->next = param;
                paramsLast = param;
            }
            else {
                paramsFirst = paramsLast = param;
            }
        }

        cstring funcName = getDeclarationName(node);
        cstring cName = getFunctionSymbolName(ctx, node);
        cstring displayName = funcName;
        IrNode *func = makeIrFunction(ctx->pool,
                                      &node->loc,
                                      node->flags | flgExtern,
                                      cName,
                                      displayName,
                                      funcType,
                                      paramsFirst,
                                      paramsCount);
        func->ast = node;
        func->function.isHint = isHint;
        func->function.attrs = lowerExternFunctionAttrs(node);
        node->ir = func;

        if (ctx->module->module.functions) {
            IrNode *last = ctx->module->module.functions;
            while (last->next)
                last = last->next;
            last->next = func;
        }
        else {
            ctx->module->module.functions = func;
        }
        return;
    }

    const IrType *funcType = lowerType(ctx, node->type);
    u64 paramsCount = countAstNodes(node->funcDecl.signature->params);
    IrNode *paramsFirst = NULL, *paramsLast = NULL;
    u32 pidx = 0;
    for (AstNode *p = node->funcDecl.signature->params; p;
         p = p->next, pidx++) {
        const IrType *pType = lowerType(ctx, p->type);
        cstring pname =
            p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
        Flags pFlags = p->flags;
        const Type *pAstType =
            p->type ? resolveUnThisUnwrapType(p->type) : NULL;
        if (pAstType && !isReferenceType(pAstType) &&
            !isPointerType(pAstType) && !hasFlag(p, Reference) &&
            isDestructible(pAstType)) {
            pFlags |= flgReferenceMembers;
        }
        IrNode *param =
            makeIrFuncParam(ctx->pool, &p->loc, pFlags, pname, pidx, pType);
        p->ir = param;
        if (paramsLast) {
            paramsLast->next = param;
            paramsLast = param;
        }
        else {
            paramsFirst = paramsLast = param;
        }
    }

    cstring funcName = getDeclarationName(node);
    AstNode *parent = node->parentScope;
    cstring modName = ctx->module->module.name;
    cstring cName = getFunctionSymbolName(ctx, node);
    cstring displayName = funcName;

    if (parent && isClassOrStructAstNode(parent)) {
        cstring typeName = getDeclarationName(parent);
        displayName =
            makeStringf(ctx->strings, "%s.%s.%s", modName, typeName, funcName);
    }
    else if (hasFlag(node, Main) || funcName == S_main) {
        displayName = S_main;
    }
    else if (!hasFlag(node, Extern) && !hasFlag(node, Builtin) && modName &&
             modName != S_main) {
        displayName = makeStringf(ctx->strings, "%s.%s", modName, funcName);
    }

    IrNode *func = makeIrFunction(ctx->pool,
                                  &node->loc,
                                  node->flags,
                                  cName,
                                  displayName,
                                  funcType,
                                  paramsFirst,
                                  paramsCount);
    func->ast = node;
    node->ir = func;
    if (node->funcDecl.isInitCover || findAttribute(node, S_memtrace))
        func->function.attrs |= irFuncAttrCallSite;

    ctx->currentFunc = func;
    ctx->nextBlockId = 0;
    ctx->nextForId = 0;
    ctx->nextWhileId = 0;
    ctx->nextIfId = 0;
    ctx->nextSwitchId = 0;
    ctx->nextMatchId = 0;
    ctx->nextOptId = 0;
    ctx->nextTernaryId = 0;
    ctx->nextLogicId = 0;
    ctx->currentScopeId = 0;
    ctx->nextScopeId = 1;
    clearDynArray(&ctx->scopes);
    pushOnDynArray(&ctx->scopes, &(ScopeInfo){.parent = 0, .hasDrops = false});
    ctx->varNames = newHashTable(sizeof(VarNameEntry), ctx->pool);
    // Params own their C names; locals that shadow a param get a suffix
    for (IrNode *p = func->function.params; p; p = p->next)
        makeUniqueVarName(ctx, p->funcParam.name);

    IrNode *entryBB = createBlock(ctx, "entry");
    IrNode *exitBB = makeIrBasicBlock(ctx->pool, NULL, "_L_exit");
    ctx->funcExitBlock = exitBB;

    setBlock(ctx, entryBB);
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, 0));

    bool hasRet = funcType->func.ret && funcType->func.ret->tag != irTypeVoid;
    if (hasRet) {
        ctx->funcRetVar = makeIrVarDecl(ctx->pool,
                                        &node->loc,
                                        flgReturned,
                                        "__ret",
                                        "__ret",
                                        funcType->func.ret,
                                        0);
        ctx->funcRetAstType = (node->type && typeIs(node->type, Func))
                                  ? node->type->func.retType
                                  : NULL;
        emitStmt(ctx, ctx->funcRetVar);
    }
    else {
        ctx->funcRetVar = NULL;
        ctx->funcRetAstType = NULL;
    }

    if (node->funcDecl.body) {
        if (nodeIs(node->funcDecl.body, BlockStmt)) {
            for (AstNode *stmt = node->funcDecl.body->blockStmt.stmts; stmt;
                 stmt = stmt->next) {
                astVisit(visitor, stmt);
            }
        }
        else {
            astVisit(visitor, node->funcDecl.body);
        }
    }

    emitBranchIfNotTerminated(ctx, exitBB);

    irAppendFunctionBlock(ctx->currentFunc, exitBB);
    setBlock(ctx, exitBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, 0));
    IrNode *retVal = ctx->funcRetVar
                         ? makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar)
                         : NULL;
    emitStmt(ctx, makeIrReturn(ctx->pool, &node->loc, retVal));

    if (ctx->module->module.functions) {
        IrNode *last = ctx->module->module.functions;
        while (last->next)
            last = last->next;
        last->next = func;
    }
    else {
        ctx->module->module.functions = func;
    }

    ctx->currentFunc = NULL;
    ctx->currentBlock = NULL;
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    if (node->externDecl.func && hasFlag(node->externDecl.func, Extern)) {
        astVisit(visitor, node->externDecl.func);
    }
}

static void visitClassOrStructDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    lowerType(ctx, node->type);
    AstNode *members = nodeIs(node, StructDecl) ? node->structDecl.members
                                                : node->classDecl.members;
    for (AstNode *m = members; m; m = m->next) {
        if (nodeIs(m, FuncDecl)) {
            astVisit(visitor, m);
        }
    }
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    cstring moduleName =
        node->program.module
            ? node->program.module->moduleDecl.name
            : (hasFlag(node, BuiltinsModule) ? S_builtins : S_main);
    ctx->module =
        makeIrModule(ctx->driver->pool, &node->loc, moduleName, ctx->types);

    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        astVisit(visitor, decl);
    }
}

static void finalizeLifecycleTypes(LowerContext *ctx)
{
    HashtableIt it = newHashTableIt(&ctx->types->types, sizeof(IrType *));
    while (hashTableItHasNext(&it)) {
        IrType *type = *(IrType **)hashTableItNext(&it);
        if (type && type->category == irTypeCatArray) {
            const AstNode *dAst = type->array.dctorAst;
            if (dAst != NULL) {
                csAssert(dAst->ir != NULL,
                         "array destructor '%s' was never lowered to IR",
                         getDeclarationName(dAst));
                type->array.dctor = dAst->ir;
            }
            else {
                type->array.dctor = NULL;
            }
        }
        else if (type && (type->category == irTypeCatStruct ||
                          type->category == irTypeCatTuple ||
                          type->category == irTypeCatUnion ||
                          type->category == irTypeCatClass)) {
            const AstNode *dAst = type->strct.dctorAst;
            const AstNode *fAst = type->strct.dctorFwdAst;
            const AstNode *cAst = type->strct.copyAst;

            if (dAst != NULL) {
                csAssert(dAst->ir != NULL,
                         "destructor '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(dAst),
                         type->name ?: "<anonymous>");
                type->strct.dctor = dAst->ir;
            }
            else {
                type->strct.dctor = NULL;
            }

            if (fAst != NULL) {
                csAssert(fAst->ir != NULL,
                         "destructor forward '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(fAst),
                         type->name ?: "<anonymous>");
                type->strct.dctorFwd = fAst->ir;
            }
            else {
                type->strct.dctorFwd = NULL;
            }

            if (cAst != NULL) {
                csAssert(cAst->ir != NULL,
                         "copy function '%s' for type '%s' was never lowered to IR",
                         getDeclarationName(cAst),
                         type->name ?: "<anonymous>");
                type->strct.copy = cAst->ir;
            }
            else {
                type->strct.copy = NULL;
            }
        }
    }
}

static void visitAsmStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    bool isVolatile = findAttribute(node, S_volatile) != NULL;
    for (AstNode *out = node->inlineAssembly.outputs; out; out = out->next) {
        if (out->asmOperand.operand)
            out->asmOperand.operand->codegen =
                lowerFlattenedOperand(visitor, out->asmOperand.operand);
    }
    for (AstNode *in = node->inlineAssembly.inputs; in; in = in->next) {
        if (in->asmOperand.operand)
            in->asmOperand.operand->codegen =
                lowerFlattenedOperand(visitor, in->asmOperand.operand);
    }
    IrNode *asmNode = makeIrInlineAsm(
        ctx->pool,
        &node->loc,
        node->inlineAssembly.text,
        NULL,
        NULL,
        NULL,
        isVolatile);
    asmNode->ast = node;
    emitStmt(ctx, asmNode);
}

IrModule *lowerToIr(CompilerDriver *driver, AstNode *node)
{
    AstNode *program = nodeIs(node, Metadata) ? node->metadata.node : node;
    csAssert0(program && nodeIs(program, Program));

    LowerContext context = {
        .driver = driver,
        .pool = driver->pool,
        .strings = driver->strings,
        .types = newIrTypeTable(driver->pool, driver->strings),
        .typeMap = newHashTable(sizeof(TypeMapping), driver->pool),
        .currentScopeId = 0,
        .nextScopeId = 1,
        .nextBlockId = 0,
        .tempCounter = 0,
        .scopes = newDynArray(sizeof(ScopeInfo)),
        .loopCtx = NULL};

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astNullLit] = visitNullLit,
        [astBoolLit] = visitBoolLit,
        [astCharLit] = visitCharLit,
        [astIntegerLit] = visitIntegerLit,
        [astFloatLit] = visitFloatLit,
        [astStringLit] = visitStringLit,
        [astIdentifier] = visitIdentifier,
        [astPath] = visitPath,
        [astMemberExpr] = visitMemberExpr,
        [astIndexExpr] = visitIndexExpr,
        [astSliceExpr] = visitSliceExpr,
        [astCatchExpr] = visitCatchExpr,
        [astYieldStmt] = visitYieldStmt,
        [astBinaryExpr] = visitBinaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astPointerOf] = visitPointerOf,
        [astReferenceOf] = visitPointerOf,
        [astCastExpr] = visitCastExpr,
        [astTypedExpr] = visitTypedExpr,
        [astCallExpr] = visitCallExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astGroupExpr] = visitGroupExpr,
        [astBackendCall] = visitBackendCall,
        [astArrayExpr] = visitArrayExpr,
        [astTupleExpr] = visitTupleExpr,
        [astStructExpr] = visitStructExpr,
        [astUnionValueExpr] = visitUnionValueExpr,
        [astClosureValue] = visitClosureValue,
        [astAliasExpr] = visitAliasExpr,
        [astVarDecl] = visitVarDecl,
        [astAssignExpr] = visitAssignExpr,
        [astExprStmt] = visitExprStmt,
        [astStmtExpr] = visitStmtExpr,
        [astBlockStmt] = visitBlockStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astForStmt] = visitForStmt,
        [astBreakStmt] = visitBreakStmt,
        [astContinueStmt] = visitContinueStmt,
        [astReturnStmt] = visitReturnStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astMatchStmt] = visitMatchStmt,
        [astAsm] = visitAsmStmt,
        [astFuncDecl] = visitFuncDecl,
        [astExternDecl] = visitExternDecl,
        [astStructDecl] = visitClassOrStructDecl,
        [astClassDecl] = visitClassOrStructDecl
    });
    // clang-format on

    pushOnDynArray(&context.scopes,
                   &(ScopeInfo){.parent = 0, .hasDrops = false});
    astVisit(&visitor, program);

    finalizeLifecycleTypes(&context);
    freeDynArray(&context.scopes);

    if (nodeIs(node, Metadata)) {
        node->metadata.ir = context.module;
    }

    return context.module;
}
