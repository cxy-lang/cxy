//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/builtins.h"

#include "core/alloc.h"

static u64 addUnionDecl(UnionMember *members, const Type *member, u64 count)
{
    //    if (typeIs(member, Union)) {
    //        for (u64 i = 0; i < member->tUnion.count; i++) {
    //            count =
    //                addUnionDecl(members, member->tUnion.members[i].type,
    //                count);
    //        }
    //        return count;
    //    }

    for (u64 i = 0; i < count; i++) {
        if (members[i].type == member)
            return count;
    }
    members[count++] = (UnionMember){.type = member};
    return count;
}

static const Type *synthesizeUnionDestructor(TypingContext *ctx,
                                             AstVisitor *visitor,
                                             const Type *unionType)
{
    const FileLoc *loc = builtinLoc();
    cstring funcName = makeStringf(ctx->strings, "__union_dctor_%u", (unsigned)unionType->index);
    cstring paramName = makeString(ctx->strings, "u");

    AstNode *paramType = makeTypeReferenceNode(ctx->pool, makeReferenceType(ctx->types, unionType, flgNone), loc);
    AstNode *param = makeFunctionParam(ctx->pool, loc, paramName, paramType, NULL, flgNone, NULL);

    AstNodeList cases = {};
    for (u64 i = 0; i < unionType->tUnion.count; i++) {
        const Type *memberType = unionType->tUnion.members[i].type;
        if (!isDestructible(memberType))
            continue;

        cstring aliasName = makeStringf(ctx->strings, "y%u", (unsigned)i);
        AstNode *aliasNode = makeResolvedIdentifier(ctx->pool, loc, aliasName, flgNone, NULL, NULL, memberType);
        AstNode *typeRef = makeTypeReferenceNode(ctx->pool, memberType, loc);

        AstNode *elemNode = makeResolvedIdentifier(ctx->pool, loc, aliasName, flgNone, aliasNode, NULL, memberType);
        AstNode *dropStmt = makeExprStmt(
            ctx->pool, loc, flgNone,
            makeBackendCallExpr(ctx->pool, loc, flgNone, bfiDrop, elemNode, makeVoidType(ctx->types)),
            NULL, makeVoidType(ctx->types));

        AstNode *caseNode = makeAstNode(
            ctx->pool, loc,
            &(AstNode){.tag = astCaseStmt,
                       .flags = flgNone,
                       .caseStmt = {.match = typeRef, .body = dropStmt, .alias = aliasNode, .idx = (u32)i}});
        insertAstNode(&cases, caseNode);
    }

    AstNode *uIdent = makeResolvedIdentifier(ctx->pool, loc, paramName, flgNone, param, NULL, param->type);
    AstNode *matchNode = makeAstNode(
        ctx->pool, loc,
        &(AstNode){.tag = astMatchStmt,
                   .flags = flgNone,
                   .matchStmt = {.expr = uIdent, .cases = cases.first}});
    AstNode *body = makeBlockStmt(ctx->pool, loc, matchNode, NULL, makeVoidType(ctx->types));
    AstNode *funcDecl = makeFunctionDecl(ctx->pool, loc, funcName, param, makeVoidAstNode(ctx->pool, loc, flgNone, NULL, NULL), body, flgGenerated | flgPure, NULL, NULL);

    const Type *funcType = checkType(visitor, funcDecl);
    if (!typeIs(funcType, Error) && ctx->root.parent) {
        astModifierAddAsLast(&ctx->root, funcDecl);
    }
    return funcType;
}

