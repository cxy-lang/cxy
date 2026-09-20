#include "lower.h"

#include "core/hash.h"
#include "core/utils.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/mangle.h"

#include <string.h>

typedef struct TypeMapping {
    const Type *astType;
    const IrType *irType;
} TypeMapping;

static inline bool isBlockTerminated(const IrNode *bb)
{
    if (bb == NULL || bb->tag != irBasicBlock)
        return false;
    IrNode *last = bb->basicBlock.last;
    return last && (last->tag == irBranch || last->tag == irBranchIf ||
                    last->tag == irSwitch || last->tag == irReturn ||
                    last->tag == irUnreachable);
}

typedef struct VarNameEntry {
    cstring name;
    u32 count;
} VarNameEntry;

static cstring makeUniqueVarName(LowerContext *ctx, cstring displayName)
{
    if (displayName == NULL || displayName[0] == '_')
        return displayName;

    VarNameEntry *found = findInHashTable(
        &ctx->varNames, &(VarNameEntry){.name = displayName},
        hashPtr(hashInit(), displayName), sizeof(VarNameEntry), comparePointers);

    if (found == NULL) {
        VarNameEntry entry = {.name = displayName, .count = 1};
        insertInHashTable(&ctx->varNames, &entry, hashPtr(hashInit(), displayName),
                          sizeof(VarNameEntry), comparePointers);
        return displayName;
    }

    u32 suffix = found->count++;
    return makeStringf(ctx->strings, "%s_%u", displayName, suffix);
}

static inline void emitStmt(LowerContext *ctx, IrNode *stmt)
{
    if (ctx->currentBlock && stmt && !isBlockTerminated(ctx->currentBlock)) {
        irAppendBlockStmt(ctx->currentBlock, stmt);
    }
}

static inline IrNode *createBlock(LowerContext *ctx, cstring prefix)
{
    cstring name = makeStringf(ctx->strings, "_L%u_%s", ctx->nextBlockId++, prefix ?: "bb");
    IrNode *bb = makeIrBasicBlock(ctx->pool, NULL, name);
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

static const IrType *findCachedType(LowerContext *ctx, const Type *type)
{
    TypeMapping *found = findInHashTable(
        &ctx->typeMap, &(TypeMapping){.astType = type}, hashPtr(hashInit(), type),
        sizeof(TypeMapping), comparePointers);
    return found ? found->irType : NULL;
}

static void cacheType(LowerContext *ctx, const Type *type, const IrType *irType)
{
    insertInHashTable(
        &ctx->typeMap, &(TypeMapping){.astType = type, .irType = irType},
        hashPtr(hashInit(), type), sizeof(TypeMapping), comparePointers);
}

static cstring getIrTypeName(const IrType *type)
{
    if (type == NULL)
        return "void";
    if (type->name)
        return type->name;
    switch (type->tag) {
    case irTypeVoid: return "void";
    case irTypeBool: return "bool";
    case irTypeChar: return "char";
    case irTypeI8: return "i8";
    case irTypeI16: return "i16";
    case irTypeI32: return "i32";
    case irTypeI64: return "i64";
    case irTypeI128: return "i128";
    case irTypeU8: return "u8";
    case irTypeU16: return "u16";
    case irTypeU32: return "u32";
    case irTypeU64: return "u64";
    case irTypeU128: return "u128";
    case irTypeF32: return "f32";
    case irTypeF64: return "f64";
    case irTypePointer: return "ptr";
    case irTypeArray: return "arr";
    case irTypeStruct: return "struct";
    case irTypeUntaggedUnion: return "union";
    case irTypeFunc: return "func";
    default: return "val";
    }
}

static cstring makeDescriptiveTupleName(LowerContext *ctx, const Type *type, IrField *fields, u64 count)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "Tuple");
    for (u64 i = 0; i < count; i++) {
        cstring elemName = getIrTypeName(fields[i].type);
        if (len + (int)strlen(elemName) + 1 >= 64) {
            snprintf(buf + len, sizeof(buf) - len, "_%u", (unsigned)type->index);
            return makeString(ctx->strings, buf);
        }
        len += snprintf(buf + len, sizeof(buf) - len, "_%s", elemName);
    }
    return makeString(ctx->strings, buf);
}

static cstring makeDescriptiveOptionalName(LowerContext *ctx, const Type *type, const IrType *targetType)
{
    cstring targetName = getIrTypeName(targetType);
    char buf[128];
    if (strlen(targetName) < 50) {
        snprintf(buf, sizeof(buf), "Optional_%s", targetName);
    }
    else {
        snprintf(buf, sizeof(buf), "Optional_%.40s_%u", targetName, (unsigned)type->index);
    }
    return makeString(ctx->strings, buf);
}

static cstring makeDescriptiveResultName(LowerContext *ctx, const Type *type, const IrType *targetType)
{
    cstring targetName = getIrTypeName(targetType);
    char buf[128];
    if (strlen(targetName) < 50) {
        snprintf(buf, sizeof(buf), "Result_%s", targetName);
    }
    else {
        snprintf(buf, sizeof(buf), "Result_%.40s_%u", targetName, (unsigned)type->index);
    }
    return makeString(ctx->strings, buf);
}

static cstring makeDescriptiveTupleDisplay(LowerContext *ctx, IrField *fields, u64 count)
{
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "(");
    for (u64 i = 0; i < count; i++) {
        if (i > 0)
            len += snprintf(buf + len, sizeof(buf) - len, ", ");
        cstring dName = fields[i].type ? (fields[i].type->display ?: fields[i].type->name) : "void";
        len += snprintf(buf + len, sizeof(buf) - len, "%s", dName);
    }
    snprintf(buf + len, sizeof(buf) - len, ")");
    return makeString(ctx->strings, buf);
}

static cstring makeDescriptiveOptionalDisplay(LowerContext *ctx, const IrType *targetType)
{
    cstring targetDisplay = targetType ? (targetType->display ?: targetType->name) : "void";
    return makeStringf(ctx->strings, "%s?", targetDisplay);
}

