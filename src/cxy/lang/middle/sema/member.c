//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "../eval/eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

static const Type *determineMemberTargetType(TypingContext *ctx,
                                             const AstNode *parent,
                                             AstNode *node)
{
    if (parent == NULL)
        return NULL;

    const Type *type = NULL;
    switch (parent->tag) {
    case astVarDecl:
        type = parent->varDecl.type ? parent->varDecl.type->type : NULL;
        break;
    case astAssignExpr:
    case astBinaryExpr:
        type = parent->assignExpr.lhs->type;
        break;
    case astSwitchStmt:
        type = parent->switchStmt.cond->type;
        break;
    case astCaseStmt:
        return determineMemberTargetType(ctx, parent->parentScope, node);
    case astFuncParamDecl:
        type = parent->funcParam.type->type;
        break;
    case astFieldExpr:
        type = parent->type;
        break;
    case astFieldDecl:
        type = parent->structField.type ? parent->structField.type->type : NULL;
        break;
    default:
        break;
    }

    if (type == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "shorthand member expression not supported on current context {s}",
                 (FormatArg[]){{.s = getAstNodeName(parent)}});
        return NULL;
    }

    const Type *stripped = stripPointerOnce(type, NULL);
    if (!typeIs(stripped, Enum)) {
        logError(ctx->L,
                 &node->loc,
                 "shorthand member expression on type '{t}', only supported on "
                 "enum types",
                 (FormatArg[]){{.t = type}});
    }

    node->memberExpr.target = makeResolvedPath(ctx->pool,
                                               &node->loc,
                                               type->name,
                                               type->flags & flgConst,
                                               type->tEnum.decl,
                                               NULL,
                                               type);
    return type;
}

static const Type *handleRedirectMemberAccess(AstVisitor *visitor,
                                              AstNode *node,
                                              const Type *targetType)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target;
    if (isOptionalType(targetType)) {
        return getOptionalTargetType(targetType);
    }

    if (isClassOrStructType(targetType)) {
        const Type *redirectOp = findStructMemberType(targetType, S_Redirect);
        if (redirectOp == NULL) {
            logError(ctx->L,
                     &node->loc,
                     "type '{t}' does not implement redirect operator `&.`",
                     (FormatArg[]){{.t = targetType}});
            return NULL;
        }

        AstNode *redirectDecl = redirectOp->func.decl;
        AstNode *redirectCall = makeCallExpr(
            ctx->pool,
            &target->loc,
            makeMemberExpr(ctx->pool,
                           &target->loc,
                           target->flags,
                           target,
                           makeResolvedPath(ctx->pool,
                                            &target->loc,
                                            S_Redirect,
                                            redirectDecl->flags,
                                            redirectDecl,
                                            NULL,
                                            NULL),
                           NULL,
                           NULL),
            NULL,
            flgNone,
            NULL,
            NULL);
        const Type *retType = checkType(visitor, redirectCall);
        if (typeIs(retType, Error))
            return NULL;

        node->memberExpr.target = redirectCall;
        node->memberExpr.isRedirect = false;
        return retType;
    }

    logError(ctx->L,
             &node->loc,
             "redirect operator `&.` not supported on type '{t}'",
             (FormatArg[]){{.t = targetType}});
    return NULL;
}

static const Type *handleOptionalMemberAccess(TypingContext *ctx,
                                              const FileLoc *loc,
                                              const Type *targetType)
{
    if (isOptionalType(targetType))
        return getOptionalTargetType(targetType);
    if (isClassType(targetType))
        return targetType;
    if (isPointerType(targetType))
        return targetType->pointer.pointed;

    logError(ctx->L,
             loc,
             "optional member access `?.` is only supported on optional, class, "
             "or pointer types, got '{t}'",
             (FormatArg[]){{.t = targetType}});
    return NULL;
}

