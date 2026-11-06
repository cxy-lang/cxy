//
// Created by Carter Mbotho on 2023-07-06.
//
#include "shake.h"

#include "lang/frontend/operator.h"
#include "lang/middle/builtins.h"
#include "lang/operations.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/visitor.h"

#include "core/alloc.h"
#include "lang/middle/eval/eval.h"

#include <inttypes.h>

static AstNode *makeTupleMemberExpr(ShakeAstContext *ctx, AstNode *tuple, u64 i)
{
    if (tuple == NULL)
        return NULL;

    if (nodeIs(tuple, TupleExpr)) {
        // TODO collect
        AstNode *arg = getNodeAtIndex(tuple->tupleExpr.elements, i);
        return duplicateAstNode(ctx->pool, arg);
    }

    AstNode *target = NULL;
    if (nodeIs(tuple, VarDecl)) {
        target = makePathFromIdent(ctx->pool, tuple->varDecl.names);
    }
    else {
        csAssert0(nodeIs(tuple, Path) || nodeIs(tuple, Identifier));
        target = copyAstNode(ctx->pool, tuple);
    }

    return makeAstNode(
        ctx->pool,
        &tuple->loc,
        &(AstNode){
            .tag = astMemberExpr,
            .flags = tuple->flags,
            .memberExpr = {.target = target,
                           .member = makeAstNode(
                               ctx->pool,
                               &tuple->loc,
                               &(AstNode){.tag = astIntegerLit,
                                          .intLiteral.uValue = (u64)i})}});
}

static void transformVariadicFunction(ShakeAstContext *ctx,
                                      AstNode *node,
                                      AstNode *param)
{

    AstNode *parent = node->parentScope;
    if (nodeIs(parent, GenericDecl)) {
        AstNode *gparam = getLastAstNode(parent->genericDecl.params);
        if (hasFlag(gparam, Variadic)) {
            gparam->genericParam.inferIndex = node->funcDecl.paramsCount;
            if (parent->genericDecl.inferrable == -1)
                parent->genericDecl.inferrable = 0;
            return;
        }
    }

    bool isReference = findAttribute(param, S_transient);
    node->flags |= flgVariadic;
    AstNode *genericParam = makeAstNode(
        ctx->pool,
        &param->funcParam.type->loc,
        &(AstNode){.tag = astGenericParam,
                   .flags = flgVariadic,
                   .genericParam = {.name = S__Variadic,
                                    .constraints = param->funcParam.type,
                                    .inferIndex = node->funcDecl.paramsCount}});
    param->funcParam.type = makePath(ctx->pool,
                                     &param->funcParam.type->loc,
                                     S__Variadic,
                                     param->funcParam.type->flags,
                                     NULL);

    if (nodeIs(parent, GenericDecl)) {
        AstNode *params = getLastAstNode(parent->genericDecl.params);
        if (params)
            params->next = genericParam;
        else
            parent->genericDecl.params = genericParam;
        parent->flags |= flgVariadic | (isReference ? flgReference : flgNone);
        if (parent->genericDecl.inferrable == -1)
            parent->genericDecl.inferrable = 0;
        parent->genericDecl.paramsCount++;
    }
    else {
        *node = (AstNode){.tag = astGenericDecl,
                          .loc = node->loc,
                          .flags = node->flags | flgVariadic |
                                   (isReference ? flgReference : flgNone),
                          .next = node->next,
                          .genericDecl = {.decl = copyAstNode(ctx->pool, node),
                                          .params = genericParam,
                                          .paramsCount = 1,
                                          .inferrable = 0}};
    }
}

attr(always_inline) static bool reportIfUnexpectedNumberOfParameters(
    ShakeAstContext *ctx,
    const FileLoc *loc,
    cstring name,
    u64 count,
    u64 expected)
{
    if (expected == count)
        return true;

    logError(ctx->L,
             loc,
             "unexpected numbers of parameters for {s} operator "
             "overload, expecting {u64}, got {u64}",
             (FormatArg[]){{.s = name}, {.u64 = expected}, {.u64 = count}});
    return false;
}

static bool validateOperatorOverloadArguments(ShakeAstContext *ctx,
                                              AstNode *node,
                                              u64 count)
{
    Operator op = node->funcDecl.operatorOverload;
    switch (op) {
#define f(OP, _0, _1, STR, ...)                                                \
    case op##OP:                                                               \
        return reportIfUnexpectedNumberOfParameters(                           \
            ctx,                                                               \
            &node->loc,                                                        \
            STR,                                                               \
            count,                                                             \
            (op == opRange || op == opIs) ? 0 : 1);
        AST_BINARY_EXPR_LIST(f)
#undef f

#define f(OP, _0, _1, STR, ...)                                                \
    case op##OP:                                                               \
        if (op != opInitOverload)                                              \
            return reportIfUnexpectedNumberOfParameters(                       \
                ctx, &node->loc, STR, count, 0);                               \
        else                                                                   \
            return true;

        AST_UNARY_EXPR_LIST(f)
#undef f

    case opIndexOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "[]", count, 1);

    case opIndexAssignOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "=[]", count, 2);

    case opSliceOverload:
        if (count < 1 || count > 2) {
            logError(ctx->L, &node->loc,
                     "operator `[..]` overload expects 1 or 2 parameters, got {u64}",
                     (FormatArg[]){{.u64 = count}});
            return false;
        }
        return true;

    case opStringOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "str", count, 1);

    case opHashOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "hash", count, 0);

    case opTruthy:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "!!", count, 0);

    case opDeinitOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "deinit", count, 0);

    case opDestructorOverload:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "destructor", count, 0);

    case opDestructorFwd:
        return reportIfUnexpectedNumberOfParameters(
            ctx, &node->loc, "destructor_fwd", count, 1);

    case opCallOverload:
    case opInitOverload:
    case opCopyOverload:
        return true;

    default:
        return false;
    }
}