static cstring makeDescriptiveResultDisplay(LowerContext *ctx, const IrType *targetType)
{
    cstring targetDisplay = targetType ? (targetType->display ?: targetType->name) : "void";
    return makeStringf(ctx->strings, "!%s", targetDisplay);
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
        case prtBool: result = getIrPrimitiveType(ctx->types, irTypeBool); break;
        case prtCChar: result = getIrPrimitiveType(ctx->types, irTypeChar); break;
        case prtChar: result = getIrPrimitiveType(ctx->types, irTypeI32); break;
        case prtI8: result = getIrPrimitiveType(ctx->types, irTypeI8); break;
        case prtI16: result = getIrPrimitiveType(ctx->types, irTypeI16); break;
        case prtI32: result = getIrPrimitiveType(ctx->types, irTypeI32); break;
        case prtI64: result = getIrPrimitiveType(ctx->types, irTypeI64); break;
        case prtI128: result = getIrPrimitiveType(ctx->types, irTypeI128); break;
        case prtU8: result = getIrPrimitiveType(ctx->types, irTypeU8); break;
        case prtU16: result = getIrPrimitiveType(ctx->types, irTypeU16); break;
        case prtU32: result = getIrPrimitiveType(ctx->types, irTypeU32); break;
        case prtU64: result = getIrPrimitiveType(ctx->types, irTypeU64); break;
        case prtU128: result = getIrPrimitiveType(ctx->types, irTypeU128); break;
        case prtF32: result = getIrPrimitiveType(ctx->types, irTypeF32); break;
        case prtF64: result = getIrPrimitiveType(ctx->types, irTypeF64); break;
        default: result = getIrPrimitiveType(ctx->types, irTypeI32); break;
        }
        break;
    case typVoid:
        result = getIrPrimitiveType(ctx->types, irTypeVoid);
        break;
    case typPointer: {
        const Type *pointed = type->pointer.pointed;
        if (isClassType(pointed))
            result = lowerType(ctx, pointed);
        else
            result = makeIrPointerType(ctx->types, lowerType(ctx, pointed));
        break;
    }
    case typReference: {
        const Type *referred = stripReference(type);
        if (isClassType(referred))
            result = lowerType(ctx, referred);
        else
            result = makeIrPointerType(ctx->types, lowerType(ctx, referred));
        break;
    }
    case typString:
        result = makeIrPointerType(ctx->types, getIrPrimitiveType(ctx->types, irTypeChar));
        break;
    case typArray:
        if (hasFlag(type, Slice) || type->array.len == UINT64_MAX) {
            const IrType *elemType = lowerType(ctx, type->array.elementType);
            IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
            fields[0] = (IrField){.name = "ptr", .type = makeIrPointerType(ctx->types, elemType), .bits = 0};
            fields[1] = (IrField){.name = "len", .type = getIrPrimitiveType(ctx->types, irTypeU64), .bits = 0};
            cstring elemName = getIrTypeName(elemType);
            cstring name = makeStringf(ctx->strings, "Slice_%s", elemName);
            cstring display = makeStringf(ctx->strings, "[%s]", elemType->display ?: elemName);
            result = makeIrStructType(ctx->types, name, display, fields, 2);
        }
        else {
            result = makeIrArrayType(ctx->types, lowerType(ctx, type->array.elementType), type->array.len);
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
        result = makeIrStructType(ctx->types, name, display, fields, count);
        break;
    }
    case typOptional: {
        const IrType *targetType = lowerType(ctx, type->optional.target);
        IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
        fields[0] = (IrField){.name = "ok", .type = getIrPrimitiveType(ctx->types, irTypeBool), .bits = 0};
        fields[1] = (IrField){.name = "val", .type = targetType, .bits = 0};
        cstring name = makeDescriptiveOptionalName(ctx, type, targetType);
        cstring display = makeDescriptiveOptionalDisplay(ctx, targetType);
        result = makeIrStructType(ctx->types, name, display, fields, 2);
        break;
    }
    case typStruct: {
        u64 count = type->tStruct.members ? type->tStruct.members->count : 0;
        u64 fieldCount = 0;
        for (u64 i = 0; i < count; i++) {
            if (nodeIs(type->tStruct.members->members[i].decl, FieldDecl))
                fieldCount++;
        }
        IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * (fieldCount ?: 1));
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
        cstring name = type->name ?: makeStringf(ctx->strings, "Struct%u", (unsigned)type->index);
        result = makeIrStructType(ctx->types, name, type->name, fields, fi);
        break;
    }
    case typClass: {
        u64 count = type->tClass.members ? type->tClass.members->count : 0;
        u64 fieldCount = 0;
        for (u64 i = 0; i < count; i++) {
            if (nodeIs(type->tClass.members->members[i].decl, FieldDecl))
                fieldCount++;
        }
        IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * (fieldCount ?: 1));
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
        cstring name = type->name ?: makeStringf(ctx->strings, "Class%u", (unsigned)type->index);
        const IrType *structType = makeIrStructType(ctx->types, name, type->name, fields, fi);
        result = makeIrPointerType(ctx->types, structType);
        break;
    }
    case typUntaggedUnion: {
        u64 count = type->untaggedUnion.members ? type->untaggedUnion.members->count : 0;
        IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * (count ?: 1));
        for (u64 i = 0; i < count; i++) {
            const NamedTypeMember *m = &type->untaggedUnion.members->members[i];
            fields[i].name = m->name;
            fields[i].type = lowerType(ctx, m->type);
            fields[i].bits = 0;
        }
        cstring name = type->name ?: makeStringf(ctx->strings, "Union%u", (unsigned)type->index);
        result = makeIrUntaggedUnionType(ctx->types, name, type->name, fields, count);
        break;
    }
    case typEnum:
        result = lowerType(ctx, type->tEnum.base ?: getPrimitiveType(ctx->driver->types, prtI32));
        break;
    case typFunc: {
        u64 count = type->func.paramsCount;
        const IrType **params = allocFromMemPool(ctx->pool, sizeof(IrType *) * (count ?: 1));
        for (u64 i = 0; i < count; i++) {
            params[i] = lowerType(ctx, type->func.params[i]);
        }
        const IrType *ret = lowerType(ctx, type->func.retType);
        bool isVariadic = hasFlag(type, Variadic) || (type->func.decl && hasFlag(type->func.decl, Variadic));
        result = makeIrFuncType(ctx->types, params, count, ret, isVariadic);
        break;
    }
    case typResult: {
        const IrType *targetType = lowerType(ctx, type->result.target);
        IrField *fields = allocFromMemPool(ctx->pool, sizeof(IrField) * 2);
        fields[0] = (IrField){.name = "tag", .type = getIrPrimitiveType(ctx->types, irTypeU32), .bits = 0};
        fields[1] = (IrField){.name = "val", .type = targetType, .bits = 0};
        cstring name = makeDescriptiveResultName(ctx, type, targetType);
        cstring display = makeDescriptiveResultDisplay(ctx, targetType);
        result = makeIrStructType(ctx->types, name, display, fields, 2);
        break;
    }
    default:
        result = getIrPrimitiveType(ctx->types, irTypeOpaque);
        break;
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
    default:
        return false;
    }
}

