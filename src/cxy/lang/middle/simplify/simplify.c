//
// Created by Carter Mbotho on 2024-01-10.
//

#include "../defer.h"
#include "../n2e.h"

#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

typedef struct SimplifyContext {
    Log *L;
    TypeTable *types;
    StrPool *strings;
    MemPool *pool;
    AstModifier root;
    AstModifier block;
    N2eContext n2e;
    AstNodeList init;
    AstNodeList fini;  // drops of destructible module globals, reverse order
    AstNodeList tfini; // same for @thread globals, run when a thread ends
    AstNodeList *startup;
    AstNode *returnVar;
    bool traceMemory;
    struct {
        AstNodeList exprs;
        bool visiting;
    } whileCond;
    union {
        struct {
            AstNode *currentFunction;
        };
        struct {
            AstNode *currentFunction;
        } stack;
    };
} SimplifyContext;

static bool isRedundantExpression(AstNode *node);

static bool isRedundantStatement(AstNode *node);

static bool compareNodeToExternDecl(const void *lhs, const void *rhs)
{
    return ((NodeToExternDecl *)lhs)->node == ((NodeToExternDecl *)rhs)->node;
}

static inline bool nodeIsComplexExpr(const AstNode *node)
{
    return !nodeIsLeftValue(node) && !isLiteralExprExt(node) &&
           !isSizeofExpr(node);
}

static bool nodeNeedsTemporaryVar(const AstNode *node)
{
    if (nodeIs(node, PointerOf) || nodeIs(node, ReferenceOf))
        return nodeNeedsTemporaryVar(node->unaryExpr.operand);

    if (nodeIsLeftValue(node) || isLiteralExprExt(node) || isSizeofExpr(node))
        return false;

    // `ex!` is a borrow of the catch result, never an owned rvalue
    if (nodeIs(node, BackendCall) &&
        node->backendCallExpr.func == bfiCurrentException)
        return false;

    if (nodeIs(node, UnaryExpr))
        return node->unaryExpr.op == opMove &&
               nodeNeedsTemporaryVar(node->unaryExpr.operand);
    return true;
}

static AstNode *makeTemporaryRef(SimplifyContext *ctx, AstNode *var)
{
    return makeResolvedIdentifier(ctx->pool,
                                  &var->loc,
                                  var->_namedNode.name,
                                  0,
                                  var,
                                  NULL,
                                  var->type);
}

// Declares a temporary variable of the given type initialised with `init`,
// ahead of the current statement. `expr` is the expression it stands for.
// Inside a while condition the variable is assigned again each time the
// condition is evaluated.
static AstNode *hoistToTemporary(SimplifyContext *ctx,
                                 AstNode *expr,
                                 AstNode *init,
                                 const Type *type,
                                 cstring prefix,
                                 u64 flags,
                                 bool reassignInWhile)
{
    AstNode *var = makeVarDecl(ctx->pool,
                               &expr->loc,
                               flags,
                               makeAnonymousVariable(ctx->strings, prefix),
                               NULL,
                               init,
                               NULL,
                               type);
    if (reassignInWhile && ctx->whileCond.visiting) {
        insertAstNode(&ctx->whileCond.exprs,
                      makeAssignExpr(ctx->pool,
                                     &expr->loc,
                                     flgNone,
                                     makeTemporaryRef(ctx, var),
                                     opAssign,
                                     deepCloneAstNode(ctx->pool, expr),
                                     NULL,
                                     var->type));
    }
    astModifierAdd(&ctx->block, var);
    return var;
}

static bool isVirtualDispatch(const AstNode *target, const AstNode *member)
{
    const Type *type = stripReference(target->type);
    if (!isClassOrStructType(type))
        return false;
    AstNode *decl = getTypeDecl(type), *func = getTypeDecl(member->type);

    return hasFlag(decl, Virtual) && hasFlag(func, Virtual);
}

static AstNode *virtualDispatch(SimplifyContext *ctx,
                                AstNode *target,
                                AstNode *member)
{
    // [target].vtable.[member](target, [args])
    AstNode *decl = getTypeDecl(target->type),
            *vTable = decl->classDecl.members,
            *vTableFunc = findMemberDeclInType(vTable->type->pointer.pointed,
                                               member->ident.value);
    AstNode *base = NULL;
    if (nodeIs(target, ReferenceOf)) {
        base = deepCloneAstNode(ctx->pool, target->unaryExpr.operand);
    }
    else {
        base = deepCloneAstNode(ctx->pool, target);
    }

    return makeMemberExpr(ctx->pool,
                          &target->loc,
                          flgConst,
                          makeMemberExpr(ctx->pool,
                                         &target->loc,
                                         flgConst,
                                         base,
                                         makeResolvedIdentifier(ctx->pool,
                                                                &target->loc,
                                                                S_vtable,
                                                                0,
                                                                vTable,
                                                                NULL,
                                                                vTable->type),
                                         NULL,
                                         vTable->type),
                          makeResolvedIdentifier(ctx->pool,
                                                 &target->loc,
                                                 vTableFunc->_namedNode.name,
                                                 0,
                                                 vTableFunc,
                                                 NULL,
                                                 vTableFunc->type),
                          NULL,
                          vTableFunc->type);
}

static void simplifyCastExpression(SimplifyContext *ctx,
                                   AstNode *node,
                                   const Type *type)
{
    const Type *from = unwrapType(node->type, NULL);

    if (isSliceType(type) || isArrayType(type) || isSliceType(from) || isArrayType(from) || isClosureType(type) || isClosureType(from))
        return;

#if defined(LLVM_BACKEND)
    if ((type == from) || typeIs(type, Func))
        return;
#else
    if ((type == from) && !typeIs(type, Func))
        return;
#endif

    if (typeIs(from, Union)) {
        u32 idx = findUnionTypeIndex(from, type);
        if (idx == UINT32_MAX) {
            logError(ctx->L,
                     &node->loc,
                     "cannot cast type {t} to union type {t}",
                     (FormatArg[]){{.t = type}, {.t = from}});
            return;
        }

        node->memberExpr.target = deepCloneAstNode(ctx->pool, node);
        node->tag = astMemberExpr;
        node->memberExpr.member =
            makeIntegerLiteral(ctx->pool,
                               &node->loc,
                               idx,
                               NULL,
                               getPrimitiveType(ctx->types, prtU64));
        node->type = type;
    }
    else {
        AstNode *expr = deepCloneAstNode(ctx->pool, node);
        if (typeIs(from, Array)) {
            node->tag = astTypedExpr;
            node->typedExpr.expr = expr;
            node->typedExpr.type =
                makeTypeReferenceNode(ctx->pool, type, &node->loc);
        }
        else {
            node->tag = astCastExpr;
            node->castExpr.expr = expr;
            node->castExpr.to =
                makeTypeReferenceNode(ctx->pool, type, &node->loc);
        }
        node->type = type;
    }
}