attr(always_inline) static u16 inferGenericParamFromFuncParam(
    const AstNode *gparam, const AstNode *type, u16 index, bool *innerType)
{
    if (nodeIs(type, ReferenceType)) {
        type = type->referenceType.referred;
        *innerType = true;
    }
    else if (nodeIs(type, PointerType)) {
        type = type->pointerType.pointed;
        *innerType = true;
    }

    if (!nodeIs(type, Path))
        return 0;
    AstNode *elem = type->path.elements;
    if (elem->next || elem->pathElement.args)
        return 0;

    return gparam->genericParam.name == elem->pathElement.name ? index + 1 : 0;
}

static AstNode *shakeVariableInitializer(ShakeAstContext *ctx, AstNode *init)
{
    if (init == NULL || nodeIs(init, TupleExpr) || nodeIs(init, Identifier) ||
        (nodeIs(init, Path) && init->path.elements->next == NULL))
        return init;

    // Create variable for this
    return makeVarDecl(ctx->pool,
                       &init->loc,
                       init->flags,
                       makeAnonymousVariable(ctx->strings, "_gi"),
                       NULL,
                       init,
                       NULL,
                       NULL);
}

static AstNode *makeStrExprBuilder(ShakeAstContext *ctx, AstNode *node)
{
    AstNode *sb = findBuiltinDecl(S_String);
    csAssert0(sb);

    return makeVarDecl(
        ctx->pool,
        &node->loc,
        flgNone,
        makeAnonymousVariable(ctx->strings, S_sb),
        NULL,
        makeCallExpr(
            ctx->pool,
            &node->loc,
            makeResolvedPath(
                ctx->pool, &node->loc, S_String, flgNone, sb, NULL, sb->type),
            NULL,
            flgNone,
            NULL,
            NULL),
        NULL,
        NULL);
}

static AstNode *makeExceptionAssignMember(ShakeAstContext *ctx, AstNode *param)
{
    AstNode *member = makePathWithElements(
        ctx->pool,
        &param->loc,
        flgNone,
        makePathElement(
            ctx->pool,
            &param->loc,
            S_this,
            flgNone,
            makePathElement(
                ctx->pool, &param->loc, param->_name, flgNone, NULL, NULL),
            NULL),
        NULL);
    return makeExprStmt(
        ctx->pool,
        &param->loc,
        flgNone,
        makeAssignExpr(
            ctx->pool,
            &param->loc,
            flgNone,
            member,
            opAssign,
            makePath(ctx->pool, &param->loc, param->_name, flgNone, NULL),
            NULL,
            NULL),
        NULL,
        NULL);
}

static AstNode *makeExceptionInit(ShakeAstContext *ctx,
                                  cstring name,
                                  AstNode *params,
                                  AstNode *assigns)
{
    AstNode *body = makeBlockStmt(
        ctx->pool,
        builtinLoc(),
        makeExprStmt(
            ctx->pool,
            builtinLoc(),
            flgNone,
            makeCallExpr(
                ctx->pool,
                builtinLoc(),
                makePath(ctx->pool, builtinLoc(), S_super, flgNone, NULL),
                makeStringLiteral(ctx->pool, builtinLoc(), name, NULL, NULL),
                flgNone,
                NULL,
                NULL),
            assigns,
            NULL),
        NULL,
        NULL);

    AstNode *func = makeFunctionDecl(ctx->pool,
                                     builtinLoc(),
                                     S_InitOverload,
                                     params,
                                     NULL,
                                     body,
                                     flgNone,
                                     NULL,
                                     NULL);
    func->funcDecl.operatorOverload = opInitOverload;
    return func;
}

static inline AstNode *makeStringLitOrNull(ShakeAstContext *ctx,
                                           cstring str,
                                           AstNode *next)
{
    return str ? makeStringLiteral(ctx->pool, builtinLoc(), str, next, NULL)
               : makeNullLiteral(ctx->pool, builtinLoc(), next, NULL);
}

static void shakeVariableDecl(AstVisitor *visitor, AstNode *node);

static void shakeVariableDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);

    astVisit(visitor, node->varDecl.type);
    astVisit(visitor, node->varDecl.init);

    AstNode *names = node->varDecl.names, *init = node->varDecl.init,
            *type = node->varDecl.type, *name = names;
    bool isReference = findAttribute(node, S_transient) != NULL;
    if (names == NULL)
        return;
    if (names->next == NULL) {
        node->flags |= isReference ? flgReference : flgNone;
        node->varDecl.name = names->ident.value;
        if (nodeIs(names, StringLit))
            names->tag = astIdentifier;
        return;
    }

    AstNode *tuple = shakeVariableInitializer(ctx, init);

    AstNode *vars = NULL, *it = NULL;
    u64 i = 0;
    for (; name; i++) {
        AstNode *name_ = name;
        name = name->next;
        name_->next = NULL;

        if (nodeIs(name_, StringLit))
            names->tag = astIdentifier;

        if (isIgnoreVar(name_->ident.value)) {
            if (tuple == NULL) {
                logError(ctx->L,
                         &name_->loc,
                         "cannot use the builtin ignore `_` variable when "
                         "multi-variable declaration has no expression",
                         NULL);
                return;
            }
            continue;
        }

        AstNode *var = makeAstNode(
            ctx->pool,
            &name_->loc,
            &(AstNode){
                .tag = astVarDecl,
                .flags = node->flags | flgVisited |
                         (isReference ? flgReference : flgNone),
                .varDecl = {.name = name_->ident.value,
                            .names = name_,
                            .type = copyAstNode(ctx->pool, type),
                            .init = makeTupleMemberExpr(ctx, tuple, i)}});
        if (vars == NULL) {
            vars = var;
            it = var;
        }
        else {
            it->next = var;
            it = var;
        }
    }

    it->next = node->next;
    if (tuple == init) {
        *node = *vars;
    }
    else {
        tuple->next = vars;
        *node = *tuple;
    }
}