static IrNode *lowerFlattenedOperand(AstVisitor *visitor, AstNode *node)
{
    if (node == NULL)
        return NULL;
    astVisit(visitor, node);
    IrNode *val = (IrNode *)node->codegen;
    if (val == NULL)
        return NULL;

    if (isIrLValueOrConst(val))
        return val;

    LowerContext *ctx = getAstVisitorContext(visitor);
    cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    IrNode *tempVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, val->type, ctx->currentScopeId);
    emitStmt(ctx, tempVar);
    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, tempVar), val));
    return makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void lowerAggregateIntoTarget(LowerContext *ctx, AstVisitor *visitor, const AstNode *expr, IrNode *targetLValue)
{
    if (expr == NULL || targetLValue == NULL)
        return;

    if (nodeIs(expr, ArrayExpr)) {
        u64 idx = 0;
        for (AstNode *elem = expr->arrayExpr.elements; elem; elem = elem->next, idx++) {
            const IrType *elemType = lowerType(ctx, elem->type);
            IrNode *indexNode = makeIrIntConst(ctx->pool, &elem->loc, idx, false, getIrPrimitiveType(ctx->types, irTypeU64));
            IrNode *elemTarget = makeIrIndex(ctx->pool, &elem->loc, targetLValue, indexNode, elemType);
            if (nodeIs(elem, ArrayExpr) || nodeIs(elem, TupleExpr) || nodeIs(elem, StructExpr)) {
                lowerAggregateIntoTarget(ctx, visitor, elem, elemTarget);
            }
            else {
                IrNode *val = lowerFlattenedOperand(visitor, elem);
                emitStmt(ctx, makeIrAssign(ctx->pool, &elem->loc, elemTarget, val));
            }
        }
    }
    else if (nodeIs(expr, TupleExpr)) {
        u32 idx = 0;
        for (AstNode *elem = expr->tupleExpr.elements; elem; elem = elem->next, idx++) {
            const IrType *elemType = lowerType(ctx, elem->type);
            IrNode *elemTarget = makeIrMember(ctx->pool, &elem->loc, targetLValue, idx, NULL, elemType);
            if (nodeIs(elem, ArrayExpr) || nodeIs(elem, TupleExpr) || nodeIs(elem, StructExpr)) {
                lowerAggregateIntoTarget(ctx, visitor, elem, elemTarget);
            }
            else {
                IrNode *val = lowerFlattenedOperand(visitor, elem);
                emitStmt(ctx, makeIrAssign(ctx->pool, &elem->loc, elemTarget, val));
            }
        }
    }
    else if (nodeIs(expr, StructExpr)) {
        u32 idx = 0;
        for (AstNode *fld = expr->structExpr.fields; fld; fld = fld->next, idx++) {
            AstNode *valNode = fld->fieldExpr.value;
            const IrType *fldType = lowerType(ctx, valNode->type);
            IrNode *fldTarget = makeIrMember(ctx->pool, &fld->loc, targetLValue, idx, fld->fieldExpr.name, fldType);
            if (nodeIs(valNode, ArrayExpr) || nodeIs(valNode, TupleExpr) || nodeIs(valNode, StructExpr)) {
                lowerAggregateIntoTarget(ctx, visitor, valNode, fldTarget);
            }
            else {
                IrNode *val = lowerFlattenedOperand(visitor, valNode);
                emitStmt(ctx, makeIrAssign(ctx->pool, &fld->loc, fldTarget, val));
            }
        }
    }
}

static IrNode *lowerConditionOperand(LowerContext *ctx, AstVisitor *visitor, AstNode *condNode)
{
    const Type *astType = condNode->type;
    IrNode *cond = lowerFlattenedOperand(visitor, condNode);
    if (astType && (hasFlag(astType, Optional) || typeIs(astType, Optional))) {
        return makeIrMember(ctx->pool, &condNode->loc, cond, 0, "ok", getIrPrimitiveType(ctx->types, irTypeBool));
    }
    return cond;
}

static void lowerSliceIntoTarget(LowerContext *ctx, AstVisitor *visitor, const AstNode *expr, IrNode *targetLValue)
{
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const Type *exprAstType = expr->type;
    const IrType *elemIrType = targetLValue->type->strct.fields[0].type;
    u64 len = (exprAstType && isArrayType(exprAstType)) ? exprAstType->array.len : 0;

    IrNode *ptrTarget = makeIrMember(ctx->pool, &targetLValue->loc, targetLValue, 0, "ptr", elemIrType);
    IrNode *lenTarget = makeIrMember(ctx->pool, &targetLValue->loc, targetLValue, 1, "len", u64Type);

    if (nodeIs(expr, ArrayExpr)) {
        const IrType *fixedArrType = makeIrArrayType(ctx->types, elemIrType->pointer.pointee, len);
        cstring arrTempName = makeStringf(ctx->strings, "__arr%u", ctx->tempCounter++);
        IrNode *arrVar = makeIrVarDecl(ctx->pool, &expr->loc, flgNone, arrTempName, arrTempName, fixedArrType, ctx->currentScopeId);
        emitStmt(ctx, arrVar);

        u64 idx = 0;
        for (AstNode *elem = expr->arrayExpr.elements; elem; elem = elem->next, idx++) {
            IrNode *val = lowerFlattenedOperand(visitor, elem);
            IrNode *idxNode = makeIrIntConst(ctx->pool, &elem->loc, idx, false, u64Type);
            IrNode *elemSlot = makeIrIndex(ctx->pool, &elem->loc, makeIrVarRef(ctx->pool, &expr->loc, arrVar), idxNode, val->type);
            emitStmt(ctx, makeIrAssign(ctx->pool, &elem->loc, elemSlot, val));
        }

        IrNode *ptrVal = makeIrVarRef(ctx->pool, &expr->loc, arrVar);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, ptrTarget, ptrVal));
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, lenTarget, makeIrIntConst(ctx->pool, &expr->loc, len, false, u64Type)));
    }
    else {
        IrNode *arrVal = lowerFlattenedOperand(visitor, (AstNode *)expr);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, ptrTarget, arrVal));
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, lenTarget, makeIrIntConst(ctx->pool, &expr->loc, len, false, u64Type)));
    }
}

static void lowerOptionalIntoTarget(LowerContext *ctx, AstVisitor *visitor, const AstNode *expr, IrNode *targetLValue)
{
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
    IrNode *okTarget = makeIrMember(ctx->pool, &targetLValue->loc, targetLValue, 0, "ok", boolType);

    if (expr == NULL || nodeIs(expr, NullLit)) {
        emitStmt(ctx, makeIrAssign(ctx->pool, &targetLValue->loc, okTarget, makeIrBoolConst(ctx->pool, &targetLValue->loc, false, boolType)));
        return;
    }

    emitStmt(ctx, makeIrAssign(ctx->pool, &targetLValue->loc, okTarget, makeIrBoolConst(ctx->pool, &targetLValue->loc, true, boolType)));
    const IrType *valType = lowerType(ctx, expr->type);
    IrNode *valTarget = makeIrMember(ctx->pool, &expr->loc, targetLValue, 1, "val", valType);

    if (nodeIs(expr, ArrayExpr) || nodeIs(expr, TupleExpr) || nodeIs(expr, StructExpr)) {
        lowerAggregateIntoTarget(ctx, visitor, expr, valTarget);
    }
    else {
        IrNode *val = lowerFlattenedOperand(visitor, (AstNode *)expr);
        emitStmt(ctx, makeIrAssign(ctx->pool, &expr->loc, valTarget, val));
    }
}

