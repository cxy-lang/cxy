//
// Created by Carter on 2023-08-27.
//
#include "check.h"

#include "lang/frontend/capture.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

#include "core/alloc.h"
#include "lang/middle/builtins.h"

static inline bool isInlineFunction(const AstNode *node)
{
    return findAttribute(node, S_inline) != NULL;
}

static inline bool isIteratorFunction(const Type *type)
{
    type = stripReference(type);
    if (type == NULL)
        return false;
    return hasFlag(type, Iterator);
}

static bool validateMainFunction(TypingContext *ctx,
                                 AstNode *node,
                                 const Type *type)
{
    if (type->func.paramsCount == 0)
        return true;

    if (type->func.paramsCount > 1) {
        logError(ctx->L,
                 &node->loc,
                 "main function can only have on argument, a slice of strings",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return false;
    }

    const Type *param = type->func.params[0];
    if (!isSliceType(param)) {
        logError(ctx->L,
                 &node->loc,
                 "main function parameter must be a slice of strings",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return false;
    }
    const Type *elem = getSliceTargetType(param);
    if (!typeIs(elem, String)) {
        logError(ctx->L,
                 &node->loc,
                 "main function parameter must be a slice of strings",
                 NULL);
        node->type = ERROR_TYPE(ctx);
        return false;
    }
    return true;
}

const Type *matchOverloadedFunctionPerfectMatch(Log *L,
                                                const Type *callee,
                                                const Type **argTypes,
                                                u64 argsCount,
                                                const FileLoc *loc,
                                                u64 flags,
                                                bool perfectMatch)
{
    if (typeIs(callee, Closure)) {
        const Type *fn = callee->closure.fn;
        u64 offset = (fn->func.paramsCount > 0 && fn->func.decl && fn->func.decl->funcDecl.this_) ? 1 : 0;
        u64 paramsCount = fn->func.paramsCount - offset;
        if (argsCount != paramsCount) {
            logError(L, loc, "calling closure expecting {u64} arguments, got {u64}", (FormatArg[]){{.u64 = paramsCount}, {.u64 = argsCount}});
            return NULL;
        }
        for (u64 i = 0; i < argsCount; i++) {
            const Type *paramType = resolveAndUnThisType(fn->func.params[i + offset]);
            const Type *argType = resolveAndUnThisType(argTypes[i]);
            if (!isTypeAssignableFrom(paramType, argType)) {
                logError(L, loc, "closure argument {u64} type '{t}' is incompatible with parameter type '{t}'", (FormatArg[]){{.u64 = i + 1}, {.t = argType}, {.t = paramType}});
                return NULL;
            }
        }
        return callee;
    }

    AstNode *decls = callee->func.decl->list.first ?: callee->func.decl,
            *decl = decls;
    const Type *found = NULL;
    u64 maxScore = argsCount * 4, matchScore = 0, declarations = 0;

    for (; decl; decl = decl->list.link) {
        const Type *type = decl->type;
        if (type == NULL)
            continue;

        if (isMemberFunction(decl) && ((decl->flags & flags) != flags))
            continue;

        bool isVariadic = hasFlag(decl, Variadic);
        u64 defaultCount = type->func.defaultValuesCount,
            paramsCount = type->func.paramsCount,
            requiredCount = paramsCount - defaultCount - isVariadic;

        declarations++;

        argsCount =
            isVariadic ? MIN(argsCount, paramsCount - isVariadic) : argsCount;
        if (argsCount < requiredCount || argsCount > paramsCount)
            continue;

        u64 score = maxScore;
        bool compatible = true;
        for (u64 i = 0; i < argsCount; i++) {
            const Type *paramType = resolveAndUnThisType(type->func.params[i]),
                       *argType = resolveAndUnThisType(argTypes[i]);
            compatible = paramType == argType;
            if (compatible)
                continue;
            if (!perfectMatch) {
                compatible = isTypeAssignableFrom(paramType, argType);
                if (compatible) {
                    score--;
                    continue;
                }
                if (isReferenceType(paramType) && !isReferenceType(argType)) {
                    compatible = isTypeAssignableFrom(stripReference(paramType),
                                                      argType);
                    if (compatible) {
                        score--;
                        continue;
                    }
                }
                compatible = isExplicitConstructableFrom(L, paramType, argType);
                if (compatible) {
                    score -= 2;
                    continue;
                }
                break;
            }
            else
                break;
        }

        if (!compatible)
            continue;

        if (score == maxScore)
            return type;

        if (score >= matchScore) {
            matchScore = score;
            found = type;
        }
    }

    if (found)
        return found;

    if (L && loc) {
        Type tAuto = {.tag = typAuto};
        Type type = {.tag = typFunc,
                     .flags = flags,
                     .name = callee->func.decl->funcDecl.name,
                     .func = {.params = argTypes,
                              .paramsCount = argsCount,
                              .retType = &tAuto}};

        logError(L,
                 loc,
                 "incompatible function reference `{s}`({u64} functions "
                 "declared did not match function with signature {t})",
                 (FormatArg[]){
                     {.s = type.name}, {.u64 = declarations}, {.t = &type}});

        decl = decls;
        while (decl) {
            if (decl->type)
                logError(L,
                         &decl->loc,
                         "found declaration with incompatible signature {t}",
                         (FormatArg[]){{.t = decl->type}});
            else
                logError(L,
                         &decl->loc,
                         "found declaration with incompatible signature "
                         "`unresolved`",
                         NULL);
            decl = decl->list.link;
        }
    }
    return NULL;
}

bool checkMemberFunctions(AstVisitor *visitor,
                          AstNode *node,
                          NamedTypeMember *members)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *member = node->structDecl.members;

    bool retype = false;
    for (u64 i = hasFlag(node, Virtual); member; member = member->next, i++) {
        if (nodeIs(member, FuncDecl)) {
            if (member->funcDecl.this_) {
                member->funcDecl.this_->type =
                    nodeIs(node, ClassDecl)
                        ? makeReferenceType(
                              ctx->types, node->type, member->flags & flgConst)
                        : makePointerType(
                              ctx->types, node->type, member->flags & flgConst);
            }

            const Type *type = member->type;
            if (!hasFlag(member, Abstract)) {
                type = checkFunctionBody(visitor, member);
                if (typeIs(type, Error)) {
                    node->type = ERROR_TYPE(ctx);
                    return false;
                }
            }

            if (member->funcDecl.operatorOverload == opRange && !isIteratorFunction(type->func.retType))
            {
                logError(ctx->L,
                         &member->loc,
                         "expecting a range operator '..' overload to return an "
                         "iterator type, got '{t}'",
                         (FormatArg[]){{.t = type->func.retType}});
                node->type = ERROR_TYPE(ctx);
                return false;
            }

            retype = members[i].type != type;
            if (members[i].name == member->funcDecl.name)
                members[i].type = type;
        }
    }

    return retype;
}

void checkFunctionParam(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *type = node->funcParam.type, *def = node->funcParam.def;
    AstNode *parent = node->parentScope;

    const Type *type_ = checkType(visitor, type), *def_ = NULL;
    if (typeIs(type_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    node->flags |= type ? (type->flags & flgConst) : flgNone;
    if (hasFlag(type, Const) && !hasFlag(type_, Const))
        type_ = makeWrappedType(ctx->types, type_, flgConst);

    if (typeIs(type_, Func) && !hasFlags(parent, flgPure | flgExtern)) {
        bool isEscaping = findAttribute(node, S_escaping) != NULL;
        node->type = makeClosureType(ctx->types, makeVoidType(ctx->types), type_, isEscaping);
        return;
    }

    if (def == NULL) {
        node->type = type_;
        return;
    }

    def->parentScope = node;
    def_ = checkType(visitor, def);
    if (typeIs(def_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (!nodeIs(def, NullLit) && !isTypeAssignableFrom(type_, def_)) {
        logError(ctx->L,
                 &def->loc,
                 "parameter default value type '{t}' is incompatible with "
                 "parameter '{t}'",
                 (FormatArg[]){{.t = def_}, {.t = type_}});
        node->type = ERROR_TYPE(ctx);
    }
    else
        node->type = type_;
}

static bool validateMemTrace(TypingContext *ctx, const AstNode *node);

const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    // Members get their signature checked from class and struct checks, so the
    // attribute is validated here, once
    if (node->type == NULL && !validateMemTrace(ctx, node))
        return node->type = ERROR_TYPE(ctx);
    AstNode *params = node->funcDecl.signature->params, *param = params,
            *ret = node->funcDecl.signature->ret, *body = node->funcDecl.body;
    u64 paramsCount = countAstNodes(params);
    node->flags |=
        findAttribute(node, S_transient) == NULL ? flgNone : flgReference;

    const Type **params_ = mallocOrDie(sizeof(Type *) * paramsCount);
    const Type *ret_ = ret ? checkType(visitor, ret) : makeAutoType(ctx->types);
    if (typeIs(ret_, Func) && !hasFlags(node, flgPure | flgExtern)) {
        ret_ = makeClosureType(ctx->types, makeVoidType(ctx->types), ret_, true);
        if (ret)
            ret->type = ret_;
    }

    const Type *type = ret_;
    u64 defaultValues = 0;
    for (u64 i = 0; param; param = param->next, i++) {
        params_[i] = param->type ?: checkType(visitor, param);
        if (typeIs(params_[i], Error))
            type = params_[i];

        defaultValues += (param->funcParam.def != NULL);
    }

    if (typeIs(type, Error)) {
        free(params_);
        return node->type = ERROR_TYPE(ctx);
    }

    if (node->list.first && node->list.first != node) {
        type = node->list.first->type;
        if (typeIs(type, Error)) {
            return node->type = type;
        }

        const Type *found =
            matchOverloadedFunctionPerfectMatch(ctx->L,
                                                type,
                                                params_,
                                                paramsCount,
                                                NULL,
                                                node->flags & flgConst,
                                                true);
        if (found && found->func.decl != node) {
            // conflict
            logError(
                ctx->L,
                &node->loc,
                "function '{s}' overload with signature {t} already declared",
                (FormatArg[]){{.s = node->funcDecl.name}, {.t = found}});
            logNote(ctx->L,
                    &found->func.decl->loc,
                    "previous declaration found here",
                    NULL);
            free(params_);

            return node->type = ERROR_TYPE(ctx);
        }
    }

    node->type =
        makeFuncType(ctx->types,
                     &(Type){.tag = typFunc,
                             .flags = node->flags & (flgConst | flgVariadic),
                             .name = getDeclarationName(node),
                             .func = {.paramsCount = paramsCount,
                                      .params = params_,
                                      .retType = ret_,
                                      .defaultValuesCount = defaultValues,
                                      .decl = node}});
    free(params_);
    return node->type;
}

const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *body = node->funcDecl.body, *ret = node->funcDecl.signature->ret;

    const Type *type = node->type;
    const Type *ret_ = type->func.retType;
    if (typeIs(ret_, Auto) && ret) {
        ret_ = checkType(visitor, ret);
    }

    bool currentReturnState = ctx->returnState,
         currentExplicitCatch = ctx->explicitCatch;
    __typeof(ctx->catcher) currentCatcher = ctx->catcher;
    AstNode *currentTaskVar = ctx->currentTaskVar;
    ctx->currentTaskVar = NULL;
    ctx->returnState = false;
    ctx->explicitCatch = false;
    ctx->currentFunction = node;
    ctx->currentEnclosure = node;
    ctx->catcher = (__typeof(ctx->catcher)){};
    ctx->fun = getDeclarationName(node);

    const Type *body_ = checkType(visitor, body);
    ctx->returnState = currentReturnState;
    ctx->explicitCatch = currentExplicitCatch;
    ctx->catcher = currentCatcher;
    ctx->currentTaskVar = currentTaskVar;

    if (typeIs(body_, Error))
        return node->type = ERROR_TYPE(ctx);

    if (nodeIs(body, BlockStmt)) {
        AstNode *ret = node->funcDecl.signature->ret;
        // Can be updated by return statement
        body_ = ret ? ret->type : body_;
    }

    if (typeIs(body_, Auto))
        body_ = makeVoidType(ctx->types);

    if (ret_ == body_)
        return type;

    if (!isTypeAssignableFrom(ret_, body_)) {
        logError(ctx->L,
                 &node->loc,
                 "return type '{t}' of function declaration doesn't match type "
                 "'{t}' returned in body.",
                 (FormatArg[]){{.t = ret_}, {.t = body_}});
        return node->type = ERROR_TYPE(ctx);
    }

    if (typeIs(ret_, Auto))
        node->type = changeFunctionRetType(ctx->types, type, body_);

    return node->type;
}

void checkFunctionType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type *ret = checkType(visitor, node->funcType.ret);
    u64 count = countAstNodes(node->funcType.params);
    const Type **params = mallocOrDie(sizeof(Type *) * count);

    AstNode *param = node->funcType.params;
    u64 defaultValuesCount = 0;
    for (u64 i = 0; param; param = param->next, i++) {
        param->parentScope = node;
        params[i] = checkType(visitor, param);
        if (params[i] == ERROR_TYPE(ctx)) {
            node->type = ERROR_TYPE(ctx);
        }
        else if (param->funcParam.def) {
            defaultValuesCount++;
        }
    }

    if (node->type == NULL) {
        node->type = makeFuncType(
            ctx->types,
            &(Type){.tag = typFunc,
                    .name = NULL,
                    .flags = node->flags,
                    .func = {.retType = ret,
                             .params = params,
                             .paramsCount = count,
                             .defaultValuesCount = defaultValuesCount,
                             .decl = node}});
    }

    free(params);
}

// Overloads called by generated glue or through tables, which cannot pass a
// hidden site. `init` and the other operators are always called by name.
static bool isGlueOperator(Operator op)
{
    switch (op) {
    case opDeinitOverload:
    case opCopyOverload:
    case opConstructorOverload:
    case opDestructorOverload:
    case opDestructorFwd:
    case opInitDefaults:
    case opHashOverload:
    case opStringOverload:
        return true;
    default: return false;
    }
}

// `@memtrace(callsite)` adds a hidden site parameter in trace builds. The
// restrictions apply in every build so both builds accept the same programs.
static bool validateMemTrace(TypingContext *ctx, const AstNode *node)
{
    const AstNode *attr = findAttribute(node, S_memtrace);
    if (attr == NULL)
        return true;

    if (findAttributeArgument(attr, S_callsite) == NULL) {
        logError(ctx->L,
                 &attr->loc,
                 "expecting `callsite` as the argument of `@memtrace`, "
                 "e.g. `@memtrace(callsite)`",
                 NULL);
        return false;
    }

    cstring why = NULL;
    if (hasFlags(node, flgVirtual | flgAbstract) || findAttribute(node, S_override))
        why = "a virtual or overriding method is called through a table that "
              "cannot pass the site";
    else if (hasFlag(node, Extern))
        why = "an extern function has a fixed C signature";
    else if (isGlueOperator(node->funcDecl.operatorOverload))
        why = "`deinit`, `copy`, `hash`, `str` and the other glue overloads are "
              "called through generated code that cannot pass the site";
    else if (findAttribute(node, S_task))
        why = "a `@task` function is started by `@async`, which cannot pass "
              "the site";
    if (why == NULL)
        return true;

    logError(ctx->L,
             &attr->loc,
             "`@memtrace(callsite)` is not allowed here: {s}",
             (FormatArg[]){{.s = why}});
    return false;
}

void checkFunctionDecl(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    if (node->funcDecl.name == S_main) {
        node->flags |= flgMain;
        ctx->root.parent->flags |= flgExecutable;
    }

    const Type *type = checkFunctionSignature(visitor, node);
    if (typeIs(type, Error))
        return;

    if (hasFlag(node, Main)) {
        validateMainFunction(ctx, node, type);
        if (typeIs(node->type, Error))
            return;
    }

    if (node->funcDecl.body) {
        checkFunctionBody(visitor, node);
    }
}