static void shakeIfStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ifStmt.cond;
    if (nodeIs(cond, VarDecl)) {
        AstNode *var = duplicateAstNode(ctx->pool, cond);
        cstring name = var->varDecl.names->ident.value;
        var->varDecl.name = makeAnonymousVariable(ctx->strings, "_hd");
        var->varDecl.names->ident.value = var->varDecl.name;
        astModifierAdd(&ctx->block, var);

        cond->tag = astPath;
        cond->path.elements = makePathElement(
            ctx->pool, &cond->loc, var->varDecl.name, var->flags, NULL, NULL);

        if (!nodeIs(node->ifStmt.body, BlockStmt)) {
            node->ifStmt.body = makeAstNode(
                ctx->pool,
                &node->ifStmt.body->loc,
                &(AstNode){.tag = astBlockStmt,
                           .blockStmt = {.stmts = node->ifStmt.body}});
        }
        node->ifStmt.body->blockStmt.stmts =
            makeVarAlias(ctx->pool,
                         &var->loc,
                         name,
                         var,
                         node->ifStmt.body->blockStmt.stmts);
    }
    else {
        astVisit(visitor, cond);
    }

    if (!nodeIs(node->ifStmt.body, BlockStmt)) {
        node->ifStmt.body =
            makeAstNode(ctx->pool,
                        &node->ifStmt.body->loc,
                        &(AstNode){.tag = astBlockStmt,
                                   .blockStmt = {.stmts = node->ifStmt.body}});
    }
    astVisit(visitor, node->ifStmt.body);

    if (node->ifStmt.otherwise) {
        AstNode *otherwise = node->ifStmt.otherwise;
        if (!nodeIs(otherwise, BlockStmt) && !nodeIs(otherwise, IfStmt)) {
            node->ifStmt.otherwise =
                makeAstNode(ctx->pool,
                            &otherwise->loc,
                            &(AstNode){.tag = astBlockStmt,
                                       .blockStmt = {.stmts = otherwise}});
        }
        astVisit(visitor, otherwise);
    }
}

static void shakeWhileStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond;

    // if (nodeIs(cond, VarDecl)) {
    //     AstNode *var = duplicateAstNode(ctx->pool, cond);
    //     var->varDecl.name = var->varDecl.names->ident.value;
    //
    //     astModifierAdd(&ctx->block, var);
    //
    //     cond->tag = astGroupExpr;
    //     cond->groupExpr.expr = makeAstNode(
    //         ctx->pool,
    //         &cond->loc,
    //         &(AstNode){.tag = astAssignExpr,
    //                    .assignExpr = {.op = opAssign,
    //                                   .lhs = makePathFromIdent(
    //                                       ctx->pool, var->varDecl.names),
    //                                   .rhs = var->varDecl.init}});
    //     var->varDecl.init = NULL;
    //
    //     if (!nodeIs(node->whileStmt.body, BlockStmt)) {
    //         node->whileStmt.body = makeAstNode(
    //             ctx->pool,
    //             &node->whileStmt.body->loc,
    //             &(AstNode){.tag = astBlockStmt,
    //                        .blockStmt = {.stmts = node->whileStmt.body}});
    //     }
    // }

    if (!nodeIs(node->whileStmt.body, BlockStmt)) {
        node->whileStmt.body = makeAstNode(
            ctx->pool,
            &node->whileStmt.body->loc,
            &(AstNode){.tag = astBlockStmt,
                       .blockStmt = {.stmts = node->whileStmt.body}});
    }

    // if (cond && !isLiteralExpr(cond)) {
    //     // if (cond) { body} else { break }
    //     node->whileStmt.body = makeBlockStmt(
    //         ctx->pool,
    //         &node->loc,
    //         makeIfStmt(ctx->pool,
    //                    &cond->loc,
    //                    flgNone,
    //                    cond,
    //                    node->whileStmt.body,
    //                    makeAstNode(ctx->pool,
    //                                builtinLoc(),
    //                                &(AstNode){.tag = astBreakStmt}),
    //                    NULL),
    //         NULL,
    //         NULL);
    //     node->whileStmt.cond =
    //         makeBoolLiteral(ctx->pool, builtinLoc(), true, NULL, NULL);
    // }
    astVisit(visitor, node->whileStmt.body);
}

static void shakeFieldDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    node->flags |= findAttribute(node, S_static) == NULL ? flgNone : flgStatic;
    astVisitFallbackVisitAll(visitor, node);
}

// The unwrapped return type of a `@task` function, `raises` tells if it is `!T`
static AstNode *getTaskReturnType(ShakeAstContext *ctx,
                                  const AstNode *node,
                                  bool *raises)
{
    const AstNode *ret = node->funcDecl.signature->ret;
    *raises = ret && nodeIs(ret, ResultType);
    if (*raises)
        ret = ret->resultType.target;
    if (ret == NULL)
        return makeVoidAstNode(ctx->pool, &node->loc, flgNone, NULL, NULL);
    return deepCloneAstNode(ctx->pool, ret);
}

// `__currentTask[R]().<method>(arg)`, `current` is the builtin `__currentTask`
static AstNode *makeTaskCall(ShakeAstContext *ctx,
                             const FileLoc *loc,
                             AstNode *current,
                             const AstNode *ret,
                             cstring method,
                             AstNode *arg)
{
    AstNode *path = makeResolvedPathWithArgs(ctx->pool,
                                             loc,
                                             S___currentTask,
                                             flgNone,
                                             current,
                                             deepCloneAstNode(ctx->pool, ret),
                                             NULL);
    AstNode *task =
        makeCallExpr(ctx->pool, loc, path, NULL, flgNone, NULL, NULL);
    AstNode *callee = makeMemberExpr(
        ctx->pool,
        loc,
        flgNone,
        task,
        makeIdentifier(ctx->pool, loc, method, 0, NULL, NULL),
        NULL,
        NULL);
    return makeCallExpr(ctx->pool, loc, callee, arg, flgNone, NULL, NULL);
}

/*
 * `@task f(...)` gets a sibling `_task_f(...): void` that runs `f` as the body
 * of a task and records the outcome on the running task. The form depends on
 * the declared return type `R` of `f`:
 *   f(...)                                     R is void
 *   __currentTask[R]()._set(f(...))            R is a value
 *   f(...) catch as ex { __currentTask[R]().throw(&&ex); return } !void
 *   var v = f(...) catch as ex { ...; return }; _set(&&v)         !R
 * `__currentTask` is the builtin registered by `stdlib/coro.cxy`.
 */