static void visitNullLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrNullConst(ctx->pool, &node->loc, lowerType(ctx, node->type));
}

static void visitBoolLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrBoolConst(ctx->pool, &node->loc, node->boolLiteral.value, lowerType(ctx, node->type));
}

static void visitCharLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrCharConst(ctx->pool, &node->loc, node->charLiteral.value, lowerType(ctx, node->type));
}

static void visitIntegerLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrIntConst(ctx->pool, &node->loc, node->intLiteral.uValue, node->intLiteral.isNegative, lowerType(ctx, node->type));
}

static void visitFloatLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrFloatConst(ctx->pool, &node->loc, node->floatLiteral.value, lowerType(ctx, node->type));
}

static void visitStringLit(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    node->codegen = makeIrStringConst(ctx->pool, &node->loc, node->stringLiteral.value, lowerType(ctx, node->type));
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    const AstNode *decl = nodeIs(node, Identifier)
                              ? node->ident.resolvesTo
                              : (node->path.elements ? node->path.elements->pathElement.resolvesTo : NULL);
    if (decl && decl->ir) {
        if (decl->ir->tag == irVarDecl || decl->ir->tag == irFuncParam || decl->ir->tag == irGlobalVar) {
            node->codegen = makeIrVarRef(ctx->pool, &node->loc, decl->ir);
            return;
        }
        if (decl->ir->tag == irFunction) {
            node->codegen = makeIrGlobalRef(ctx->pool, &node->loc, decl->ir->function.name, decl->ir, decl->ir->type);
            return;
        }
    }
    cstring name = nodeIs(node, Identifier) ? node->ident.value : node->path.elements->pathElement.name;
    node->codegen = makeIrGlobalRef(ctx->pool, &node->loc, name, NULL, irType);
}

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *target = lowerFlattenedOperand(visitor, node->memberExpr.target);
    AstNode *mem = node->memberExpr.member;
    u32 index = 0;
    cstring name = NULL;
    if (mem) {
        if (nodeIs(mem, IntegerLit)) {
            index = (u32)mem->intLiteral.uValue;
        }
        else {
            csAssert(nodeIs(mem, Identifier), "member should be an identifier");
            name = mem->ident.value;
        }
    }
    node->codegen = makeIrMember(ctx->pool, &node->loc, target, index, name, lowerType(ctx, node->type));
}

static void visitIndexExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *target = lowerFlattenedOperand(visitor, node->indexExpr.target);
    IrNode *index = lowerFlattenedOperand(visitor, node->indexExpr.index);
    const Type *targetAstType = node->indexExpr.target->type;

    if (targetAstType && isSliceType(targetAstType)) {
        IrNode *ptrTarget = makeIrMember(ctx->pool, &node->loc, target, 0, "ptr", target->type->strct.fields[0].type);
        node->codegen = makeIrIndex(ctx->pool, &node->loc, ptrTarget, index, lowerType(ctx, node->type));
        return;
    }

    node->codegen = makeIrIndex(ctx->pool, &node->loc, target, index, lowerType(ctx, node->type));
}

static void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->binaryExpr.op;

    if (op == opLAnd) {
        const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
        IrNode *lhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.lhs);

        cstring condName = makeStringf(ctx->strings, "__cond%u", ctx->tempCounter++);
        IrNode *condVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, condName, condName, boolType, ctx->currentScopeId);
        emitStmt(ctx, condVar);

        IrNode *rhsBB = createBlock(ctx, "and_rhs");
        IrNode *falseBB = createBlock(ctx, "and_false");
        IrNode *mergeBB = createBlock(ctx, "and_merge");

        emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, lhs, rhsBB, falseBB));

        setBlock(ctx, rhsBB);
        IrNode *rhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.rhs);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, condVar), rhs));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, falseBB);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, condVar),
                                   makeIrBoolConst(ctx->pool, &node->loc, false, boolType)));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, condVar);
        return;
    }

    if (op == opLOr) {
        const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);
        IrNode *lhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.lhs);

        cstring condName = makeStringf(ctx->strings, "__cond%u", ctx->tempCounter++);
        IrNode *condVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, condName, condName, boolType, ctx->currentScopeId);
        emitStmt(ctx, condVar);

        IrNode *trueBB = createBlock(ctx, "or_true");
        IrNode *rhsBB = createBlock(ctx, "or_rhs");
        IrNode *mergeBB = createBlock(ctx, "or_merge");

        emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, lhs, trueBB, rhsBB));

        setBlock(ctx, trueBB);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, condVar),
                                   makeIrBoolConst(ctx->pool, &node->loc, true, boolType)));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, rhsBB);
        IrNode *rhs = lowerConditionOperand(ctx, visitor, node->binaryExpr.rhs);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, condVar), rhs));
        emitBranchIfNotTerminated(ctx, mergeBB);

        setBlock(ctx, mergeBB);
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, condVar);
        return;
    }

    IrNode *lhs = lowerFlattenedOperand(visitor, node->binaryExpr.lhs);
    IrNode *rhs = lowerFlattenedOperand(visitor, node->binaryExpr.rhs);
    node->codegen = makeIrBinary(ctx->pool, &node->loc, op, lhs, rhs, lowerType(ctx, node->type));
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *operand = lowerFlattenedOperand(visitor, node->unaryExpr.operand);
    const IrType *irType = lowerType(ctx, node->type);
    if (node->unaryExpr.op == opPtrof || node->unaryExpr.op == opRefof) {
        const Type *opType = node->unaryExpr.operand->type;
        if (opType && (isClassType(opType) || (node->unaryExpr.op == opRefof && isReferenceType(opType))))
            node->codegen = operand;
        else
            node->codegen = makeIrAddrOf(ctx->pool, &node->loc, operand, irType);
    }
    else if (node->unaryExpr.op == opMove)
        node->codegen = makeIrMove(ctx->pool, &node->loc, operand);
    else if (node->unaryExpr.op == opDeref) {
        const Type *opType = node->unaryExpr.operand->type;
        if (opType && (hasFlag(opType, Optional) || typeIs(opType, Optional)))
            node->codegen = makeIrMember(ctx->pool, &node->loc, operand, 1, "val", irType);
        else
            node->codegen = makeIrDeref(ctx->pool, &node->loc, operand, irType);
    }
    else
        node->codegen = makeIrUnary(ctx->pool, &node->loc, node->unaryExpr.op, operand, irType);
}