static void simplifyCondition(SimplifyContext *ctx, AstNode *cond)
{
    AstNode *rhs = NULL, *lhs = deepCloneAstNode(ctx->pool, cond);
    if (isIntegerType(cond->type)) {
        rhs = makeIntegerLiteral(ctx->pool, &cond->loc, 0, NULL, cond->type);
    }
    else if (isFloatType(cond->type)) {
        rhs = makeFloatLiteral(ctx->pool, &cond->loc, 0.0, NULL, cond->type);
    }
    else if (isCharacterType(cond->type)) {
        rhs = makeCharLiteral(ctx->pool, &cond->loc, 0, NULL, cond->type);
    }
    else if (isPointerType(cond->type)) {
        rhs = makeNullLiteral(ctx->pool, &cond->loc, NULL, cond->type);
    }
    else {
        unreachable("Shouldn't be a thing!");
    }
    cond->tag = astBinaryExpr;
    cond->binaryExpr.op = opNe;
    cond->binaryExpr.lhs = lhs;
    cond->binaryExpr.rhs = rhs;
}

static AstNode *wrapLogicalOperandInStmtExpr(SimplifyContext *ctx,
                                             AstNode *expr)
{
    AstNode *exprStmt =
        makeExprStmt(ctx->pool, &expr->loc, flgNone, expr, NULL, expr->type);
    AstNode *block =
        makeBlockStmt(ctx->pool, &expr->loc, exprStmt, NULL, expr->type);
    AstNode *stmtExpr =
        makeStmtExpr(ctx->pool, &expr->loc, flgNone, block, NULL, expr->type);

    block->flags |= flgBlockReturns;
    expr->parentScope = exprStmt;
    exprStmt->parentScope = block;
    block->parentScope = stmtExpr;

    return stmtExpr;
}

static bool isRedundantExpressionMany(AstNode *node)
{
    AstNode *it = node;
    for (; it; it = it->next) {
        if (isRedundantExpression(it))
            return true;
    }

    return false;
}

static bool isRedundantStructExpression(AstNode *node)
{
    AstNode *it = node->structExpr.fields;
    for (; it; it = it->next) {
        if (isRedundantExpression(it->fieldExpr.value))
            return true;
    }

    return false;
}

static bool isRedundantExpression(AstNode *node)
{
    if (node == NULL)
        return true;

    if (hasFlag(node, BlockValue))
        return false;

    switch (node->tag) {
    case astNullLit:
    case astCharLit:
    case astBoolLit:
    case astIntegerLit:
    case astFloatLit:
    case astStringLit:
    case astIdentifier:
    case astPath:
        return true;
    case astMemberExpr:
        return isRedundantExpression(node->memberExpr.target);
    case astIndexExpr:
        return isRedundantExpression(node->indexExpr.target);
    case astArrayExpr:
        return isRedundantExpressionMany(node->arrayExpr.elements);
    case astTupleExpr:
        return isRedundantExpressionMany(node->tupleExpr.elements);
    case astStructExpr:
        return isRedundantStructExpression(node);
    case astGroupExpr:
        return isRedundantExpression(node->groupExpr.expr);
    case astStmtExpr:
        return isRedundantStatement(node->stmtExpr.stmt);
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        return op != opPreInc && op != opPreDec && op != opPostInc &&
               op != opPostDec && op != opDelete &&
               isRedundantExpression(node->unaryExpr.operand);
    }

    case astBinaryExpr:
        return isRedundantExpression(node->binaryExpr.lhs) ||
               isRedundantExpression(node->binaryExpr.rhs);

    case astTernaryExpr:
        return isRedundantExpression(node->ternaryExpr.cond) ||
               isRedundantExpression(node->ternaryExpr.body) ||
               isRedundantExpression(node->ternaryExpr.otherwise);
    default:
        return false;
    }
}

bool isRedundantStatement(AstNode *node)
{
    if (nodeIs(node, BlockStmt))
        return node->blockStmt.stmts == NULL;

    if (!nodeIs(node, ExprStmt))
        return false;

    AstNode *expr = node->exprStmt.expr;
    if (nodeIs(expr, ExprStmt))
        return isRedundantStatement(expr);

    return isRedundantExpression(expr);
}


static void createModuleInit(AstVisitor *visitor, AstNode *program)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    cstring name =
        program->program.module
            ? makeStringConcat(ctx->strings,
                               S___init,
                               "_",
                               program->program.module->moduleDecl.name)
            : S___init;
    AstNode *func = makeFunctionDecl(
        ctx->pool,
        builtinLoc(),
        name,
        NULL,
        makeVoidAstNode(
            ctx->pool, builtinLoc(), flgNone, NULL, makeVoidType(ctx->types)),
        makeBlockStmt(ctx->pool, builtinLoc(), ctx->init.first, NULL, NULL),
        flgTopLevelDecl | flgPublic | flgConstructor,
        NULL,
        NULL);

    astVisit(visitor, func->funcDecl.body);

    func->type = makeFuncType(
        ctx->types,
        &(Type){.tag = typFunc,
                .name = name,
                .func = {.retType = makeVoidType(ctx->types), .decl = func}});
    func->parentScope = program;

    AstNode *ext =
        makeAstNode(ctx->pool,
                    &func->loc,
                    &(AstNode){.tag = astExternDecl,
                               .type = func->type,
                               .flags = func->flags | flgPublic | flgModuleInit,
                               .next = program->program.decls,
                               .externDecl.func = func});
    program->program.decls = ext;
    getLastAstNode(ext)->next = func;
}