static const Type *synthesizeUnionCopy(TypingContext *ctx,
                                       AstVisitor *visitor,
                                       const Type *unionType)
{
    const FileLoc *loc = builtinLoc();
    cstring funcName = makeStringf(ctx->strings, "__union_copy_%u", (unsigned)unionType->index);
    cstring paramName = makeString(ctx->strings, "u");

    AstNode *paramType = makeTypeReferenceNode(ctx->pool, makeReferenceType(ctx->types, unionType, flgNone), loc);
    AstNode *param = makeFunctionParam(ctx->pool, loc, paramName, paramType, NULL, flgNone, NULL);

    AstNodeList cases = {};
    for (u64 i = 0; i < unionType->tUnion.count; i++) {
        const Type *memberType = unionType->tUnion.members[i].type;
        cstring aliasName = makeStringf(ctx->strings, "y%u", (unsigned)i);
        AstNode *aliasNode = makeResolvedIdentifier(ctx->pool, loc, aliasName, flgNone, NULL, NULL, memberType);
        AstNode *typeRef = makeTypeReferenceNode(ctx->pool, memberType, loc);

        AstNode *elemExpr = makeResolvedIdentifier(ctx->pool, loc, aliasName, flgNone, aliasNode, NULL, memberType);
        if (isDestructible(memberType) || isClassType(memberType)) {
            elemExpr = makeBackendCallExpr(ctx->pool, loc, flgNone, bfiCopy, elemExpr, memberType);
        }

        AstNode *retStmt = makeReturnAstNode(ctx->pool, loc, flgNone, elemExpr, NULL, unionType);
        AstNode *caseNode = makeAstNode(
            ctx->pool, loc,
            &(AstNode){.tag = astCaseStmt,
                       .flags = flgNone,
                       .caseStmt = {.match = typeRef, .body = retStmt, .alias = aliasNode, .idx = (u32)i}});
        insertAstNode(&cases, caseNode);
    }

    AstNode *uIdent = makeResolvedIdentifier(ctx->pool, loc, paramName, flgNone, param, NULL, param->type);
    AstNode *matchNode = makeAstNode(
        ctx->pool, loc,
        &(AstNode){.tag = astMatchStmt,
                   .flags = flgNone,
                   .matchStmt = {.expr = uIdent, .cases = cases.first}});
    AstNode *body = makeBlockStmt(ctx->pool, loc, matchNode, NULL, unionType);
    AstNode *retTypeNode = makeTypeReferenceNode(ctx->pool, unionType, loc);
    AstNode *funcDecl = makeFunctionDecl(ctx->pool, loc, funcName, param, retTypeNode, body, flgGenerated | flgPure, NULL, NULL);

    const Type *funcType = checkType(visitor, funcDecl);
    if (!typeIs(funcType, Error) && ctx->root.parent) {
        astModifierAddAsLast(&ctx->root, funcDecl);
    }
    return funcType;
}

static void implementUnionTypeCopyAndDestructor(AstVisitor *visitor,
                                                AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    bool hasCopy = node->type->tUnion.copyFunc != NULL;
    if (!isDestructible(node->type) || hasCopy)
        return;

    const Type *func = synthesizeUnionCopy(ctx, visitor, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->tUnion.copyFunc = func;

    func = synthesizeUnionDestructor(ctx, visitor, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->tUnion.destructorFunc = func;
}

const Type *checkTypeShallow(AstVisitor *visitor, AstNode *node, bool shallow)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node == NULL)
        return NULL;
    bool wasShallow = ctx->shallow;
    ctx->shallow = wasShallow || shallow;
    if (hasFlag(node, Comptime)) {
        node->flags &= ~flgComptime;
        bool status = evaluate(ctx->evaluator, node);
        if (!status) {
            ctx->shallow = wasShallow;
            return node->type = ERROR_TYPE(ctx);
        }
        if (nodeIs(node, Noop)) {
            return makeVoidType(ctx->types);
        }
    }

    if (node->type) {
        ctx->shallow = wasShallow;
        return resolveType(node->type);
    }

    astVisit(visitor, node);
    ctx->shallow = wasShallow;

    const Type *type = resolveType(node->type),
               *unwrapped = unwrapType(type, NULL);
    if (typeIs(unwrapped, This) && unwrapped->_this.trackReferences) {
        pushThisReference(unwrapped, node);
    }
    return type;
}