static AstNode *makeTaskWrapper(ShakeAstContext *ctx, AstNode *node)
{
    MemPool *pool = ctx->pool;
    const FileLoc *loc = &node->loc;
    cstring name = getDeclarationName(node);
    AstNode *current = findBuiltinDecl(S___currentTask);
    if (current == NULL) {
        logError(ctx->L,
                 loc,
                 "`@task` requires '__currentTask' to be defined (import "
                 "'stdlib/coro.cxy')",
                 NULL);
        return NULL;
    }
    bool raises = false;
    AstNode *ret = getTaskReturnType(ctx, node, &raises);
    bool isVoid = nodeIs(ret, VoidType);
    cstring setName = makeString(ctx->strings, "_set");

    AstNode *params =
        deepCloneManyAstNode(pool, node->funcDecl.signature->params);
    AstNodeList args = {};
    for (AstNode *param = params; param; param = param->next)
        insertAstNode(&args,
                      makePath(pool, loc, param->funcParam.name, flgNone, NULL));
    AstNode *call = makeCallExpr(pool,
                                 loc,
                                 makePath(pool, loc, name, flgNone, NULL),
                                 args.first,
                                 flgAsync, // the only call allowed on a `@task`
                                 NULL,
                                 NULL);

    AstNodeList stmts = {};
    if (!raises) {
        insertAstNode(
            &stmts,
            makeExprStmt(pool,
                         loc,
                         flgNone,
                         isVoid ? call
                                : makeTaskCall(ctx, loc, current, ret, setName, call),
                         NULL,
                         NULL));
    }
    else {
        AstNodeList handler = {};
        insertAstNode(
            &handler,
            makeExprStmt(
                pool,
                loc,
                flgNone,
                makeTaskCall(ctx,
                             loc,
                             current,
                             ret,
                             makeString(ctx->strings, "throw"),
                             makeUnaryExpr(pool,
                                           loc,
                                           flgNone,
                                           true,
                                           opMove,
                                           makePath(pool, loc, S_ex, flgNone, NULL),
                                           NULL,
                                           NULL)),
                NULL,
                NULL));
        insertAstNode(
            &handler, makeReturnAstNode(pool, loc, flgNone, NULL, NULL, NULL));
        AstNode *var = makeAstNode(
            pool,
            loc,
            &(AstNode){.tag = astAliasExpr,
                       .aliasExpr = {.name = S_ex, .expr = NULL}});
        AstNode *caught = makeCatchExpr(
            pool,
            loc,
            flgNone,
            call,
            var,
            makeBlockStmt(pool, loc, handler.first, NULL, NULL),
            NULL,
            NULL);
        if (isVoid) {
            insertAstNode(&stmts,
                          makeExprStmt(pool, loc, flgNone, caught, NULL, NULL));
        }
        else {
            cstring value = makeString(ctx->strings, "__task_value");
            insertAstNode(
                &stmts, makeVarDecl(pool, loc, flgNone, value, NULL, caught, NULL, NULL));
            AstNode *moved = makeUnaryExpr(pool,
                                           loc,
                                           flgNone,
                                           true,
                                           opMove,
                                           makePath(pool, loc, value, flgNone, NULL),
                                           NULL,
                                           NULL);
            insertAstNode(
                &stmts,
                makeExprStmt(pool,
                             loc,
                             flgNone,
                             makeTaskCall(ctx, loc, current, ret, setName, moved),
                             NULL,
                             NULL));
        }
    }

    AstNode *wrapper = makeFunctionDecl(
        pool,
        loc,
        makeStringf(ctx->strings, CXY_TASK_WRAPPER_PREFIX "%s", name),
        params,
        makeVoidAstNode(pool, loc, flgNone, NULL, NULL),
        makeBlockStmt(pool, loc, stmts.first, NULL, NULL),
        node->flags & (flgPublic | flgStatic | flgConst | flgTestContext),
        NULL,
        NULL);
    if (node->funcDecl.this_)
        wrapper->funcDecl.this_ = deepCloneAstNode(pool, node->funcDecl.this_);
    return wrapper;
}

static bool validateTaskFunction(ShakeAstContext *ctx, const AstNode *node)
{
    const char *problem = NULL;
    const AstNode *ret = node->funcDecl.signature->ret;
    if (ctx->inFunction)
        problem = "local functions cannot be tasks";
    else if (hasFlag(node, Extern) || node->funcDecl.body == NULL)
        problem = "a task function needs a body";
    else if (node->funcDecl.operatorOverload != opInvalid)
        problem = "operator overloads cannot be tasks";
    else if (ret == NULL || nodeIs(ret, AutoType))
        problem = "a task function needs an explicit return type";
    else {
        for (const AstNode *p = node->funcDecl.signature->params; p;
             p = p->next) {
            if (hasFlag(p, Variadic)) {
                problem = "variadic functions cannot be tasks";
                break;
            }
        }
    }
    if (problem == NULL)
        return true;
    logError(ctx->L,
             &node->loc,
             "invalid `@task` function, {s}",
             (FormatArg[]){{.s = problem}});
    return false;
}