// Teardown counterpart of createModuleInit: __fini_<module> (process exit) or
// __tfini_<module> (thread exit). Detected by name in the backend.
static void createModuleFini(AstVisitor *visitor,
                             AstNode *program,
                             cstring prefix,
                             AstNodeList *stmts)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    cstring name =
        program->program.module
            ? makeStringConcat(ctx->strings,
                               prefix,
                               "_",
                               program->program.module->moduleDecl.name)
            : prefix;
    AstNode *func = makeFunctionDecl(
        ctx->pool,
        builtinLoc(),
        name,
        NULL,
        makeVoidAstNode(
            ctx->pool, builtinLoc(), flgNone, NULL, makeVoidType(ctx->types)),
        makeBlockStmt(ctx->pool, builtinLoc(), stmts->first, NULL, NULL),
        flgTopLevelDecl | flgPublic,
        NULL,
        NULL);

    astVisit(visitor, func->funcDecl.body);

    func->type = makeFuncType(
        ctx->types,
        &(Type){.tag = typFunc,
                .name = name,
                .func = {.retType = makeVoidType(ctx->types), .decl = func}});
    func->parentScope = program;

    AstNode *ext =
        makeAstNode(ctx->pool,
                    &func->loc,
                    &(AstNode){.tag = astExternDecl,
                               .type = func->type,
                               .flags = func->flags | flgPublic,
                               .next = program->program.decls,
                               .externDecl.func = func});
    program->program.decls = ext;
    getLastAstNode(ext)->next = func;
}

static AstNode *makeGlobalRefNode(SimplifyContext *ctx, AstNode *var)
{
    return makeResolvedIdentifier(
        ctx->pool, &var->loc, var->varDecl.name, 0, var, NULL, var->type);
}

// Records `drop(g); g = null` for a destructible module global. Prepended so
// teardown runs in reverse order of declaration.
static void recordGlobalFini(SimplifyContext *ctx, AstNode *node)
{
    if (hasFlag(node, Extern) || hasFlag(node, Comptime) || node->type == NULL)
        return;
    const Type *type = resolveUnThisUnwrapType(node->type);
    if (type == NULL || isReferenceType(type) || !isDestructible(type))
        return;

    const Type *voidType = makeVoidType(ctx->types);
    AstNode *first = makeExprStmt(
        ctx->pool,
        &node->loc,
        flgNone,
        makeBackendCallExpr(ctx->pool,
                            &node->loc,
                            flgForced,
                            bfiDrop,
                            makeGlobalRefNode(ctx, node),
                            voidType),
        NULL,
        voidType);
    AstNode *last = first;
    if (isClassType(type)) {
        // Idempotent: a second run (or a late use) sees null, not a freed object
        last = makeExprStmt(
            ctx->pool,
            &node->loc,
            flgNone,
            makeAssignExpr(
                ctx->pool,
                &node->loc,
                flgNone,
                makeGlobalRefNode(ctx, node),
                opAssign,
                makeNullLiteral(ctx->pool, &node->loc, NULL, node->type),
                NULL,
                node->type),
            NULL,
            voidType);
        first->next = last;
    }

    AstNodeList *list =
        findAttribute(node, S_thread) != NULL ? &ctx->tfini : &ctx->fini;
    last->next = list->first;
    list->first = first;
    if (list->last == NULL)
        list->last = last;
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->program.decls;
    AstNodeList externals = {};

    astVisit(visitor, node->program.module);
    astVisitManyNodes(visitor, node->program.top);

    astModifierInit(&ctx->root, node);

    if (isBuiltinsInitialized()) {
        insertAstNode(&externals,
                      n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_ref));
        insertAstNode(&externals,
                      n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_drop));

        if (ctx->traceMemory) {
            insertAstNode(
                &externals,
                n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_get_trace));
            insertAstNode(
                &externals,
                n2eMakeExternReferenceToBuiltin(&ctx->n2e, S_sptr_drop_trace));
        }
    }

    for (; decl; decl = decl->next) {
        astModifierNext(&ctx->root, decl);
        if (nodeIsNoop(decl)) {
            astModifierRemoveCurrent(&ctx->root);
            continue;
        }

        astVisit(visitor, decl);
    }

    if (ctx->init.first != NULL) {
        // only create initializer when there is data to initialize
        createModuleInit(visitor, node);
    }
    if (ctx->fini.first != NULL)
        createModuleFini(visitor, node, "__fini", &ctx->fini);
    if (ctx->tfini.first != NULL)
        createModuleFini(visitor, node, "__tfini", &ctx->tfini);

    if (isBuiltinsInitialized()) {
        externals.last->next = node->program.decls;
        node->program.decls = externals.first;
    }
}

static void visitCastExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->castExpr.expr, *to = node->castExpr.to;
    astVisit(visitor, expr);

    if (isUnionType(to->type)) {
        const Type *type = stripReference(expr->type);
        if (to->type == type) {
            node->tag = astBackendCall;
            node->backendCallExpr.func = bfiCopy;
            node->backendCallExpr.args = expr;
        }
        else {
            u32 idx = findUnionTypeIndex(to->type, type);
            csAssert0(idx != UINT32_MAX);
            node->tag = astUnionValueExpr;
            node->unionValue.idx = idx;
            node->unionValue.value = expr;
        }
    }
    // else if (isUnionType(expr->type) && !hasFlag(node, BitUnionCast)) {
    //     AstNode *builtinAssert = findBuiltinDecl(S___cxy_assert);
    //     csAssert0(builtinAssert);
    //     u32 idx = findUnionTypeIndex(expr->type, stripReference(to->type));
    //     csAssert0(idx != UINT32_MAX);
    //     AstNodeList args = {};
    //     insertAstNode(
    //         &args,
    //         makeBinaryExpr(
    //             ctx->pool,
    //             &expr->loc,
    //             flgNone,
    //             makeMemberExpr(ctx->pool,
    //                            &expr->loc,
    //                            flgNone,
    //                            deepCloneAstNode(ctx->pool, expr),
    //                            makeUnsignedIntegerLiteral(
    //                                ctx->pool,
    //                                &expr->loc,
    //                                0,
    //                                NULL,
    //                                getPrimitiveType(ctx->types, prtU64)),
    //                            NULL,
    //                            getPrimitiveType(ctx->types, prtU64)),
    //             opEq,
    //             makeUnsignedIntegerLiteral(
    //                 ctx->pool,
    //                 &expr->loc,
    //                 idx,
    //                 NULL,
    //                 getPrimitiveType(ctx->types, prtU64)),
    //             NULL,
    //             getPrimitiveType(ctx->types, prtBool)));
    //     insertAstNode(&args,
    //                   makeStringLiteral(ctx->pool,
    //                                     &expr->loc,
    //                                     expr->loc.fileName,
    //                                     NULL,
    //                                     makeStringType(ctx->types)));
    //     insertAstNode(
    //         &args,
    //         makeUnsignedIntegerLiteral(ctx->pool,
    //                                    &expr->loc,
    //                                    expr->loc.begin.row,
    //                                    NULL,
    //                                    getPrimitiveType(ctx->types, prtU64)));
    //     insertAstNode(
    //         &args,
    //         makeUnsignedIntegerLiteral(ctx->pool,
    //                                    &expr->loc,
    //                                    expr->loc.begin.col,
    //                                    NULL,
    //                                    getPrimitiveType(ctx->types, prtU64)));

    //     AstNode *assert = makeExprStmt(
    //         ctx->pool,
    //         &node->loc,
    //         flgNone,
    //         makeCallExpr(ctx->pool,
    //                      &node->loc,
    //                      makeResolvedIdentifier(ctx->pool,
    //                                             &node->loc,
    //                                             S___cxy_assert,
    //                                             0,
    //                                             builtinAssert,
    //                                             NULL,
    //                                             builtinAssert->type),
    //                      args.first,
    //                      node->flags,
    //                      NULL,
    //                      builtinAssert->type->func.retType),
    //         NULL,
    //         makeVoidType(ctx->types));
    //     astModifierAdd(&ctx->block, assert);
    // }
}