static const Type *checkTupleIndexMember(TypingContext *ctx,
                                         AstNode *node,
                                         const Type *targetType,
                                         AstNode *member)
{
    if (!typeIs(targetType, Tuple)) {
        logError(ctx->L,
                 &member->loc,
                 "type '{t}' does not support integer literal member "
                 "access, expecting expression of tuple type",
                 (FormatArg[]){{.t = targetType}});
        return NULL;
    }

    if (member->intLiteral.isNegative ||
        integerLiteralValue(member) >= targetType->tuple.count) {
        logError(ctx->L,
                 &member->loc,
                 "tuple {t} member access out of range, expecting "
                 "'0-{u64}', got '{i128}",
                 (FormatArg[]){{.t = targetType},
                               {.u64 = targetType->tuple.count - 1},
                               {.i128 = member->intLiteral.value}});
        return NULL;
    }

    const Type *elemType = targetType->tuple.members[member->intLiteral.value];
    node->flags = (targetType->flags & flgConst) | (elemType->flags & flgConst);
    return elemType;
}

void checkMemberExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target,
            *member = node->memberExpr.member;

    const Type *targetType =
        target ? checkType(visitor, target)
               : determineMemberTargetType(ctx, node->parentScope, node);
    if (targetType == NULL || typeIs(targetType, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    // `#T.attr`: the target is a type info node, evaluate at comp-time
    if (target && hasFlag(target, Typeinfo)) {
        if (!evaluate(ctx->evaluator, node)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        if (node->type == NULL)
            checkType(visitor, node);
        return;
    }

    if (hasFlag(member, Comptime) && !evaluate(ctx->evaluator, member)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (node->memberExpr.isRedirect) {
        targetType = handleRedirectMemberAccess(visitor, node, targetType);
        if (targetType == NULL || typeIs(targetType, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
        target = node->memberExpr.target;
    }

    bool isOptionalAccess = node->memberExpr.isOptional;
    if (isOptionalAccess) {
        targetType = handleOptionalMemberAccess(ctx, &node->loc, targetType);
        if (targetType == NULL || typeIs(targetType, Error)) {
            node->type = ERROR_TYPE(ctx);
            return;
        }
    }

    targetType = stripAll(targetType);
    if (nodeIs(member, Identifier)) {
        node->type = checkMember(visitor, targetType, member);
        if (node->type == NULL) {
            logError(ctx->L,
                     &member->loc,
                     "type '{t}' does not have member named '{s}'",
                     (FormatArg[]){{.t = targetType},
                                   {.s = member->ident.value}});
            node->type = ERROR_TYPE(ctx);
        }
        if (member->ident.super) {
            const Type *type = makeReferenceType(
                ctx->types,
                member->ident.resolvesTo->parentScope->type,
                member->ident.resolvesTo->parentScope->type->flags);

            if (isClassType(target->type)) {
                target = makeReferenceOfExpr(
                    ctx->pool,
                    &target->loc,
                    target->flags,
                    target,
                    NULL,
                    makeReferenceType(ctx->types, target->type, target->flags));
            }

            node->memberExpr.target = makeCastExpr(
                ctx->pool,
                &target->loc,
                target->flags,
                target,
                makeTypeReferenceNode(ctx->pool, type, &target->loc),
                NULL,
                type);
            member->ident.super--;
        }
    }
    else if (nodeIs(member, Path)) {
        AstNode *name = member->path.elements;
        for (; name; name = name->next) {
            targetType = checkMember(visitor, targetType, name);
            if (targetType == NULL) {
                logError(
                    ctx->L,
                    &member->loc,
                    "type '{t}' does not have member named '{s}'",
                    (FormatArg[]){{.t = targetType},
                                  {.s = name->pathElement.name}});
                node->type = ERROR_TYPE(ctx);
                return;
            }
        }
        node->type = targetType;
    }
    else if (nodeIs(member, IntegerLit)) {
        const Type *res = checkTupleIndexMember(ctx, node, targetType, member);
        node->type = res ?: ERROR_TYPE(ctx);
    }

    if (isOptionalAccess && node->type && !typeIs(node->type, Error)) {
        if (!typeIs(node->type, Func) && !isOptionalType(node->type)) {
            node->type = makeOptionalType(ctx->types, node->type, flgNone);
        }
    }
}