static void shakeFuncDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    u16 required = 0, total = 0;
    bool isTask = findAttribute(node, S_task) != NULL;
    if (isTask && !validateTaskFunction(ctx, node))
        return;
    node->flags |= findAttribute(node, S_pure) == NULL ? flgNone : flgPure;
    node->flags |= findAttribute(node, S_static) == NULL ? flgNone : flgStatic;

    bool isStrOverload = node->funcDecl.operatorOverload == opStringOverload;
    bool hasDefaultParams = false, isVariadic = false;
    ctx->fun = getDeclarationName(node);

    AstNode *params = node->funcDecl.signature->params, *param = params;
    for (; param; param = param->next) {
        total++;
        astVisit(visitor, param);
        if (isStrOverload)
            param->flags |= flgReference;
        if (hasFlag(node, Variadic) && param->next == NULL)
            param->flags |= flgVariadic;

        if (param->funcParam.def) {
            if (!hasDefaultParams) {
                required = total - 1;
            }
            hasDefaultParams = true;
        }
        else if (hasDefaultParams && !hasFlag(param, Variadic)) {
            logError(ctx->L,
                     &param->loc,
                     "declaring a parameter without a default value not "
                     "allowed after one declared with a default value",
                     NULL);
            return;
        }
        else if (hasFlag(param, Variadic)) {
            // transform to variadic function
            if (param->next) {
                logError(ctx->L,
                         &param->next->loc,
                         "variadic parameter should be the last parameter",
                         NULL);
                return;
            }
            isVariadic = true;
            break;
        }
        else {
            required++;
        }
    }

    Operator overloadOperator = node->funcDecl.operatorOverload;
    if (overloadOperator != opInvalid) {
        if (!validateOperatorOverloadArguments(ctx, node, total))
            return;
        if (ctx->currentClsOrStruct && overloadOperator == opDeinitOverload) {
            ctx->currentClsOrStruct->flags |= flgImplementsDeinit;
        }
    }

    node->funcDecl.paramsCount = total;
    node->funcDecl.requiredParamsCount = required;
    if (node->funcDecl.signature->ret)
        astVisit(visitor, node->funcDecl.signature->ret);
    if (node->funcDecl.body && !nodeIs(node->funcDecl.body, BlockStmt)) {
        node->funcDecl.body = makeAstNode(
            ctx->pool,
            &node->funcDecl.body->loc,
            &(AstNode){
                .tag = astBlockStmt,
                .blockStmt = {
                    .stmts = makeAstNode(
                        ctx->pool,
                        &node->funcDecl.body->loc,
                        &(AstNode){.tag = astReturnStmt,
                                   .returnStmt.expr = node->funcDecl.body})}});
    }
    ctx->inFunction = true;
    astVisit(visitor, node->funcDecl.body);

    if (isVariadic) {
        if (!hasFlag(node, Extern)) {
            transformVariadicFunction(ctx, node, param);
        }
        else {
            node->flags |= flgVariadic;
        }
    }

    if (isTask) {
        AstNode *wrapper = makeTaskWrapper(ctx, node);
        if (wrapper)
            insertAstNode(&ctx->taskWrappers, wrapper);
    }
}

// Shakes the collected `_task_f` wrappers and returns them as one list
static AstNode *takeTaskWrappers(AstVisitor *visitor)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNodeList wrappers = ctx->taskWrappers;
    ctx->taskWrappers = (AstNodeList){};
    for (AstNode *it = wrappers.first; it;) {
        AstNode *next = it->next;
        it->next = NULL;
        astVisit(visitor, it);
        it->next = next;
        it = next;
    }
    return wrappers.first;
}

static void shakeUnionDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->unionDecl.members;
    for (; member; member = member->next) {
        astVisit(visitor, member);
        if (nodeIs(member, VoidType)) {
            member->tag = astPath;
            member->path.elements = makePathElement(
                ctx->pool, &member->loc, S_Void, flgNone, NULL, NULL);
        }
    }
}

static void shakeCallExpr(AstVisitor *visitor, AstNode *node)
{
    if (findAttribute(node, S_sync))
        node->flags |= flgSyncCall;
    astVisitFallbackVisitAll(visitor, node);
    AstNodeList args = {};
    AstNode *arg = node->callExpr.args;
    for (; arg;) {
        AstNode *tmp = arg;
        arg = arg->next;
        tmp->next = NULL;
        if (!nodeIs(tmp, Noop)) {
            insertAstNode(&args, tmp);
        }
    }
    node->callExpr.args = args.first;
}

static void shakeGroupExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *expr = node->groupExpr.expr;
    if (nodeIs(expr, BlockStmt)) {
        expr->flags |= flgBlockReturns;
        node->tag = astStmtExpr;
        node->stmtExpr.stmt = expr;
    }

    astVisitFallbackVisitAll(visitor, node);
}

// An operand that runs only on some paths is shaken inside a block of its own, so
// what shake hoists out of it (the builder of an `f"..."`) stays on that path.
// When nothing was hoisted the block is dropped again.
static void shakeLazyOperand(AstVisitor *visitor, AstNode **slot)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = *slot;
    if (expr == NULL)
        return;

    AstNode *next = expr->next;
    expr->next = NULL;
    AstNode *exprStmt =
        makeExprStmt(ctx->pool, &expr->loc, flgNone, expr, NULL, NULL);
    AstNode *block = makeBlockStmt(ctx->pool, &expr->loc, exprStmt, NULL, NULL);
    block->flags |= flgBlockReturns;
    astVisit(visitor, block);

    if (block->blockStmt.stmts == exprStmt && exprStmt->next == NULL) {
        expr->next = next;
        return;
    }
    *slot = makeStmtExpr(ctx->pool, &expr->loc, flgNone, block, next, NULL);
}

static void shakeTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->ternaryExpr.cond);
    shakeLazyOperand(visitor, &node->ternaryExpr.body);
    shakeLazyOperand(visitor, &node->ternaryExpr.otherwise);
}

// `x catch y` evaluates `y` only when `x` failed. A block is a scope already.
static void shakeCatchExpr(AstVisitor *visitor, AstNode *node)
{
    astVisit(visitor, node->catchExpr.expr);
    astVisit(visitor, node->catchExpr.var);
    if (nodeIs(node->catchExpr.body, BlockStmt))
        astVisit(visitor, node->catchExpr.body);
    else
        shakeLazyOperand(visitor, &node->catchExpr.body);
}

static void shakeBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    Operator op = node->binaryExpr.op;
    if (op == opLAnd || op == opLOr || op == opCoalesce) {
        astVisit(visitor, node->binaryExpr.lhs);
        shakeLazyOperand(visitor, &node->binaryExpr.rhs);
        return;
    }
    astVisitFallbackVisitAll(visitor, node);
}

static void shakeExprStmt(AstVisitor *visitor, AstNode *node)
{
    if (nodeIs(node->exprStmt.expr, CallExpr) && findAttribute(node, S_sync)) {
        node->exprStmt.expr->flags |= flgSyncCall;
    }
    astVisitFallbackVisitAll(visitor, node);
}

static void shakeMatchStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->matchStmt.expr;
    astVisit(visitor, expr);
    if (!nodeIsLeftValue(expr)) {
        AstNode *var =
            makeVarDecl(ctx->pool,
                        &expr->loc,
                        flgNone,
                        makeAnonymousVariable(ctx->strings, "_match"),
                        NULL,
                        expr,
                        NULL,
                        NULL);
        node->matchStmt.expr =
            makePath(ctx->pool, &expr->loc, var->varDecl.name, flgNone, NULL);

        astModifierAdd(&ctx->block, var);
    }
    astVisitManyNodes(visitor, node->matchStmt.cases);
}