static void visitIndexExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->indexExpr.target, *index = node->indexExpr.index;
    astVisit(visitor, target);
    astVisit(visitor, index);
    if (nodeNeedsTemporaryVar(index)) {
        AstNode *var =
            hoistToTemporary(ctx,
                             index,
                             deepCloneAstNode(ctx->pool, index),
                             index->type,
                             "_idx",
                             flgTemporary | (index->flags & flgConst),
                             true);
        index->tag = astIdentifier;
        index->ident.value = var->_namedNode.name;
        index->ident.resolvesTo = var;
        index->flags |= flgMove;
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    if (isClosureType(callee->type)) {
        callee->parentScope = node;
        astVisit(visitor, callee);
        if (nodeNeedsTemporaryVar(callee)) {
            // `.fn` and `.env` are both read from the callee, evaluate it once
            // a closure variable has the canonical closure type, like
            // checkVarDecl, the lambda may be wrapped in a group `(...)()`
            AstNode *value = resolveGroupExpr(callee);
            const Type *type = nodeIs(value, ClosureValue) &&
                                       value->closureValue.canonicalType
                                   ? value->closureValue.canonicalType
                                   : callee->type;
            AstNode *var =
                hoistToTemporary(ctx, callee, callee, type, "_c", flgNone, true);
            node->callExpr.callee = makeTemporaryRef(ctx, var);
            node->callExpr.callee->parentScope = node;
        }
        astVisitManyNodes(visitor, args);
        return;
    }
    AstNode *func = callee->type->func.decl;
    callee->parentScope = node;
    astVisit(visitor, callee);
    AstNode *arg = args,
            *params = nodeIs(func, FuncDecl) ? func->funcDecl.signature->params
                                             : func->funcType.params,
            *param = params;
    if (nodeIs(callee, MemberExpr) && param && param->_name == S_this)
        param = param->next;
    for (; arg && param; arg = arg->next, param = param->next) {
        if (hasFlag(param, Variadic)) {
            break;
        }
        const Type *paramType =
            param->type ?: (param->funcParam.type ? param->funcParam.type->type : NULL);
        if (paramType == NULL || typeIs(paramType, Auto))
            continue;
        astVisit(visitor, arg);

        const Type *left = resolveUnThisUnwrapType(paramType),
                   *right = resolveUnThisUnwrapType(arg->type);
        if (left != right) {
            simplifyCastExpression(ctx, arg, left);
        }
    }

    for (; arg; arg = arg->next) {
        astVisit(visitor, arg);
    }

    csAssert0(func);
    AstNode *this = getMemberFunctionThis(func);
    if (this == NULL || !nodeIs(callee, MemberExpr))
        return;

    if (nodeIs(callee->memberExpr.target, MemberExpr)) {
        AstNode *innerMem = callee->memberExpr.target->memberExpr.member;
        if (nodeIs(innerMem, Identifier) && innerMem->ident.value == S_vtable)
            return;
    }

    csAssert0(nodeIs(callee, MemberExpr));
    bool isOptional = callee->memberExpr.isOptional || node->callExpr.isOptional;
    AstNode *target = callee->memberExpr.target,
            *call = callee->memberExpr.member;

    if (callee->memberExpr.isRedirect) {
        callee->memberExpr.member = NULL;
        target = callee;
    }

    call->type = callee->type;

    if (isReferenceType(this->type) && isClassType(target->type)) {
        target = makeReferenceOfExpr(
            ctx->pool, &target->loc, this->flags, target, NULL, this->type);
    }
    else if (isPointerType(this->type) && !(typeIs(target->type, Pointer) ||
                                            typeIs(target->type, Reference))) {
        target = makePointerOfExpr(
            ctx->pool, &target->loc, this->flags, target, NULL, this->type);
    }

    target->flags |= flgReference;
    if (isVirtualDispatch(target, call)) {
        node->callExpr.callee = virtualDispatch(ctx, target, call);
        target->next = args;
        node->callExpr.args = target;
    }
    else {
        target->next = args;
        node->callExpr.args = target;
        node->callExpr.callee = call;
    }
    node->callExpr.isOptional = isOptional;
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->ident.resolvesTo;
    if (nodeIs(target, FuncDecl) || nodeIs(target, ExternDecl) ||
        (nodeIs(target, VarDecl) && hasFlag(target, TopLevelDecl))) //
    {
        NodeToExternDecl *f2e = n2eGetNodeToExternDecl(&ctx->n2e, target);
        if (f2e == NULL) {
            // new function declaration added, add extern
            AstNode *decl =
                nodeIs(target, ExternDecl)
                    ? deepCloneAstNode(ctx->pool, target)
                    : makeAstNode(ctx->pool,
                                  &target->loc,
                                  &(AstNode){.tag = astExternDecl,
                                             .type = target->type,
                                             .flags = target->flags,
                                             .externDecl.func = target});

            n2eAddNodeToExternDecl(&ctx->n2e, target, decl);
            astModifierAddHead(&ctx->root, decl);
            node->ident.resolvesTo = decl;
        }
        else {
            node->ident.resolvesTo = f2e->target;
        }
    }
    else if (nodeIs(target, AliasExpr) && target->aliasExpr.expr != NULL) {
        // We need to substitute the alias expression
        replaceAstNode(node,
                       deepCloneAstNode(ctx->pool, target->aliasExpr.expr));
    }
}

