//
// Created by Carter Mbotho on 2023-08-24.
//
#include "check.h"

#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/visitor.h"

#include "core/alloc.h"
#include "lang/middle/shake/shake.h"

static bool checkClosureParameters(AstVisitor *visitor,
                                   AstNode *params,
                                   u64 *paramsCountOut)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    u64 count = 0;
    for (AstNode *param = params; param; param = param->next, count++) {
        param->type ?: checkType(visitor, param);
        if (typeIs(param->type, Error))
            return false;

        if (hasFlag(param, Variadic)) {
            logError(ctx->L,
                     &param->loc,
                     "variadic parameters are not supported on closures",
                     NULL);
            return false;
        }
    }
    *paramsCountOut = count;
    return true;
}

static bool synthesizeClosureEnvironment(AstVisitor *visitor,
                                         const FileLoc *loc,
                                         Capture *captures,
                                         u64 captureCount,
                                         const Type **envTypeOut,
                                         AstNode **envExprOut)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (captureCount == 0) {
        *envTypeOut = makeVoidType(ctx->types);
        *envExprOut = makeNullLiteral(ctx->pool,
                                      loc,
                                      NULL,
                                      makeVoidPointerType(ctx->types, flgNone));
        return true;
    }

    NamedTypeMember *envMembers =
        allocFromMemPool(ctx->pool, sizeof(NamedTypeMember) * captureCount);
    AstNodeList envFieldDecls = {};
    AstNodeList envInitArgs = {};

    for (u64 i = 0; i < captureCount; i++) {
        Capture *capture = &captures[i];
        cstring name = getCapturedNodeName(capture->node);
        const Type *outerType = capture->node->type;
        const Type *fieldType = (capture->mode == capReference)
                                    ? makeReferenceType(ctx->types, outerType, flgNone)
                                    : outerType;

        AstNode *field = capture->field;
        csAssert0(field != NULL);
        field->structField.type =
            makeTypeReferenceNode(ctx->pool, fieldType, &capture->node->loc);
        field->type = fieldType;
        field->structField.index = (u32)i;

        envMembers[i] = (NamedTypeMember){
            .name = name, .type = fieldType, .decl = field};
        insertAstNode(&envFieldDecls, field);

        AstNode *val = makeResolvedPath(ctx->pool,
                                        loc,
                                        name,
                                        capture->node->flags,
                                        capture->node,
                                        NULL,
                                        outerType);
        if (capture->mode == capReference) {
            val = makeReferenceOfExpr(
                ctx->pool, loc, flgNone, val, NULL, fieldType);
        }
        else if (capture->mode == capMove) {
            val = makeUnaryExpr(
                ctx->pool, loc, flgNone, true, opMove, val, NULL, fieldType);
        }
        insertAstNode(&envInitArgs,
                      makeFieldExpr(ctx->pool, loc, name, flgNone, val, field, NULL));
    }

    cstring envName = makeAnonymousVariable(ctx->strings, "__closure_env");
    AstNode *envDecl = makeStructDecl(
        ctx->pool, loc, flgNone, envName, envFieldDecls.first, NULL, NULL);

    AstNode *builtins = createClassOrStructBuiltins(ctx->pool, envDecl),
            *builtin = builtins;
    for (; builtin; builtin = builtin->next) {
        builtin->funcDecl.this_ =
            makeFunctionParam(ctx->pool,
                              &builtin->loc,
                              S_this,
                              NULL,
                              NULL,
                              flgNone,
                              builtin->funcDecl.signature->params);
        builtin->parentScope = envDecl;
    }
    insertAstNode(&envFieldDecls, builtins);
    envDecl->structDecl.members = envFieldDecls.first;
    envDecl->parentScope = ctx->root.parent;

    const Type *envType = checkType(visitor, envDecl);
    if (typeIs(envType, Error))
        return false;

    astModifierAdd(&ctx->root, envDecl);

    *envTypeOut = envType;
    *envExprOut = makeStructExpr(ctx->pool,
                                 loc,
                                 flgNone,
                                 makeTypeReferenceNode(ctx->pool, envType, loc),
                                 envInitArgs.first,
                                 NULL,
                                 envType);
    return true;
}