static void visitPointerOf(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *operand = lowerFlattenedOperand(visitor, node->unaryExpr.operand);
    const Type *opType = node->unaryExpr.operand->type;
    if (opType && (isClassType(opType) || ((nodeIs(node, ReferenceOf) || node->unaryExpr.op == opRefof) && isReferenceType(opType)))) {
        node->codegen = operand;
        return;
    }
    node->codegen = makeIrAddrOf(ctx->pool, &node->loc, operand, lowerType(ctx, node->type));
}

static void visitCastExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *targetType = node->type;
    const Type *srcType = node->castExpr.expr->type;

    if (targetType && isSliceType(targetType) && srcType && isArrayType(srcType)) {
        const IrType *sliceIrType = lowerType(ctx, targetType);
        cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
        IrNode *sliceTemp = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, sliceIrType, ctx->currentScopeId);
        emitStmt(ctx, sliceTemp);
        lowerSliceIntoTarget(ctx, visitor, node->castExpr.expr, makeIrVarRef(ctx->pool, &node->loc, sliceTemp));
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, sliceTemp);
        return;
    }

    IrNode *expr = lowerFlattenedOperand(visitor, node->castExpr.expr);
    node->codegen = makeIrCast(ctx->pool, &node->loc, expr, lowerType(ctx, node->type));
}

static void visitTypedExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *targetType = node->type;
    const Type *srcType = node->typedExpr.expr->type;

    if (targetType && isSliceType(targetType) && srcType && isArrayType(srcType)) {
        const IrType *sliceIrType = lowerType(ctx, targetType);
        cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
        IrNode *sliceTemp = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, sliceIrType, ctx->currentScopeId);
        emitStmt(ctx, sliceTemp);
        lowerSliceIntoTarget(ctx, visitor, node->typedExpr.expr, makeIrVarRef(ctx->pool, &node->loc, sliceTemp));
        node->codegen = makeIrVarRef(ctx->pool, &node->loc, sliceTemp);
        return;
    }

    IrNode *expr = lowerFlattenedOperand(visitor, node->typedExpr.expr);
    node->codegen = makeIrCast(ctx->pool, &node->loc, expr, lowerType(ctx, node->type));
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->callExpr.callee);
    IrNode *callee = (IrNode *)node->callExpr.callee->codegen;

    const AstNode *calleeNode = node->callExpr.callee;
    const Type *calleeType = resolveUnThisUnwrapType(calleeNode->type);
    if (calleeType == NULL) {
        const AstNode *calleeDecl = nodeIs(calleeNode, Identifier)
                                        ? calleeNode->ident.resolvesTo
                                        : (calleeNode->path.elements ? calleeNode->path.elements->pathElement.resolvesTo : NULL);
        if (calleeDecl && calleeDecl->type)
            calleeType = resolveUnThisUnwrapType(calleeDecl->type);
    }
    if (calleeType && typeIs(calleeType, Pointer))
        calleeType = resolveUnThisUnwrapType(calleeType->pointer.pointed);
    u64 argsCount = countAstNodes(node->callExpr.args);
    IrNode *argsFirst = NULL, *argsLast = NULL;
    u64 argIdx = 0;
    for (AstNode *arg = node->callExpr.args; arg; arg = arg->next, argIdx++) {
        const Type *paramAstType = (calleeType && typeIs(calleeType, Func) && argIdx < calleeType->func.paramsCount)
                                       ? calleeType->func.params[argIdx]
                                       : NULL;
        IrNode *loweredArg = NULL;
        if (paramAstType && isSliceType(paramAstType) && arg->type && isArrayType(arg->type) && !isSliceType(arg->type)) {
            const IrType *sliceIrType = lowerType(ctx, paramAstType);
            cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
            IrNode *sliceTemp = makeIrVarDecl(ctx->pool, &arg->loc, flgNone, tempName, tempName, sliceIrType, ctx->currentScopeId);
            emitStmt(ctx, sliceTemp);
            lowerSliceIntoTarget(ctx, visitor, arg, makeIrVarRef(ctx->pool, &arg->loc, sliceTemp));
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
    node->codegen = makeIrCall(ctx->pool, &node->loc, callee, argsFirst, argsCount, lowerType(ctx, node->type));
}

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *irType = lowerType(ctx, node->type);
    astVisit(visitor, node->ternaryExpr.cond);
    IrNode *cond = (IrNode *)node->ternaryExpr.cond->codegen;

    cstring tempName = makeStringf(ctx->strings, "__ternary_%u", ctx->tempCounter++);
    IrNode *tempVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, irType, ctx->currentScopeId);
    emitStmt(ctx, tempVar);

    IrNode *thenBB = createBlock(ctx, "ternary_then");
    IrNode *elseBB = createBlock(ctx, "ternary_else");
    IrNode *mergeBB = createBlock(ctx, "ternary_merge");

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB));

    setBlock(ctx, thenBB);
    astVisit(visitor, node->ternaryExpr.body);
    IrNode *thenVal = (IrNode *)node->ternaryExpr.body->codegen;
    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, tempVar), thenVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, elseBB);
    astVisit(visitor, node->ternaryExpr.otherwise);
    IrNode *elseVal = (IrNode *)node->ternaryExpr.otherwise->codegen;
    emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, tempVar), elseVal));
    emitBranchIfNotTerminated(ctx, mergeBB);

    setBlock(ctx, mergeBB);
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *arrType = lowerType(ctx, node->type);
    cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    IrNode *tempVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, arrType, ctx->currentScopeId);
    emitStmt(ctx, tempVar);
    lowerAggregateIntoTarget(ctx, visitor, node, makeIrVarRef(ctx->pool, &node->loc, tempVar));
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *tupType = lowerType(ctx, node->type);
    cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    IrNode *tempVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, tupType, ctx->currentScopeId);
    emitStmt(ctx, tempVar);
    lowerAggregateIntoTarget(ctx, visitor, node, makeIrVarRef(ctx->pool, &node->loc, tempVar));
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *strctType = lowerType(ctx, node->type);
    cstring tempName = makeStringf(ctx->strings, "__t%u", ctx->tempCounter++);
    IrNode *tempVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, tempName, tempName, strctType, ctx->currentScopeId);
    emitStmt(ctx, tempVar);
    lowerAggregateIntoTarget(ctx, visitor, node, makeIrVarRef(ctx->pool, &node->loc, tempVar));
    node->codegen = makeIrVarRef(ctx->pool, &node->loc, tempVar);
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
        IrNode *sizeNode = makeIrSizeof(ctx->pool, &node->loc, elemIrType);
        AstNode *dctorArg = args ? args->next : NULL;
        IrNode *dctorNode = dctorArg ? lowerFlattenedOperand(visitor, dctorArg)
                                     : makeIrNullConst(ctx->pool, &node->loc, getIrPrimitiveType(ctx->types, irTypePointer));
        sizeNode->next = dctorNode;

        IrNode *callee = makeIrGlobalRef(ctx->pool, &node->loc, "__smart_ptr_alloc", NULL,
                                         makeIrPointerType(ctx->types, getIrPrimitiveType(ctx->types, irTypeVoid)));
        node->codegen = makeIrCall(ctx->pool, &node->loc, callee, sizeNode, 2, lowerType(ctx, node->type));
        break;
    }
    case bfiZeromem: {
        IrNode *target = lowerFlattenedOperand(visitor, args);
        node->codegen = makeIrZeromem(ctx->pool, &node->loc, target);
        break;
    }
    case bfiSizeOf: {
        const IrType *targetType = lowerType(ctx, args->type);
        node->codegen = makeIrSizeof(ctx->pool, &node->loc, targetType);
        break;
    }
    case bfiAlloca: {
        const IrType *elemType = lowerType(ctx, args->type);
        IrNode *count = args->next ? lowerFlattenedOperand(visitor, args->next) : NULL;
        node->codegen = makeIrAlloca(ctx->pool, &node->loc, elemType, count);
        break;
    }
    case bfiCopy: {
        IrNode *src = lowerFlattenedOperand(visitor, args);
        IrNode *dst = lowerFlattenedOperand(visitor, args->next);
        node->codegen = makeIrCopy(ctx->pool, &node->loc, src, dst);
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

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *varAstType = (node->varDecl.names && node->varDecl.names->type)
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
        IrNode *glob = makeIrGlobalVar(ctx->driver->pool, &node->loc, node->flags, name, irType, init);
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
    IrNode *varDecl = makeIrVarDecl(ctx->pool, &node->loc, node->flags, cName, displayName, irType, ctx->currentScopeId);
    node->ir = varDecl;
    if (node->varDecl.names)
        node->varDecl.names->ir = varDecl;
    emitStmt(ctx, varDecl);

    if (node->varDecl.init) {
        AstNode *initNode = node->varDecl.init;
        if (hasFlag(varAstType, Slice) || isSliceType(varAstType)) {
            lowerSliceIntoTarget(ctx, visitor, initNode, makeIrVarRef(ctx->pool, &node->loc, varDecl));
        }
        else if (hasFlag(varAstType, Optional) || typeIs(varAstType, Optional)) {
            lowerOptionalIntoTarget(ctx, visitor, initNode, makeIrVarRef(ctx->pool, &node->loc, varDecl));
        }
        else if (nodeIs(initNode, ArrayExpr) || nodeIs(initNode, TupleExpr) || nodeIs(initNode, StructExpr)) {
            lowerAggregateIntoTarget(ctx, visitor, initNode, makeIrVarRef(ctx->pool, &node->loc, varDecl));
        }
        else {
            IrNode *val = lowerFlattenedOperand(visitor, initNode);
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, varDecl), val));
        }
    }
    else if (hasFlag(varAstType, Optional) || typeIs(varAstType, Optional)) {
        lowerOptionalIntoTarget(ctx, visitor, NULL, makeIrVarRef(ctx->pool, &node->loc, varDecl));
    }
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *lhs = lowerFlattenedOperand(visitor, node->assignExpr.lhs);
    AstNode *rhsNode = node->assignExpr.rhs;
    const Type *lhsAstType = node->assignExpr.lhs->type;
    const Type *rhsAstType = rhsNode->type;

    if (lhsAstType && (hasFlag(lhsAstType, Slice) || isSliceType(lhsAstType)) &&
        (!rhsAstType || (!hasFlag(rhsAstType, Slice) && !isSliceType(rhsAstType)))) {
        lowerSliceIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else if (lhsAstType && (hasFlag(lhsAstType, Optional) || typeIs(lhsAstType, Optional)) &&
        (!rhsAstType || (!hasFlag(rhsAstType, Optional) && !typeIs(rhsAstType, Optional)))) {
        lowerOptionalIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else if (nodeIs(rhsNode, ArrayExpr) || nodeIs(rhsNode, TupleExpr) || nodeIs(rhsNode, StructExpr)) {
        lowerAggregateIntoTarget(ctx, visitor, rhsNode, lhs);
    }
    else {
        IrNode *rhs = lowerFlattenedOperand(visitor, rhsNode);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, lhs, rhs));
    }
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->exprStmt.expr);
    IrNode *expr = (IrNode *)node->exprStmt.expr->codegen;
    if (expr)
        emitStmt(ctx, expr);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    u32 scopeId = ctx->nextScopeId++;
    u32 prevScopeId = ctx->currentScopeId;
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
    IrNode *thenBB = createBlock(ctx, "if_then");
    IrNode *elseBB = node->ifStmt.otherwise ? createBlock(ctx, "if_else") : NULL;
    IrNode *mergeBB = createBlock(ctx, "if_end");

    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, thenBB, elseBB ?: mergeBB));

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
    IrNode *condBB = createBlock(ctx, "while_cond");
    IrNode *bodyBB = createBlock(ctx, "while_body");
    IrNode *exitBB = createBlock(ctx, "while_exit");

    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, condBB);
    IrNode *cond = lowerConditionOperand(ctx, visitor, node->whileStmt.cond);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = condBB, .exitBlock = exitBB, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->whileStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