static void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    // if (node->binaryExpr.op == opIs) {
    //     astVisitFallbackVisitAll(visitor, node);
    //     const Type *type = stripPointerOrReferenceOnce(lhs->type, NULL);
    //     csAssert0(isUnionType(type));
    //     u32 idx = findUnionTypeIndex(type, rhs->type);
    //     csAssert0(idx != UINT32_MAX);
    //     // Convert to lhs.tag == idx
    //     lhs->memberExpr.target = deepCloneAstNode(ctx->pool, lhs);
    //     lhs->tag = astMemberExpr;
    //     lhs->memberExpr.member =
    //         makeIntegerLiteral(ctx->pool,
    //                            &lhs->loc,
    //                            0,
    //                            NULL,
    //                            getPrimitiveType(ctx->types, prtU64));
    //     lhs->type = getPrimitiveType(ctx->types, prtU64);

    //     rhs->tag = astIntegerLit;
    //     rhs->intLiteral.isNegative = false;
    //     rhs->intLiteral.uValue = idx;
    //     rhs->type = getPrimitiveType(ctx->types, prtU64);

    //     node->binaryExpr.op = opEq;
    // }
    // else
    if (node->binaryExpr.op == opEq || node->binaryExpr.op == opNe) {
        astVisitFallbackVisitAll(visitor, node);
        if (nodeIs(rhs, NullLit) && hasFlag(lhs->type, FuncTypeParam)) {
            // We want to turn this into lhs.0 = null
            AstNode *target = deepCloneAstNode(ctx->pool, lhs);
            lhs->tag = astMemberExpr;
            lhs->flags = flgNone;
            lhs->type = target->type->tuple.members[0];
            clearAstBody(lhs);
            lhs->memberExpr.member =
                makeIntegerLiteral(ctx->pool,
                                   &lhs->loc,
                                   0,
                                   NULL,
                                   getPrimitiveType(ctx->types, prtU64));
            lhs->memberExpr.target = target;
        }
    }
    else if (node->binaryExpr.op == opLOr || node->binaryExpr.op == opLAnd) {
        bool whileCondVisiting = ctx->whileCond.visiting;
        node->binaryExpr.lhs = wrapLogicalOperandInStmtExpr(ctx, lhs);
        node->binaryExpr.rhs = wrapLogicalOperandInStmtExpr(ctx, rhs);

        ctx->whileCond.visiting = false;
        astVisit(visitor, node->binaryExpr.lhs);
        astVisit(visitor, node->binaryExpr.rhs);
        ctx->whileCond.visiting = whileCondVisiting;
    }
    else {
        astVisitFallbackVisitAll(visitor, node);
    }
}

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    // Transform to if, this will avoid computing otherwise unecessarily
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->ternaryExpr.cond,
            *body = node->ternaryExpr.body ?: deepCloneAstNode(ctx->pool, cond),
            *otherwise = node->ternaryExpr.otherwise;
    AstNode *tmp =
        makeVarDecl(ctx->pool,
                    &node->loc,
                    flgMove,
                    makeAnonymousVariable(ctx->strings, "_if"),
                    makeTypeReferenceNode(ctx->pool, node->type, &node->loc),
                    NULL,
                    NULL,
                    node->type);
    astModifierAdd(&ctx->block, tmp);
    body = makeExprStmt(
        ctx->pool,
        &body->loc,
        flgNone,
        makeAssignExpr(
            ctx->pool,
            &body->loc,
            flgNone,
            makeResolvedIdentifier(
                ctx->pool, &body->loc, tmp->_name, 0, tmp, NULL, tmp->type),
            opAssign,
            body,
            NULL,
            body->type),
        NULL,
        body->type);
    otherwise =
        makeExprStmt(ctx->pool,
                     &otherwise->loc,
                     flgNone,
                     makeAssignExpr(ctx->pool,
                                    &otherwise->loc,
                                    flgNone,
                                    makeResolvedIdentifier(ctx->pool,
                                                           &otherwise->loc,
                                                           tmp->_name,
                                                           0,
                                                           tmp,
                                                           NULL,
                                                           tmp->type),
                                    opAssign,
                                    otherwise,
                                    NULL,
                                    otherwise->type),
                     NULL,
                     otherwise->type);
    while (nodeIs(cond, GroupExpr))
        cond = cond->groupExpr.expr;
    AstNode *if_ = makeIfStmt(
        ctx->pool,
        &node->loc,
        flgNone,
        cond,
        makeBlockStmt(
            ctx->pool, &body->loc, body, NULL, makeVoidType(ctx->types)),
        makeBlockStmt(ctx->pool,
                      &otherwise->loc,
                      otherwise,
                      NULL,
                      makeVoidType(ctx->types)),
        NULL);
    if_->type = makeVoidType(ctx->types);
    astVisit(visitor, if_);
    astModifierAdd(&ctx->block, if_);
    // Update current node
    node->tag = astIdentifier;
    clearAstBody(node);
    node->ident.value = tmp->_name;
    node->ident.resolvesTo = tmp;
}

static void visitPathElement(AstVisitor *visitor, AstNode *node)
{
    AstNode copy = *node;
    node->tag = astIdentifier;
    node->ident.value = copy.pathElement.name;
    node->ident.resolvesTo = copy.pathElement.resolvesTo;
    node->ident.super = copy.pathElement.super;
    node->next = NULL;
    astVisit(visitor, node);
}