static AstNode *synthesizeClosureBodyFunction(AstVisitor *visitor,
                                              AstNode *closureNode,
                                              const FileLoc *loc,
                                              const Type *envType,
                                              AstNode *params,
                                              AstNode *ret,
                                              AstNode *body,
                                              AstNode **funcRefOut,
                                              const Type **fnTypeOut)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    cstring bodyName = makeAnonymousVariable(ctx->strings, "__closure_body");
    const Type *envPtrType =
        isVoidType(envType)
            ? makeVoidPointerType(ctx->types, flgNone)
            : makePointerType(ctx->types, envType, flgNone);
    AstNode *envThisParam =
        makeFunctionParam(ctx->pool,
                          loc,
                          S_this,
                          makeTypeReferenceNode(ctx->pool, envPtrType, loc),
                          NULL,
                          flgNone,
                          params);

    AstNode *bodyFunc = makeFunctionDecl(ctx->pool,
                                         loc,
                                         bodyName,
                                         params,
                                         ret,
                                         body,
                                         flgNone,
                                         NULL,
                                         NULL);
    bodyFunc->funcDecl.this_ = envThisParam;
    if (closureNode)
        closureNode->closureExpr.construct = bodyFunc;

    const Type *fnType = checkType(visitor, bodyFunc);
    if (typeIs(fnType, Error))
        return NULL;

    astModifierAdd(&ctx->root, bodyFunc);

    *fnTypeOut = fnType;
    *funcRefOut =
        makeResolvedPath(ctx->pool, loc, bodyName, flgNone, bodyFunc, NULL, fnType);
    return bodyFunc;
}

void checkClosureExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    ctx->currentEnclosure = node;

    u64 paramsCount = 0;
    if (!checkClosureParameters(visitor, node->closureExpr.params, &paramsCount)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *envType = NULL;
    AstNode *envExpr = NULL;
    if (!synthesizeClosureEnvironment(visitor,
                                      &node->loc,
                                      node->closureExpr.capture,
                                      node->closureExpr.captureCount,
                                      &envType,
                                      &envExpr)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *fnType = NULL;
    AstNode *funcRef = NULL;
    AstNode *bodyFunc = synthesizeClosureBodyFunction(visitor,
                                                      node,
                                                      &node->loc,
                                                      envType,
                                                      node->closureExpr.params,
                                                      node->closureExpr.ret,
                                                      node->closureExpr.body,
                                                      &funcRef,
                                                      &fnType);
    if (bodyFunc == NULL || typeIs(fnType, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type **userParamTypes =
        paramsCount > 0
            ? allocFromMemPool(ctx->pool, sizeof(Type *) * paramsCount)
            : NULL;
    u64 pIdx = 0;
    for (AstNode *p = node->closureExpr.params; p; p = p->next, pIdx++) {
        userParamTypes[pIdx] = p->type;
    }
    const Type *userFnType = makeFuncType(
        ctx->types,
        &(Type){.tag = typFunc,
                .flags = flgNone,
                .func = {.params = userParamTypes,
                         .paramsCount = (u16)paramsCount,
                         .retType = bodyFunc->type->func.retType}});

    const Type *closureType = makeClosureType(ctx->types, envType, userFnType, false);
    const Type *canonicalType =
        makeClosureType(ctx->types, makeVoidType(ctx->types), userFnType, false);
    clearAstBody(node);
    node->tag = astClosureValue;
    node->type = closureType;
    node->closureValue.env = envExpr;
    node->closureValue.func = funcRef;
    node->closureValue.canonicalType = canonicalType;
}

bool promoteClosureToEscaping(TypingContext *ctx,
                             AstNode *expr,
                             const Type *targetType,
                             cstring actionContext)
{
    if (!expr || !isClosureType(targetType) || !targetType->closure.isEscaping)
        return true;

    AstNode *target = resolvePath(expr) ?: expr;

    if (nodeIs(target, FuncParamDecl) && isClosureType(target->type) &&
        !target->type->closure.isEscaping) {
        logError(ctx->L,
                 &expr->loc,
                 "cannot {s} non-escaping closure parameter '{s}' to an "
                 "escaping destination",
                 (FormatArg[]){{.s = actionContext},
                               {.s = target->funcParam.name}});
        return false;
    }

    const Type *canonical = targetType->closure.env
                                ? makeClosureType(ctx->types,
                                                  makeVoidType(ctx->types),
                                                  targetType->closure.fn,
                                                  true)
                                : targetType;

    if (nodeIs(target, VarDecl) && target->varDecl.init &&
        nodeIs(target->varDecl.init, ClosureValue)) {
        AstNode *val = target->varDecl.init;
        val->type = makeClosureType(
            ctx->types, val->type->closure.env, val->type->closure.fn, true);
        val->closureValue.canonicalType = canonical;
        target->type = canonical;
    }
    else if (nodeIs(expr, ClosureValue)) {
        expr->type = makeClosureType(
            ctx->types, expr->type->closure.env, expr->type->closure.fn, true);
        expr->closureValue.canonicalType = canonical;
    }

    return true;
}