static void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const Type *rangeAstType = node->forStmt.range->type;
    const IrType *u64Type = getIrPrimitiveType(ctx->types, irTypeU64);
    const IrType *boolType = getIrPrimitiveType(ctx->types, irTypeBool);

    if (nodeIs(node->forStmt.range, RangeExpr)) {
        AstNode *startNode = node->forStmt.range->rangeExpr.start;
        AstNode *endNode = node->forStmt.range->rangeExpr.end;
        AstNode *stepNode = node->forStmt.range->rangeExpr.step;

        IrNode *startVal = lowerFlattenedOperand(visitor, startNode);
        IrNode *endVal = lowerFlattenedOperand(visitor, endNode);
        const IrType *varType = startVal ? startVal->type : u64Type;

        AstNode *loopVarNode = node->forStmt.var;
        cstring displayName = getDeclarationName(loopVarNode);
        cstring cName = makeUniqueVarName(ctx, displayName);
        IrNode *varDecl = makeIrVarDecl(ctx->pool, &loopVarNode->loc, loopVarNode->flags, cName, displayName, varType, ctx->currentScopeId);
        loopVarNode->ir = varDecl;
        emitStmt(ctx, varDecl);
        emitStmt(ctx, makeIrAssign(ctx->pool, &startNode->loc, makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl), startVal));

        IrNode *condBB = createBlock(ctx, "for_cond");
        IrNode *bodyBB = createBlock(ctx, "for_body");
        IrNode *exitBB = createBlock(ctx, "for_exit");

        emitBranchIfNotTerminated(ctx, condBB);
        setBlock(ctx, condBB);

        IrNode *cond = makeIrBinary(ctx->pool, &node->loc, opLt, makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl), endVal, boolType);
        emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

        setBlock(ctx, bodyBB);
        LoopCtx loop = {.condBlock = condBB, .exitBlock = exitBB, .prev = ctx->loopCtx};
        ctx->loopCtx = &loop;
        astVisit(visitor, node->forStmt.body);
        ctx->loopCtx = loop.prev;

        IrNode *stepVal = stepNode ? lowerFlattenedOperand(visitor, stepNode) : makeIrIntConst(ctx->pool, &node->loc, 1, false, varType);
        IrNode *nextVal = makeIrBinary(ctx->pool, &node->loc, opAdd, makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl), stepVal, varType);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &loopVarNode->loc, varDecl), nextVal));
        emitBranchIfNotTerminated(ctx, condBB);

        setBlock(ctx, exitBB);
        return;
    }

    if (rangeAstType && (isArrayType(rangeAstType) || isSliceType(rangeAstType))) {
        IrNode *arr = lowerFlattenedOperand(visitor, node->forStmt.range);
        bool isSlice = isSliceType(rangeAstType);
        IrNode *lenVal = isSlice
                             ? makeIrMember(ctx->pool, &node->loc, arr, 1, "len", u64Type)
                             : makeIrIntConst(ctx->pool, &node->loc, rangeAstType->array.len, false, u64Type);
        IrNode *dataTarget = isSlice
                                 ? makeIrMember(ctx->pool, &node->loc, arr, 0, "ptr", arr->type->strct.fields[0].type)
                                 : arr;

        cstring idxName = makeStringf(ctx->strings, "__idx%u", ctx->tempCounter++);
        IrNode *idxVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, idxName, idxName, u64Type, ctx->currentScopeId);
        emitStmt(ctx, idxVar);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, idxVar), makeIrIntConst(ctx->pool, &node->loc, 0, false, u64Type)));

        IrNode *condBB = createBlock(ctx, "for_cond");
        IrNode *bodyBB = createBlock(ctx, "for_body");
        IrNode *exitBB = createBlock(ctx, "for_exit");

        emitBranchIfNotTerminated(ctx, condBB);
        setBlock(ctx, condBB);

        IrNode *cond = makeIrBinary(ctx->pool, &node->loc, opLt, makeIrVarRef(ctx->pool, &node->loc, idxVar), lenVal, boolType);
        emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

        setBlock(ctx, bodyBB);
        AstNode *elemVarNode = node->forStmt.var;
        const IrType *elemType = lowerType(ctx, elemVarNode->type);
        cstring elemDisplayName = getDeclarationName(elemVarNode);
        cstring elemCName = makeUniqueVarName(ctx, elemDisplayName);
        IrNode *elemVar = makeIrVarDecl(ctx->pool, &elemVarNode->loc, elemVarNode->flags, elemCName, elemDisplayName, elemType, ctx->currentScopeId);
        elemVarNode->ir = elemVar;
        emitStmt(ctx, elemVar);

        IrNode *elemVal = makeIrIndex(ctx->pool, &elemVarNode->loc, dataTarget, makeIrVarRef(ctx->pool, &node->loc, idxVar), elemType);
        emitStmt(ctx, makeIrAssign(ctx->pool, &elemVarNode->loc, makeIrVarRef(ctx->pool, &elemVarNode->loc, elemVar), elemVal));

        if (elemVarNode->next) {
            AstNode *idxUserVar = elemVarNode->next;
            const IrType *i64Type = getIrPrimitiveType(ctx->types, irTypeI64);
            cstring iDisplayName = getDeclarationName(idxUserVar);
            cstring iCName = makeUniqueVarName(ctx, iDisplayName);
            IrNode *iVar = makeIrVarDecl(ctx->pool, &idxUserVar->loc, idxUserVar->flags, iCName, iDisplayName, i64Type, ctx->currentScopeId);
            idxUserVar->ir = iVar;
            emitStmt(ctx, iVar);
            IrNode *castIdx = makeIrCast(ctx->pool, &idxUserVar->loc, makeIrVarRef(ctx->pool, &node->loc, idxVar), i64Type);
            emitStmt(ctx, makeIrAssign(ctx->pool, &idxUserVar->loc, makeIrVarRef(ctx->pool, &idxUserVar->loc, iVar), castIdx));
        }

        LoopCtx loop = {.condBlock = condBB, .exitBlock = exitBB, .prev = ctx->loopCtx};
        ctx->loopCtx = &loop;
        astVisit(visitor, node->forStmt.body);
        ctx->loopCtx = loop.prev;

        IrNode *nextIdx = makeIrBinary(ctx->pool, &node->loc, opAdd, makeIrVarRef(ctx->pool, &node->loc, idxVar), makeIrIntConst(ctx->pool, &node->loc, 1, false, u64Type), u64Type);
        emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, makeIrVarRef(ctx->pool, &node->loc, idxVar), nextIdx));
        emitBranchIfNotTerminated(ctx, condBB);

        setBlock(ctx, exitBB);
        return;
    }

    IrNode *condBB = createBlock(ctx, "for_cond");
    IrNode *bodyBB = createBlock(ctx, "for_body");
    IrNode *exitBB = createBlock(ctx, "for_exit");

    emitBranchIfNotTerminated(ctx, condBB);
    setBlock(ctx, condBB);

    IrNode *cond = lowerConditionOperand(ctx, visitor, node->forStmt.range);
    emitStmt(ctx, makeIrBranchIf(ctx->pool, &node->loc, cond, bodyBB, exitBB));

    setBlock(ctx, bodyBB);
    LoopCtx loop = {.condBlock = condBB, .exitBlock = exitBB, .prev = ctx->loopCtx};
    ctx->loopCtx = &loop;
    astVisit(visitor, node->forStmt.body);
    ctx->loopCtx = loop.prev;
    emitBranchIfNotTerminated(ctx, condBB);

    setBlock(ctx, exitBB);
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->exitBlock)
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->exitBlock));
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (ctx->loopCtx && ctx->loopCtx->condBlock)
        emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, ctx->loopCtx->condBlock));
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (node->returnStmt.expr && ctx->funcRetVar) {
        AstNode *retExpr = node->returnStmt.expr;
        IrNode *retVarRef = makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar);
        if (nodeIs(retExpr, ArrayExpr) || nodeIs(retExpr, TupleExpr) || nodeIs(retExpr, StructExpr)) {
            lowerAggregateIntoTarget(ctx, visitor, retExpr, retVarRef);
        }
        else {
            astVisit(visitor, retExpr);
            IrNode *val = (IrNode *)retExpr->codegen;
            emitStmt(ctx, makeIrAssign(ctx->pool, &node->loc, retVarRef, val));
        }
    }
    if (ctx->currentScopeId > 0)
        emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, ctx->currentScopeId));
    emitStmt(ctx, makeIrBranch(ctx->pool, &node->loc, ctx->funcExitBlock));
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    IrNode *cond = lowerFlattenedOperand(visitor, node->switchStmt.cond);
    IrNode *exitBB = createBlock(ctx, "switch_exit");

    u64 totalCases = 0;
    for (const AstNode *c = node->switchStmt.cases; c; c = c->next) {
        if (c->caseStmt.match)
            totalCases += countAstNodes(c->caseStmt.match);
        else
            totalCases++;
    }

    IrCase *cases = allocFromMemPool(ctx->pool, sizeof(IrCase) * (totalCases ?: 1));
    u64 caseIdx = 0;
    IrNode *defaultBB = NULL;

    for (AstNode *c = node->switchStmt.cases; c; c = c->next) {
        IrNode *caseBB = createBlock(ctx, "switch_case");
        if (c->caseStmt.match == NULL) {
            defaultBB = caseBB;
        }
        else {
            for (const AstNode *m = c->caseStmt.match; m; m = m->next) {
                u64 val = nodeIs(m, IntegerLit) ? m->intLiteral.uValue : (nodeIs(m, CharLit) ? m->charLiteral.value : 0);
                cases[caseIdx++] = (IrCase){.value = val, .target = caseBB};
            }
        }

        IrNode *savedBlock = ctx->currentBlock;
        setBlock(ctx, caseBB);
        LoopCtx loop = {.condBlock = NULL, .exitBlock = exitBB, .prev = ctx->loopCtx};
        ctx->loopCtx = &loop;
        astVisit(visitor, c->caseStmt.body);
        ctx->loopCtx = loop.prev;
        emitBranchIfNotTerminated(ctx, exitBB);
        ctx->currentBlock = savedBlock;
    }

    IrNode *switchNode = makeIrNode(ctx->pool, &node->loc, flgNone, irSwitch, NULL);
    switchNode->switchStmt.cond = cond;
    switchNode->switchStmt.cases = cases;
    switchNode->switchStmt.casesCount = caseIdx;
    switchNode->switchStmt.defaultTarget = defaultBB ?: exitBB;
    emitStmt(ctx, switchNode);

    setBlock(ctx, exitBB);
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Extern))
        return;

    const IrType *funcType = lowerType(ctx, node->type);
    u64 paramsCount = countAstNodes(node->funcDecl.signature->params);
    IrNode *paramsFirst = NULL, *paramsLast = NULL;
    u32 pidx = 0;
    for (AstNode *p = node->funcDecl.signature->params; p; p = p->next, pidx++) {
        const IrType *pType = lowerType(ctx, p->type);
        cstring pname = p->funcParam.name ?: makeStringf(ctx->strings, "p%u", pidx);
        IrNode *param = makeIrFuncParam(ctx->pool, &p->loc, p->flags, pname, pidx, pType);
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
    cstring cName = funcName;
    cstring displayName = funcName;

    if (parent && isClassOrStructAstNode(parent)) {
        cstring typeName = getDeclarationName(parent);
        cName = makeStringf(ctx->strings, "%s_%s_%s", modName, typeName, funcName);
        displayName = makeStringf(ctx->strings, "%s.%s.%s", modName, typeName, funcName);
    }
    else if (hasFlag(node, Main) || funcName == S_main) {
        cName = S__main;
        displayName = S_main;
    }
    else if (!hasFlag(node, Extern) && modName && modName != S_main) {
        cName = makeStringf(ctx->strings, "%s_%s", modName, funcName);
        displayName = makeStringf(ctx->strings, "%s.%s", modName, funcName);
    }

    IrNode *func = makeIrFunction(ctx->pool, &node->loc, node->flags, cName, displayName, funcType, paramsFirst, paramsCount);
    node->ir = func;

    ctx->currentFunc = func;
    ctx->nextBlockId = 0;
    ctx->currentScopeId = 0;
    ctx->nextScopeId = 1;
    ctx->varNames = newHashTable(sizeof(VarNameEntry), ctx->pool);

    IrNode *entryBB = createBlock(ctx, "entry");
    IrNode *exitBB = createBlock(ctx, "exit");
    ctx->funcExitBlock = exitBB;

    setBlock(ctx, entryBB);
    emitStmt(ctx, makeIrScopeEnter(ctx->pool, &node->loc, 0));

    bool hasRet = funcType->func.ret && funcType->func.ret->tag != irTypeVoid;
    if (hasRet) {
        ctx->funcRetVar = makeIrVarDecl(ctx->pool, &node->loc, flgNone, "__ret", "__ret", funcType->func.ret, 0);
        emitStmt(ctx, ctx->funcRetVar);
    }
    else {
        ctx->funcRetVar = NULL;
    }

    if (node->funcDecl.body) {
        astVisit(visitor, node->funcDecl.body);
    }

    emitBranchIfNotTerminated(ctx, exitBB);

    setBlock(ctx, exitBB);
    emitStmt(ctx, makeIrScopeExit(ctx->pool, &node->loc, 0));
    IrNode *retVal = ctx->funcRetVar ? makeIrVarRef(ctx->pool, &node->loc, ctx->funcRetVar) : NULL;
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

static void visitClassOrStructDecl(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    lowerType(ctx, node->type);
    AstNode *members = nodeIs(node, StructDecl) ? node->structDecl.members : node->classDecl.members;
    for (AstNode *m = members; m; m = m->next) {
        if (nodeIs(m, FuncDecl)) {
            astVisit(visitor, m);
        }
    }
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    cstring moduleName = node->program.module ? node->program.module->moduleDecl.name : "main";
    ctx->module = makeIrModule(ctx->driver->pool, &node->loc, moduleName, ctx->types);

    for (AstNode *decl = node->program.decls; decl; decl = decl->next) {
        astVisit(visitor, decl);
    }
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
        [astPath] = visitIdentifier,
        [astMemberExpr] = visitMemberExpr,
        [astIndexExpr] = visitIndexExpr,
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
        [astVarDecl] = visitVarDecl,
        [astAssignExpr] = visitAssignExpr,
        [astExprStmt] = visitExprStmt,
        [astBlockStmt] = visitBlockStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astForStmt] = visitForStmt,
        [astBreakStmt] = visitBreakStmt,
        [astContinueStmt] = visitContinueStmt,
        [astReturnStmt] = visitReturnStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astFuncDecl] = visitFuncDecl,
        [astStructDecl] = visitClassOrStructDecl,
        [astClassDecl] = visitClassOrStructDecl
    });
    // clang-format on

    astVisit(&visitor, program);

    if (nodeIs(node, Metadata)) {
        node->metadata.ir = context.module;
    }

    return context.module;
}