static void visitPathExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements;
    if (hasFlag(base, AddThis)) {
        AstNode *this = getMemberFunctionThis(ctx->currentFunction);
        csAssert0(this);
        node->path.elements = makeResolvedPathElement(
            ctx->pool, &node->loc, S_this, this->flags, this, base, this->type);

        if (base->pathElement.resolvesTo == NULL) {
            // it already resolves to correct member
            base->pathElement.resolvesTo = findMemberDeclInType(
                stripAll(this->type), base->pathElement.name);
            csAssert0(base->pathElement.resolvesTo);
        }
        node->flags &= ~flgAddThis;
    }

    AstNode *elem = node->path.elements;
    AstNode *resolved = elem->pathElement.resolvesTo;
    if (resolved && (nodeIs(resolved, ImportDecl) || nodeIs(resolved, ModuleDecl)))
        elem = elem->next;

    AstNode *next = elem->next;
    astVisit(visitor, elem);
    if (next == NULL) {
        replaceAstNodeWith(node, elem);
        return;
    }

    AstNode *target = elem;
    for (; next;) {
        elem = next;
        next = next->next;
        astVisit(visitor, elem);
        resolved = elem->ident.resolvesTo;
        if (nodeIs(resolved, VarAlias)) {
            elem->ident.value = resolved->varAlias.var->_name;
            elem->ident.resolvesTo = resolved->varAlias.var;
            if (next == NULL) {
                replaceAstNodeWith(node, elem);
                return;
            }
            target = elem;
            continue;
        }
        if (next) {
            target->next = NULL;
            elem->next = NULL;
            target = makeMemberExpr(ctx->pool,
                                    locExtend(&target->loc, &elem->loc),
                                    elem->flags,
                                    target,
                                    elem,
                                    NULL,
                                    elem->type);
        }
    }

    node->tag = astMemberExpr;
    node->memberExpr.target = target;
    node->memberExpr.member = elem;
}

static void visitStmtExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astVisit(visitor, node->stmtExpr.stmt);
    AstNode *stmt = node->stmtExpr.stmt;
    if (nodeIs(stmt, BlockStmt)) {
        if (stmt->blockStmt.stmts == NULL) {
            astModifierRemoveCurrent(&ctx->block);
        }
        else if (stmt->blockStmt.stmts->next == NULL) {
            AstNode *inner = stmt->blockStmt.stmts;
            if (hasFlag(stmt, BlockReturns) && nodeIs(inner, ExprStmt))
                replaceAstNode(node, inner->exprStmt.expr);
            else
                replaceAstNode(node, inner);
            node->flags &= ~flgBlockValue;
        }
    }
}

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    if (!nodeIs(node, MemberExpr))
        return;

    AstNode *target = node->memberExpr.target;
    if (nodeNeedsTemporaryVar(target) && !nodeIs(target, TypeRef)) {
        AstNode *var = hoistToTemporary(
            ctx, target, target, target->type, "_m", flgNone, true);
        node->memberExpr.target = makeTemporaryRef(ctx, var);
    }
    if (nodeIs(node->memberExpr.member, Path))
        astVisit(visitor, node->memberExpr.member);
}

static void visitReferenceOfExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    astVisitFallbackVisitAll(visitor, node);
    AstNode *operand = node->unaryExpr.operand;
    if (nodeNeedsTemporaryVar(operand)) {
        AstNode *var = hoistToTemporary(
            ctx, operand, operand, operand->type, "_r", flgTemporary, false);
        node->unaryExpr.operand = makeTemporaryRef(ctx, var);
    }
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *field = node->structExpr.fields;
    for (; field; field = field->next) {
        AstNode *target =
            findMemberDeclInType(node->type, field->fieldExpr.name);
        csAssert0(target);
        field->fieldExpr.index = target->structField.index;
        const Type *left = unwrapType(target->type, NULL),
                   *right = unwrapType(field->fieldExpr.value->type, NULL);
        bool nullLit =
            nodeIs(field->fieldExpr.value, NullLit) &&
            (isArrayType(left) || isUnionType(left) || isTupleType(left));
        if (!nullLit && (left != right || typeIs(left, Func))) {
            simplifyCastExpression(ctx, field->fieldExpr.value, left);
        }
        astVisit(visitor, field->fieldExpr.value);
    }
}

static void visitStructDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNodeList fields = {NULL}, others = {NULL};
    u64 i = 0;

    AstNode *member = node->structDecl.members, *next = member;
    for (; next;) {
        member = next;
        next = next->next;
        member->next = NULL;
        if (!hasFlag(member, Static) && nodeIs(member, FieldDecl)) {
            member->structField.index = i++;
            insertAstNode(&fields, member);
            continue;
        }

        if (nodeIs(member, FieldDecl)) {
            csAssert0(hasFlag(member, Static));
            AstNode *type = member->structField.type,
                    *init = member->structField.value;
            cstring name = member->structField.name;
            member->tag = astVarAlias;
            clearAstBody(member);
            member->varAlias.name = name;
            member->varAlias.var = insertAstNode(
                &others,
                makeVarDecl(ctx->pool,
                            &member->loc,
                            member->flags | flgTopLevelDecl,
                            makeAnonymousVariable(ctx->strings, name),
                            type,
                            init,
                            NULL,
                            member->type));
        }
        else if (nodeIs(member, FuncDecl)) {
            insertAstNode(&others, member);
        }
    }
    node->structDecl.members = fields.first;
    if (others.first)
        astModifierAddAsNext(&ctx->root, others.first);
}

static void visitForStmt(AstVisitor *visitor, AstNode *node)
{
    astVisitFallbackVisitAll(visitor, node);
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *parent = node->parentScope;

    AstModifier block = ctx->block;
    astModifierInit(&ctx->block, node);
    AstNode *stmt = node->blockStmt.stmts, *last = node->blockStmt.last;

    for (; stmt; stmt = stmt->next) {
        astModifierNext(&ctx->block, stmt);
        if (hasFlag(stmt, Comptime)) {
            astModifierRemoveCurrent(&ctx->block);
            continue;
        }
        astVisit(visitor, stmt);
        if (nodeIsNoop(stmt)) {
            astModifierRemoveCurrent(&ctx->block);
            continue;
        }

        if (!hasFlag(node, BlockReturns) || stmt->next != NULL) {
            if (isRedundantStatement(stmt)) {
                logWarningWithId(
                    ctx->L,
                    wrnRedundantStmt,
                    &stmt->loc,
                    "removing this statement because it is redundant",
                    NULL);
            }
        }
        last = stmt;
    }

    if (last && hasFlag(node, BlockReturns))
        last->flags |= flgBlockValue;

    if (node->blockStmt.stmts == NULL && nodeIs(parent, BlockStmt)) {
        node->tag = astNoop;
    }
    else {
        node->blockStmt.last = last;
    }
    ctx->block = block;
}