void checkBuiltinType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    switch (node->tag) {
    case astAutoType:
        node->type = makeAutoType(ctx->types);
        break;
    case astStringType:
        node->type = makeStringType(ctx->types);
        break;
    case astVoidType:
        node->type = makeVoidType(ctx->types);
        break;
    case astPrimitiveType:
        node->type = getPrimitiveType(ctx->types, node->primitiveType.id);
        break;
    default:
        csAssert0(false);
    }
    if (hasFlag(node, Const))
        node->type = makeWrappedType(ctx->types, node->type, flgConst);
}

void checkTypeDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (!hasFlag(node, ForwardDecl) && node->typeDecl.aliased) {
        const Type *type =
            makeThisType(ctx->types, node->typeDecl.name, flgNone);
        node->type = type;
        ((Type *)type)->_this.trackReferences = true;

        const Type *aliased = checkType(visitor, node->typeDecl.aliased);
        if (typeIs(aliased, Error)) {
            resolveThisReferences(ctx->types, type, NULL);
            node->type = aliased;
            return;
        }

        ((Type *)type)->_this.that = node->type = makeAliasType(
            ctx->types, aliased, node->typeDecl.name, node->flags & flgConst);
        resolveThisReferences(ctx->types, type, aliased);
    }
    else {
        node->type = makeOpaqueTypeWithFlags(
            ctx->types,
            node->typeDecl.name,
            getForwardDeclDefinition(node),
            hasFlag(node, ForwardDecl) ? flgForwardDecl : flgNone);
    }
}

void checkUnionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *members = node->unionDecl.members, *member = members;
    u64 count = 0;

    for (; member; member = member->next) {
        const Type *type = checkType(visitor, member);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }
        //        if (typeIs(type, Union))
        //            count += type->tUnion.count;
        //        else
        count++;
    }

    if (typeIs(node->type, Error))
        return;

    UnionMember *members_ = mallocOrDie(sizeof(UnionMember) * count);
    u64 i = 0;
    member = node->unionDecl.members;
    u64 flags = flgNone;
    for (; member; member = member->next) {
        i = addUnionDecl(members_, resolveType(member->type), i);
        if (isClassType(member->type) || isDestructible(member->type))
            flags |= flgReferenceMembers;
    }

    node->type = makeUnionType(ctx->types, members_, i, flags);
    free(members_);
    implementUnionTypeCopyAndDestructor(visitor, node);
}

void checkOptionalType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->optionalType.type);
    if (target == NULL || typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = makeOptionalType(ctx->types, target, flgNone);
}

void checkPointerType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *pointed = checkType(visitor, node->pointerType.pointed);
    if (pointed == NULL || typeIs(pointed, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->type = makePointerType(ctx->types, pointed, node->flags & flgConst);
}

void checkReferenceType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *referred = checkType(visitor, node->referenceType.referred);
    if (referred == NULL || typeIs(referred, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isReferenceType(referred)) {
        if (hasFlag(node, Const) && !hasFlag(referred, Const))
            referred = makeWrappedType(ctx->types, referred, flgConst);
        node->type = referred;
        return;
    }

    if (isReferable(referred)) {
        node->type =
            makeReferenceType(ctx->types, referred, node->flags & flgConst);
    }
    else
        node->type = referred;
}

void checkExceptionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *param = node->exception.params;
    for (; param; param = param->next) {
        const Type *type = checkType(visitor, param);
        if (typeIs(type, Error)) {
            node->type = ERROR_TYPE(ctx);
        }
    }
    if (typeIs(node->type, Error))
        return;
    node->type = makeExceptionType(ctx->types, node);
    const Type *type = checkType(visitor, node->exception.body);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
    }
}

void checkResultType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    const Type *target = checkType(visitor, node->resultType.target);
    if (typeIs(target, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = makeResultType(ctx->types, target);
}