static void shakeIteratorExpr(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    astVisitManyNodes(visitor, node->iteratorExpr.args);
    astVisit(visitor, node->iteratorExpr.type);

    if (node->iteratorExpr.next && !nodeIs(node->iteratorExpr.next, FuncDecl)) {
        AstNode *body = node->iteratorExpr.next;
        AstNode *bodyStmt = nodeIs(body, BlockStmt)
                                ? body
                                : makeBlockStmt(ctx->pool,
                                                &body->loc,
                                                makeReturnAstNode(ctx->pool, &body->loc, flgNone, body, NULL, NULL),
                                                NULL,
                                                NULL);
        AstNode *retType = makeOptionalTypeAst(ctx->pool, &node->iteratorExpr.type->loc, flgNone, deepCloneAstNode(ctx->pool, node->iteratorExpr.type), NULL, NULL);
        AstNode *func = makeOperatorOverload(ctx->pool,
                                             &body->loc,
                                             opCallOverload,
                                             NULL,
                                             retType,
                                             bodyStmt,
                                             flgNone,
                                             NULL,
                                             NULL);
        node->iteratorExpr.next = func;
    }
    astVisit(visitor, node->iteratorExpr.next);

    if (node->iteratorExpr.truthy && !nodeIs(node->iteratorExpr.truthy, FuncDecl)) {
        AstNode *body = node->iteratorExpr.truthy;
        AstNode *bodyStmt = nodeIs(body, BlockStmt)
                                ? body
                                : makeBlockStmt(ctx->pool,
                                                &body->loc,
                                                makeReturnAstNode(ctx->pool, &body->loc, flgNone, body, NULL, NULL),
                                                NULL,
                                                NULL);
        AstNode *boolType = makePrimitiveTypeAst(ctx->pool, &body->loc, flgNone, prtBool, NULL, NULL);
        AstNode *func = makeOperatorOverload(ctx->pool,
                                             &body->loc,
                                             opTruthy,
                                             NULL,
                                             boolType,
                                             bodyStmt,
                                             flgNone,
                                             NULL,
                                             NULL);
        node->iteratorExpr.truthy = func;
    }
    astVisit(visitor, node->iteratorExpr.truthy);
}

void shakeClosureExpr(AstVisitor *visitor, AstNode *node)
{
    astVisitFallbackVisitAll(visitor, node);
}

static void shakeReturnStmt(AstVisitor *visitor, AstNode *node)
{
    astVisitFallbackVisitAll(visitor, node);
}

static void shakeGenericDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->genericDecl.decl, *gparams = node->genericDecl.params,
            *gparam = gparams;
    if (findAttribute(node, S_task) || findAttribute(decl, S_task)) {
        logError(ctx->L,
                 &node->loc,
                 "invalid `@task` function, generic functions cannot be tasks",
                 NULL);
        return;
    }
    node->genericDecl.paramsCount = countAstNodes(gparams);
    node->genericDecl.name = getDeclarationName(node->genericDecl.decl);
    if (findAttribute(node, S_pure))
        node->genericDecl.decl->flags |= flgPure;

    astVisit(visitor, gparams);

    if (!nodeIs(decl, FuncDecl)) {
        astVisit(visitor, decl);
        return;
    }
    typedef struct {
        u16 idx;
        bool innerType;
    } InferenceInfo;
    InferenceInfo *inferrable =
        callocOrDie(sizeof(InferenceInfo), node->genericDecl.paramsCount);
    int index = -1;
    for (u16 i = 0; gparam; gparam = gparam->next, i++) {
        node->flags |= (gparam->flags & flgVariadic);
        decl->flags |= (gparam->flags & flgVariadic);

        if (gparam->genericParam.defaultValue != NULL) {
            logWarning(ctx->L,
                       &gparam->loc,
                       "default values on function generic decl params not "
                       "supported, ignoring",
                       NULL);
        }
        AstNode *fparam = decl->funcDecl.signature->params;
        for (u16 j = 0; fparam; fparam = fparam->next, j++) {
            AstNode *type = fparam->funcParam.type;
            if (hasFlag(fparam, Variadic))
                break;
            inferrable[i].idx = inferGenericParamFromFuncParam(
                gparam, type, j, &inferrable[i].innerType);
            if (inferrable[i].idx) {
                index = index == -1 ? i : index;
                break;
            }
        }
        if (!inferrable[i].idx)
            index = -1;
    }

    if (index >= 0) {
        gparam = gparams;
        for (u64 i = 0; gparam; gparam = gparam->next, i++) {
            if (i >= index) {
                gparam->genericParam.inferIndex = inferrable[i].idx;
                gparam->genericParam.innerType = inferrable[i].innerType;
            }
        }
    }
    else if (decl->funcDecl.operatorOverload != opInvalid &&
             decl->funcDecl.operatorOverload != opIs &&
             decl->funcDecl.operatorOverload != opCast) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported generic function overload, must be inferrable",
                 NULL);
        return;
    }

    free(inferrable);
    node->genericDecl.inferrable = (i16)index;
    decl->parentScope = node;
    astVisit(visitor, decl);
}

static FunctionSignature *makeTestFunctionSignature(ShakeAstContext *ctx,
                                                    const FileLoc *loc)
{
    AstNode *voidType =
        makeVoidAstNode(ctx->pool, loc, flgNone, NULL, NULL);
    AstNode *resultType = makeAstNode(
        ctx->pool,
        loc,
        &(AstNode){.tag = astResultType,
                   .flags = flgTypeAst,
                   .resultType = {.target = voidType}});
    return makeFunctionSignature(
        ctx->pool, &(FunctionSignature){.ret = resultType});
}