void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ifStmt.cond;
    // if (!isBooleanType(cond->type)) {
    //     simplifyCondition(ctx, cond);
    // }
    astVisitFallbackVisitAll(visitor, node);
}

void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr == NULL)
        return;
    astVisit(visitor, expr);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->whileStmt.cond, *update = node->whileStmt.update;
    if (!isBooleanType(cond->type)) {
        simplifyCondition(ctx, cond);
    }
    __typeof(ctx->whileCond) wc = ctx->whileCond;
    ctx->whileCond = (__typeof(ctx->whileCond)){};
    ctx->whileCond.visiting = true;
    astVisit(visitor, cond);
    if (ctx->whileCond.exprs.first) {
        if (update != NULL) {
            insertAstNode(&ctx->whileCond.exprs, update);
        }
        node->whileStmt.update = makeGroupExpr(
            ctx->pool, &cond->loc, flgNone, ctx->whileCond.exprs.first, NULL);
    }
    ctx->whileCond = wc;

    astVisit(visitor, node->whileStmt.body);
    astVisit(visitor, update);
}

void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->matchStmt.expr, *esac = node->switchStmt.cases;
    astVisit(visitor, expr);
    for (; esac; esac = esac->next) {
        AstNode *body = esac->caseStmt.body;
        if (body && !nodeIs(body, BlockStmt)) {
            esac->caseStmt.body = makeBlockStmt(
                ctx->pool, &body->loc, body, NULL, makeVoidType(ctx->types));
        }
        astVisitFallbackVisitAll(visitor, esac);
    }
}

void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    // SimplifyContext *ctx = getAstVisitorContext(visitor);
    // AstNode *expr = node->matchStmt.expr;
    // astVisit(visitor, expr);
    // AstNode *cond = deepCloneAstNode(ctx->pool, expr);
    // expr->memberExpr.target = deepCloneAstNode(ctx->pool, expr);
    // expr->tag = astMemberExpr;
    // expr->memberExpr.member = makeIntegerLiteral(
    //     ctx->pool, &expr->loc, 0, NULL, getPrimitiveType(ctx->types, prtU64));
    // expr->type = getPrimitiveType(ctx->types, prtU64);

    // node->tag = astSwitchStmt;

    // for (AstNode *esac = node->matchStmt.cases; esac; esac = esac->next) {
    //     AstNode *match = esac->caseStmt.match, *alias = esac->caseStmt.alias,
    //             *body = esac->caseStmt.body;
    //     if (match) {
    //         esac->caseStmt.match = makeIntegerLiteral(
    //             ctx->pool, &match->loc, esac->caseStmt.idx, NULL, expr->type);
    //         if (alias) {
    //             alias->aliasExpr.expr = makeCastExpr(
    //                 ctx->pool,
    //                 &alias->loc,
    //                 flgUnionCast,
    //                 deepCloneAstNode(ctx->pool, cond),
    //                 makeTypeReferenceNode(ctx->pool, alias->type, &alias->loc),
    //                 NULL,
    //                 alias->type);
    //             alias->aliasExpr.expr->castExpr.idx = esac->caseStmt.idx;
    //             if (!nodeIs(body, BlockStmt)) {
    //                 esac->caseStmt.body =
    //                     makeBlockStmt(ctx->pool,
    //                                   &body->loc,
    //                                   esac->caseStmt.body,
    //                                   NULL,
    //                                   makeVoidType(ctx->types));
    //             }
    //         }
    //     }
    //     else if (!nodeIs(body, BlockStmt)) {
    //         esac->caseStmt.body = makeBlockStmt(
    //             ctx->pool, &body->loc, body, NULL, makeVoidType(ctx->types));
    //     }
    //     astVisit(visitor, esac);
    // }
    astVisitFallbackVisitAll(visitor, node);
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    ctx->currentFunction = node;
    n2eAddNodeToExternDecl(&ctx->n2e, node, node);
    AstNode *param = node->funcDecl.signature->params;
    if (node->funcDecl.this_) {
        node->funcDecl.signature->params = node->funcDecl.this_;
    }

    for (; param; param = param->next) {
        if (isVoidType(param->type))
            param->next = NULL;
    }

    AstNode *body = node->funcDecl.body;
    if (nodeIs(body, ExprStmt)) {
        AstNode *ret = makeReturnAstNode(ctx->pool,
                                         &body->loc,
                                         flgNone,
                                         body->exprStmt.expr,
                                         NULL,
                                         body->type);
        body->tag = astBlockStmt;
        body->blockStmt.stmts = ret;
    }
    astVisitFallbackVisitAll(visitor, node);
    ctx->currentFunction = NULL;
}

static void visitVarDecl(AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    n2eAddNodeToExternDecl(&ctx->n2e, node, node);
    AstNode *init = node->varDecl.init;
    if (!hasFlag(node, TopLevelDecl)) {
        astVisit(visitor, init);
        return;
    }

    recordGlobalFini(ctx, node);

    if (init == NULL)
        return;

    if (!isStaticExpr(init)) {
        if (typeIs(init->type, Class)) {
            node->varDecl.init =
                makeNullLiteral(ctx->pool, &init->loc, NULL, init->type);
        }
        else {
            node->varDecl.init = NULL;
        }
        if (isArrayType(init->type)) {
            // const _ga = {}
            // bfi(Copy ga, _ga)
            AstNode *ga =
                makeVarDecl(ctx->pool,
                            &init->loc,
                            flgTemporary | flgConst,
                            makeAnonymousVariable(ctx->strings, "_ga"),
                            NULL,
                            init,
                            NULL,
                            init->type);
            insertAstNode(&ctx->init, ga);
            AstNode *args =
                makeResolvedIdentifier(ctx->pool,
                                       &init->loc,
                                       node->varDecl.name,
                                       0,
                                       node,
                                       makeResolvedIdentifier(ctx->pool,
                                                              &init->loc,
                                                              ga->varDecl.name,
                                                              0,
                                                              ga,
                                                              NULL,
                                                              ga->type),
                                       node->type);
            insertAstNode(
                &ctx->init,
                makeExprStmt(ctx->pool,
                             &init->loc,
                             flgNone,
                             makeBackendCallExpr(ctx->pool,
                                                 &init->loc,
                                                 flgNone,
                                                 bfiCopy,
                                                 args,
                                                 makeVoidType(ctx->types)),
                             NULL,
                             makeVoidType(ctx->types)));
        }
        else {
            // just a list of assignment expressions
            insertAstNode(&ctx->init,
                          makeExprStmt(ctx->pool,
                                       &node->loc,
                                       flgNone,
                                       makeAssignExpr(ctx->pool,
                                                      &node->loc,
                                                      node->flags,
                                                      makeResolvedIdentifier(
                                                          ctx->pool,
                                                          &node->loc,
                                                          node->varDecl.name,
                                                          0,
                                                          node,
                                                          NULL,
                                                          node->type),
                                                      opAssign,
                                                      init,
                                                      NULL,
                                                      init->type),
                                       NULL,
                                       makeVoidType(ctx->types)));
        }
    }
    else {
        astVisit(visitor, init);
    }
}