static void shakeTestDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    u64 idx = node->loc.begin.row;
    if (idx <= ctx->lastTestId)
        idx = ++ctx->lastTestId;
    else
        ctx->lastTestId = idx;

    AstNode *body = node->testDecl.body;
    astVisit(visitor, body);
    csAssert0(nodeIs(body, BlockStmt));

    node->tag = astFuncDecl;
    node->flags |= flgTestContext;
    node->funcDecl.body = body;
    node->funcDecl.signature = makeTestFunctionSignature(ctx, &node->loc);

    if (findAttribute(node, S_setup)) {
        if (ctx->testSetup != NULL) {
            logError(ctx->L,
                     &node->loc,
                     "multiple test setup functions are not allowed",
                     NULL);
            return;
        }

        node->funcDecl.name = S_testSetup;
        ctx->testSetup = makeTupleExpr(
            ctx->pool,
            &node->loc,
            flgNone,
            makeStringLiteral(ctx->pool,
                              &node->loc,
                              node->testDecl.name ?: S_testSetup,
                              makeResolvedIdentifier(
                                  ctx->pool,
                                  &node->loc,
                                  S_testSetup,
                                  0,
                                  node,
                                  makeUnsignedIntegerLiteral(
                                      ctx->pool, &node->loc, 0x1, NULL, NULL),
                                  NULL),
                              NULL),
            NULL,
            NULL);
    }
    else if (findAttribute(node, S_cleanup)) {
        if (ctx->testCleanup != NULL) {
            logError(ctx->L,
                     &node->loc,
                     "multiple test cleanup functions are not allowed",
                     NULL);
            return;
        }

        node->funcDecl.name = S_testCleanup;
        ctx->testCleanup = makeTupleExpr(
            ctx->pool,
            &node->loc,
            flgNone,
            makeStringLiteral(ctx->pool,
                              &node->loc,
                              node->testDecl.name ?: S_testCleanup,
                              makeResolvedIdentifier(
                                  ctx->pool,
                                  &node->loc,
                                  S_testCleanup,
                                  0,
                                  node,
                                  makeUnsignedIntegerLiteral(
                                      ctx->pool, &node->loc, 0x2, NULL, NULL),
                                  NULL),
                              NULL),
            NULL,
            NULL);
    }
    else {
        char buf[128];
        bool isDisabled = findAttribute(node, S_disabled) != NULL;
        snprintf(buf, 128, "%" PRIu64, idx);
        cstring prefix = isDisabled ? "DISABLED_" : "";
        cstring name = makeStringConcat(ctx->strings, prefix, "test", buf);
        cstring testName = node->testDecl.name ?: name;
        u32 flags = isDisabled ? 0x4 : 0;
        node->funcDecl.name = name;
        insertAstNode(
            &ctx->testCases,
            makeTupleExpr(
                ctx->pool,
                &node->loc,
                flgNone,
                makeStringLiteral(ctx->pool,
                                  &node->loc,
                                  testName,
                                  makeResolvedIdentifier(
                                      ctx->pool,
                                      &node->loc,
                                      name,
                                      0,
                                      node,
                                      makeUnsignedIntegerLiteral(
                                          ctx->pool, &node->loc, flags, NULL, NULL),
                                      NULL),
                                  NULL),
                NULL,
                NULL));
    }
}

static void shakeExceptionDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->exception.body;
    AstNodeList members = {}, assigns = {};
    AstNode *params = node->exception.params, *param = params;
    bool hasDefault = false;
    cstring name = node->exception.name;

    for (; param; param = param->next) {
        AstNode *def = param->funcParam.def;
        if (hasDefault && def == NULL) {
            logError(ctx->L,
                     &param->loc,
                     "exception argument '{s}' must have a default initializer",
                     (FormatArg[]){{.s = param->_name}});
            continue;
        }
        hasDefault = hasDefault || def != NULL;
        insertAstNode(
            &members,
            makeStructField(ctx->pool,
                            &param->loc,
                            param->_name,
                            flgNone,
                            deepCloneAstNode(ctx->pool, param->funcParam.type),
                            NULL,
                            NULL));
        insertAstNode(&assigns, makeExceptionAssignMember(ctx, param));
    }

    // func `init`() {}
    insertAstNode(&members,
                  makeExceptionInit(ctx, node->_name, params, assigns.first));

    if (!nodeIs(body, BlockStmt)) {
        body = makeBlockStmt(
            ctx->pool,
            &body->loc,
            makeReturnAstNode(ctx->pool, &body->loc, flgNone, body, NULL, NULL),
            NULL,
            NULL);
    }

    insertAstNode(
        &members,
        makeFunctionDecl(
            ctx->pool,
            &node->loc,
            S_what,
            NULL,
            makeStringTypeAst(ctx->pool, builtinLoc(), flgNone, NULL, NULL),
            body,
            flgNone,
            NULL,
            NULL));

    node->tag = astClassDecl;
    clearAstBody(node);
    node->classDecl.name = name;
    node->classDecl.members = members.first;
    node->classDecl.base =
        makePath(ctx->pool, builtinLoc(), S_Exception, flgNone, NULL);
    astVisit(visitor, node);
}

static void shakeForStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *variable = node->forStmt.var, *body = node->forStmt.body;
    u64 flags = variable->flags;
    AstNode *name = variable->varDecl.names;
    if (name->next) {
        AstNode *it = name->next;
        variable->varDecl.name = name->ident.value;
        variable->loc = name->loc;
        name->next = NULL;
        for (; it;) {
            variable->next = makeVarDecl(ctx->pool,
                                         &it->loc,
                                         flags,
                                         it->ident.value,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL);
            name = it;
            it = it->next;
            name->next = NULL;
            variable = variable->next;
        }
    }
    else {
        variable->varDecl.name = name->ident.value;
    }
    astVisit(visitor, node->forStmt.range);

    if (!hasFlag(node, Comptime) && !nodeIs(body, BlockStmt))
        node->forStmt.body =
            makeBlockStmt(ctx->pool, &body->loc, body, NULL, NULL);
    astVisit(visitor, node->forStmt.body);
}

static void shakeBlockStmt(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstModifier prev = ctx->block;
    astModifierInit(&ctx->block, node);

    AstNode *stmt = node->blockStmt.stmts;
    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        astVisit(visitor, stmt);
    }

    ctx->block = prev;
}

static void shakeArrayType(AstVisitor *visitor, AstNode *node)
{
    astVisitFallbackVisitAll(visitor, node);
}

static void shakeStringExpr(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    AstNode *part = node->stringExpr.parts;
    if (nodeIs(part, StringLit) && part->next == NULL) {
        node->tag = astStringLit;
        node->stringLiteral = part->stringLiteral;
        return;
    }

    AstNode *var = makeStrExprBuilder(ctx, node),
            *sb = makeResolvedPath(ctx->pool,
                                   &node->loc,
                                   var->varDecl.name,
                                   flgNone,
                                   var,
                                   NULL,
                                   var->type);

    for (; part;) {
        if (nodeIs(part, StringLit) && part->stringLiteral.value[0] == '\0') {
            part = part->next;
            continue;
        }
        sb = makeBinaryExpr(ctx->pool,
                            locExtend(&node->loc, &part->loc),
                            flgNone,
                            sb,
                            opShl,
                            part,
                            NULL,
                            NULL);
        part = part->next;
        sb->binaryExpr.rhs->next = NULL;
    }

    var->next = makeExprStmt(ctx->pool, &node->loc, flgNone, sb, NULL, NULL);
    astModifierAdd(&ctx->block, var);

    AstNode *path =
        makePath(ctx->pool, &node->loc, var->varDecl.name, var->flags, NULL);
    if (ctx->borrowedStringExpr == node) {
        // Only a receiver (`f"..".s()`): keep ownership so the builder is dropped
        ctx->borrowedStringExpr = NULL;
        path->next = node->next;
        *node = *path;
        return;
    }

    node->tag = astUnaryExpr;
    node->unaryExpr.isPrefix = true;
    node->unaryExpr.op = opMove;
    node->unaryExpr.operand = path;
}

static void shakeMemberExpr(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    if (nodeIs(node->memberExpr.target, StringExpr))
        ctx->borrowedStringExpr = node->memberExpr.target;
    astVisitFallbackVisitAll(visitor, node);
}

static void shakeClassOrStructDecl(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    ctx->cls = getDeclarationName(node);
    ctx->stack.currentClsOrStruct = node;
    AstNodeList outerWrappers = ctx->taskWrappers;
    ctx->taskWrappers = (AstNodeList){};
    astVisitFallbackVisitAll(visitor, node);
    AstNode *wrappers = takeTaskWrappers(visitor);
    if (wrappers) {
        if (node->structDecl.members)
            getLastAstNode(node->structDecl.members)->next = wrappers;
        else
            node->structDecl.members = wrappers;
    }
    ctx->taskWrappers = outerWrappers;
    ctx->stack.currentClsOrStruct = NULL;
    AstNode *builtins = createClassOrStructBuiltins(ctx->pool, node);
    if (node->structDecl.members) {
        getLastAstNode(node->structDecl.members)->next = builtins;
    }
    else
        node->structDecl.members = builtins;
}

static void shakeProgram(AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);

    astVisitManyNodes(visitor, node->program.top);

    AstNode *decl = node->program.decls;
    astModifierInit(&ctx->root, node);

    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        astVisit(visitor, decl);
    }

    AstNode *wrappers = takeTaskWrappers(visitor);
    if (wrappers) {
        if (node->program.decls)
            getLastAstNode(node->program.decls)->next = wrappers;
        else
            node->program.decls = wrappers;
    }
}

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    ShakeAstContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;

    func(visitor, node);

    ctx->stack = stack;
}

static bool buildTestsVariable(ShakeAstContext *ctx, AstNode *program)
{
    if (ctx->testSetup) {
        insertAstNodeFront(&ctx->testCases, ctx->testSetup);
    }

    if (ctx->testCleanup) {
        insertAstNode(&ctx->testCases, ctx->testCleanup);
    }

    if (!ctx->testCases.first) {
        // there are no test cases to run
        return false;
    }

    getLastAstNode(program->program.decls)->next = makeVarDecl(
        ctx->pool,
        builtinLoc(),
        flgTestContext | flgTopLevelDecl,
        S_allTestCases,
        NULL,
        makeArrayExpr(
            ctx->pool, builtinLoc(), flgNone, ctx->testCases.first, NULL, NULL),
        NULL,
        NULL);
    return true;
}

AstNode *shakeAstNode(CompilerDriver *driver, AstNode *node)
{
    ShakeAstContext context = {
        .L = driver->L,
        .pool = driver->pool,
        .strings = driver->strings,
        .exceptionTrace =
            driver->options.debug || driver->options.optimizationLevel != O3,
        .path = node->loc.fileName,
        .mod =
            node->program.module ? node->program.module->moduleDecl.name : NULL,
    };

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = shakeProgram,
        [astVarDecl] = shakeVariableDecl,
        [astIfStmt] = shakeIfStmt,
        [astWhileStmt] = shakeWhileStmt,
        [astForStmt] = shakeForStmt,
        [astFuncDecl] = shakeFuncDecl,
        [astFieldDecl] = shakeFieldDecl,
        [astUnionDecl] = shakeUnionDecl,
        [astGenericDecl] = shakeGenericDecl,
        [astTestDecl] = shakeTestDecl,
        [astException] = shakeExceptionDecl,
        [astBlockStmt] = shakeBlockStmt,
        [astStringExpr] = shakeStringExpr,
        [astMemberExpr] = shakeMemberExpr,
        [astClosureExpr] = shakeClosureExpr,
        [astIteratorExpr] = shakeIteratorExpr,
        [astCallExpr] = shakeCallExpr,
        [astGroupExpr] = shakeGroupExpr,
        [astBinaryExpr] = shakeBinaryExpr,
        [astTernaryExpr] = shakeTernaryExpr,
        [astCatchExpr] = shakeCatchExpr,
        [astExprStmt] = shakeExprStmt,
        [astMatchStmt] = shakeMatchStmt,
        [astReturnStmt] = shakeReturnStmt,
        [astClassDecl] = shakeClassOrStructDecl,
        [astStructDecl] = shakeClassOrStructDecl,
        [astArrayType] = shakeArrayType,
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);
    if (!hasFlag(node, ImportedModule)) {
        driver->hasTestCases = buildTestsVariable(&context, node);
    }
    return node;
}