#ifdef LLVM_BACKEND
static void simplifyMainModule(SimplifyContext *ctx, AstNode *program)
{
    const Type *moduleCtorType = makeTupleType(
        ctx->types,
        (const Type *[]){
            getPrimitiveType(ctx->types, prtI32),
            makeFuncType(ctx->types,
                         &(Type){.tag = typFunc,
                                 .name = S___init,
                                 .func = {.retType = makeVoidType(ctx->types),
                                          .decl = NULL}}),
            makeVoidPointerType(ctx->types, flgNone)},
        3,
        flgNone);

    AstNodeList elems = {};
    AstNode *init = ctx->startup->first;
    i32 i = 0;
    for (; init; init = init->next, i++) {
        // we want to add that function here
        AstNode *members = makeIntegerLiteral(
            ctx->pool,
            &init->loc,
            65535 - i,
            makeResolvedIdentifier(
                ctx->pool,
                &init->loc,
                init->type->name,
                0,
                init,
                makeNullLiteral(ctx->pool,
                                &init->loc,
                                NULL,
                                moduleCtorType->tuple.members[2]),
                init->type),
            moduleCtorType->tuple.members[0]);
        insertAstNode(&elems,
                      makeTupleExpr(ctx->pool,
                                    &members->loc,
                                    flgNone,
                                    members,
                                    NULL,
                                    moduleCtorType));
    }

    getLastAstNode(ctx->startup->first)->next = program->program.decls;
    program->program.decls = ctx->startup->first;
    const Type *type = makeArrayType(ctx->types, moduleCtorType, i);
    ctx->startup->first = NULL;

    AstNode *ctors = makeVarDecl(
        ctx->pool,
        builtinLoc(),
        flgTopLevelDecl | flgConst | flgDiDisable,
        makeString(ctx->strings, S___LLVM_global_ctors),
        NULL,
        makeArrayExpr(
            ctx->pool, builtinLoc(), flgConst, elems.first, NULL, type),
        NULL,
        type);
    ctors->attrs = makeAttribute(
        ctx->pool,
        builtinLoc(),
        S_linkage,
        makeStringLiteral(ctx->pool,
                          builtinLoc(),
                          S_Appending,
                          NULL,
                          makeStringType(ctx->types)),
        makeAttribute(ctx->pool,
                      builtinLoc(),
                      S_section,
                      makeStringLiteral(ctx->pool,
                                        builtinLoc(),
                                        S_ctor_section,
                                        NULL,
                                        makeStringType(ctx->types)),
                      NULL));
    getLastAstNode(program->program.decls)->next = ctors;
}
#endif

static void withSavedStack(Visitor func, AstVisitor *visitor, AstNode *node)
{
    SimplifyContext *ctx = getAstVisitorContext(visitor);
    __typeof(ctx->stack) stack = ctx->stack;
    if (node->type && node->type->retyped) {
        node->type = node->type->retyped;
    }
    node->type = resolveAndUnThisType(node->type);
    func(visitor, node);

    ctx->stack = stack;
}

static void resolveTypesImpl(Visitor func, AstVisitor *visitor, AstNode *node)
{
    if (node->type && node->type->retyped) {
        node->type = node->type->retyped;
    }
    func(visitor, node);
}

static void resolveTypes(CompilerDriver *driver, AstNode *node)
{
    AstVisitor visitor = makeAstVisitor(NULL,
                                        {},
                                        .fallback = astVisitFallbackVisitAll,
                                        .dispatch = resolveTypesImpl);
    astVisit(&visitor, node);
}

static AstNode *simplifyCode(CompilerDriver *driver, AstNode *node)
{
    // resolveTypes(driver, node);

    SimplifyContext context = {.L = driver->L,
                               .types = driver->types,
                               .strings = driver->strings,
                               .pool = driver->pool,
                               .startup = &driver->startup,
                               .traceMemory = driver->options.withMemoryTrace &&
                                              isBuiltinsInitialized()};
    n2eInit(&context.n2e, driver->pool);

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astPath] = visitPathExpr,
        [astPathElem] = visitPathElement,
        [astIdentifier] = visitIdentifier,
        [astBinaryExpr] = visitBinaryExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astIndexExpr] = visitIndexExpr,
        [astCallExpr] = visitCallExpr,
        [astCastExpr] = visitCastExpr,
        [astStmtExpr] = visitStmtExpr,
        [astStructExpr] = visitStructExpr,
        [astMemberExpr] = visitMemberExpr,
        [astReferenceOf] = visitReferenceOfExpr,
        [astPointerOf] = visitReferenceOfExpr,
        [astForStmt] = visitForStmt,
        [astBlockStmt] = visitBlockStmt,
        [astWhileStmt] = visitWhileStmt,
        [astIfStmt] = visitIfStmt,
        [astReturnStmt] = visitReturnStmt,
        [astMatchStmt] = visitMatchStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astStructDecl] = visitStructDecl,
        [astClassDecl] = visitStructDecl,
        [astFuncDecl] = visitFuncDecl,
        [astVarDecl] = visitVarDecl,
        [astGenericDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll, .dispatch = withSavedStack);
    // clang-format on

    astVisit(&visitor, node);
    n2eDeinit(&context.n2e);

#ifdef LLVM_BACKEND
    if (hasFlag(node, Main) && driver->startup.first) {
        simplifyMainModule(&context, node);
    }
#endif
    return node;
}

AstNode *simplifyAst(CompilerDriver *driver, AstNode *node)
{
    node = simplifyDeferStatements(driver, node);
    if (node == NULL || hasErrors(driver->L))
        return node;

    return simplifyCode(driver, node);
}
