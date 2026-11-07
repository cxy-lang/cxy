//
// Created by Carter Mbotho on 2024-03-23.
//

#include "check.h"

#include "core/alloc.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"

AstNode *vTableMembersFind(TypingContext *ctx,
                           AstNode *members,
                           const AstNode *member)
{
    AstNode *it = members;
    while (it) {
        if (member->_namedNode.name == it->_namedNode.name) {
            return it;
        }
        it = it->next;
    }
    return NULL;
}

static AstNode *makeVTableVariable(TypingContext *ctx,
                                   AstNode *node,
                                   AstNode *vTableType,
                                   AstNode *init)
{
    AstNode *vTable = makeVarDecl(
        ctx->pool,
        &node->loc,
        flgConst | flgTopLevelDecl | (node->flags & flgPublic),
        makeStringConcat(ctx->strings, getDeclarationName(node), "_vTable"),
        NULL,
        makeStructExpr(ctx->pool,
                       &node->loc,
                       flgConst,
                       makeResolvedIdentifier(ctx->pool,
                                              &node->loc,
                                              vTableType->_namedNode.name,
                                              0,
                                              vTableType,
                                              NULL,
                                              vTableType->type),
                       init,
                       NULL,
                       vTableType->type),
        NULL,
        vTableType->type);
    astModifierAdd(&ctx->root, vTable);
    return vTable;
}

static AstNode *makeVTableMemberInit(TypingContext *ctx,
                                     AstNode *node,
                                     AstNode *vTable)
{
    const Type *type_ = makePointerType(ctx->types, vTable->type, flgNone);
    return makePointerOfExpr(ctx->pool,
                             &node->loc,
                             flgConst,
                             makeResolvedIdentifier(ctx->pool,
                                                    &node->loc,
                                                    vTable->_namedNode.name,
                                                    0,
                                                    vTable,
                                                    NULL,
                                                    vTable->type),
                             NULL,
                             type_);
}

static AstNode *makeVTableMember(TypingContext *ctx,
                                 AstNode *node,
                                 AstNode *vTable)
{
    const Type *type_ = makePointerType(ctx->types, vTable->type, flgNone);
    return makeStructField(ctx->pool,
                           &node->loc,
                           S_vtable,
                           flgConst | flgVTable,
                           makeTypeReferenceNode(ctx->pool, type_, &node->loc),
                           makeVTableMemberInit(ctx, node, vTable),
                           NULL);
}

static inline AstNode *getVTableMember(AstNode *node)
{
    csAssert0(hasFlag(node, Virtual));
    return node->classDecl.members;
}

static AstNode *getVTableDefault(AstNode *node)
{
    AstNode *member = getVTableMember(node);
    csAssert0(nodeIs(member, FieldDecl));
    AstNode *value = member->structField.value;
    csAssert0(nodeIs(value, PointerOf));
    value = value->unaryExpr.operand;
    csAssert0(nodeIs(value, Identifier));
    value = value->ident.resolvesTo;
    csAssert0(nodeIs(value, VarDecl));
    return value->varDecl.init;
}

static AstNode *getVTableType(AstNode *node)
{
    AstNode *def = getVTableDefault(node);
    csAssert0(nodeIs(def, StructExpr));
    AstNode *type = def->structExpr.left;
    csAssert0(nodeIs(type, Identifier));
    return type->ident.resolvesTo;
}

static const Type *makeVTableMethodType(TypingContext *ctx, const Type *classType, const Type *funcType, Flags flags)
{
    if (!typeIs(funcType, Func))
        return funcType;

    const Type *thisType = isClassType(classType)
                               ? makeReferenceType(ctx->types, classType, flags & flgConst)
                               : makePointerType(ctx->types, classType, flags & flgConst);

    u64 count = funcType->func.paramsCount + 1;
    const Type **params = mallocOrDie(sizeof(const Type *) * count);
    params[0] = thisType;
    for (u64 i = 0; i < funcType->func.paramsCount; i++) {
        params[i + 1] = funcType->func.params[i];
    }

    Type init = *funcType;
    init.func.params = params;
    init.func.paramsCount = (u16)count;

    const Type *res = makeFuncType(ctx->types, &init);
    free(params);
    return res;
}

static AstNode *makeMemberFunctionRef(TypingContext *ctx,
                                      AstNode *node,
                                      AstNode *member)
{
    return makePathWithElements(
        ctx->pool,
        &member->loc,
        flgConst,
        makeResolvedPathElement(
            ctx->pool,
            &member->loc,
            getDeclarationName(node),
            flgConst,
            node,
            makeResolvedPathElement(ctx->pool,
                                    &member->loc,
                                    getDeclarationName(member),
                                    flgConst,
                                    member,
                                    NULL,
                                    member->type),
            member->type),
        NULL);
}

static void inheritanceChainBuildVTableType(TypingContext *ctx,
                                            AstNodeList *members,
                                            AstNodeList *init,
                                            AstNode *node)
{
    AstNode *base = node->classDecl.base;
    if (base != NULL) {
        inheritanceChainBuildVTableType(ctx, members, init, resolvePath(base));
        AstNode *field = init->first;
        for (; field; field = field->next) {
            AstNode *member =
                findMemberDeclInType(node->type, field->_namedNode.name);
            if (nodeIs(member, FuncDecl)) {
                field->fieldExpr.value =
                    makeMemberFunctionRef(ctx, node, member);
            }
        }
    }

    if (!hasFlag(node, Virtual))
        return;

    AstNode *member = node->classDecl.members;
    for (; member; member = member->next) {
        if (!hasFlag(member, Virtual))
            continue;
        AstNode *field = vTableMembersFind(ctx, members->first, member);
        if (field)
            continue;
        const Type *methodType = makeVTableMethodType(ctx, node->type, member->type, member->flags);
        insertAstNode(
            members,
            makeStructField(
                ctx->pool,
                &member->loc,
                member->_namedNode.name,
                flgConst,
                makeTypeReferenceNode(ctx->pool, methodType, &member->loc),
                NULL,
                NULL));

        insertAstNode(
            init,
            makeFieldExpr(
                ctx->pool,
                &member->loc,
                member->_namedNode.name,
                flgConst,
                member->funcDecl.body
                    ? makeMemberFunctionRef(ctx, node, member)
                    : makeNullLiteral(
                          ctx->pool, &member->loc, NULL, methodType),
                member,
                NULL));
    }

    AstNode *vTableMember = getVTableMember(node);
    if (hasFlag(vTableMember, VTable)) {
        AstNode *decl = getTypeDecl(stripPointer(vTableMember->type));
        if (decl != NULL) {
            member = decl->structDecl.members->next;
            AstNode *expr =
                vTableMember->structField.value->unaryExpr.operand->ident
                    .resolvesTo->varDecl.init->structExpr.fields->next;
            for (; member; member = member->next) {
                AstNode *field = vTableMembersFind(ctx, members->first, member);
                if (field != NULL)
                    continue;
                insertAstNode(members, deepCloneAstNode(ctx->pool, member));
                insertAstNode(init, deepCloneAstNode(ctx->pool, expr));
                expr = expr->next;
            }
        }
    }
}

AstNode *inheritanceBuildVTableType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNodeList members = {NULL}, init = {NULL};
    csAssert0(hasFlag(node, Virtual));
    // Add id
    AstNode *tid = insertAstNode(
        &members,
        makeStructField(
            ctx->pool,
            &node->loc,
            S___tid,
            flgNone,
            makeTypeReferenceNode(
                ctx->pool, getPrimitiveType(ctx->types, prtU64), &node->loc),
            NULL,
            NULL));
    insertAstNode(
        &init,
        makeFieldExpr(ctx->pool,
                      &node->loc,
                      S___tid,
                      flgNone,
                      makeUnsignedIntegerLiteral(ctx->pool,
                                                 &node->loc,
                                                 unThisType(node->type)->index,
                                                 NULL,
                                                 tid->type),
                      tid,
                      NULL));

    inheritanceChainBuildVTableType(ctx, &members, &init, node);
    csAssert0(members.first != NULL);

    AstNode *vTableType = makeStructDecl(
        ctx->pool,
        &node->loc,
        flgNone,
        makeStringConcat(ctx->strings, getDeclarationName(node), "_VTable"),
        members.first,
        NULL,
        NULL);

    const Type *type = checkType(visitor, vTableType);
    if (typeIs(type, Error)) {
        node->type = ERROR_TYPE(ctx);
        return NULL;
    }
    astModifierAdd(&ctx->root, vTableType);

    AstNode *vTable = makeVTableVariable(ctx, node, vTableType, init.first);
    AstNode *member = makeVTableMember(ctx, node, vTable);
    member->next = node->classDecl.members;
    node->classDecl.members = member;
    return member;
}

// The nearest class above `node` that declares a virtual method and so owns a vtable
// type, skipping classes that only override. NULL when there is none.
AstNode *inheritanceVirtualBase(AstNode *node)
{
    AstNode *base =
        node->classDecl.base ? resolvePath(node->classDecl.base) : NULL;
    while (base && !hasFlag(base, Virtual))
        base = base->classDecl.base ? resolvePath(base->classDecl.base) : NULL;
    return base;
}

// The method of `name` that fills the vtable slot for `node`: the one nearest
// `virtualBase`. A class that overrides without `virtual` closes the slot, so a
// method of the same name further down is not an override.
static AstNode *findVTableOverride(AstNode *node,
                                   const AstNode *virtualBase,
                                   cstring name)
{
    AstNode *found = NULL;
    for (AstNode *it = node; it && it != virtualBase;
         it = it->classDecl.base ? resolvePath(it->classDecl.base) : NULL) {
        AstNode *decl = findMemberDeclInType(it->type, name);
        if (decl && nodeIs(decl, FuncDecl))
            found = decl;
    }
    return found;
}

AstNode *inheritanceMakeVTableMember(TypingContext *ctx,
                                     AstNode *node,
                                     AstNode *init)
{
    AstNode *type = getVTableType(inheritanceVirtualBase(node));
    const Type *type_ = makePointerType(ctx->types, type->type, flgNone);
    AstNode *member =
        makeStructField(ctx->pool,
                        &node->loc,
                        S_vtable,
                        flgConst | flgVTable,
                        makeTypeReferenceNode(ctx->pool, type_, &node->loc),
                        init,
                        NULL);
    member->next = node->classDecl.members;
    node->classDecl.members = member;
    return member;
}

static const Type *baseClassOf(const Type *type)
{
    const TypeInheritance *inheritance = getTypeInheritance(unThisType(type));
    return inheritance && inheritance->base ? unThisType(inheritance->base)
                                            : NULL;
}

// Whether `cls`, or a class between it and `declaring`, has a body for `method`. A
// method is matched by name, the way the vtable slot of an override is.
static bool isAbstractMethodImplemented(const Type *cls,
                                        const Type *declaring,
                                        const AstNode *method)
{
    for (; cls && cls != declaring; cls = baseClassOf(cls)) {
        const NamedTypeMember *found =
            findClassMember(cls, method->funcDecl.name);
        if (found && found->decl && nodeIs(found->decl, FuncDecl) &&
            !hasFlag(found->decl, Abstract))
            return true;
    }
    return false;
}

#define MAX_INHERITANCE_DEPTH 64

// The first abstract method that `cls` has no body for, looking from the topmost base
// down to `cls` itself, or NULL. A method counts as implemented when `cls` or a class
// between it and the one that declares the method has a body for it.
const AstNode *findUnimplementedAbstractMethod(const Type *cls)
{
    const Type *chain[MAX_INHERITANCE_DEPTH];
    u32 depth = 0;
    cls = unThisType(cls);
    for (const Type *it = cls; it && depth < MAX_INHERITANCE_DEPTH;
         it = baseClassOf(it))
        chain[depth++] = it;

    while (depth > 0) {
        const Type *declaring = chain[--depth];
        const AstNode *decl = getTypeDecl(declaring);
        if (decl == NULL || !nodeIs(decl, ClassDecl))
            continue;
        for (const AstNode *member = decl->classDecl.members; member;
             member = member->next) {
            if (nodeIs(member, FuncDecl) && hasFlag(member, Abstract) &&
                !isAbstractMethodImplemented(cls, declaring, member))
                return member;
        }
    }
    return NULL;
}

// A class that still has an abstract method to implement is abstract too: it
// compiles, and an instance of it is refused where it is made
void inheritanceMarkAbstract(AstNode *node)
{
    if (findUnimplementedAbstractMethod(node->type) != NULL)
        node->flags |= flgAbstract;
}

AstNode *inheritanceBuildVTable(TypingContext *ctx, AstNode *node)
{
    AstNode *base = inheritanceVirtualBase(node);
    AstNode *def = getVTableDefault(base), *type = getVTableType(base);

    AstNodeList value = {NULL};
    AstNode *member = def->structExpr.fields;
    insertAstNode(
        &value,
        makeFieldExpr(ctx->pool,
                      &node->loc,
                      S___tid,
                      flgNone,
                      makeUnsignedIntegerLiteral(ctx->pool,
                                                 &node->loc,
                                                 unThisType(node->type)->index,
                                                 NULL,
                                                 def->type),
                      member,
                      NULL));
    member = member->next;
    for (; member; member = member->next) {
        AstNode *func =
            findVTableOverride(node, base, member->_namedNode.name);
        if (func != NULL) {
            // Change the function we resolve to
            insertAstNode(
                &value,
                makeFieldExpr(
                    ctx->pool,
                    &member->loc,
                    member->_namedNode.name,
                    member->flags,
                    func->funcDecl.body
                        ? makeMemberFunctionRef(ctx, node, func)
                        : makeNullLiteral(
                              ctx->pool, &func->loc, NULL, func->type),
                    member,
                    NULL));
        }
        else {
            insertAstNode(&value, deepCloneAstNode(ctx->pool, member));
        }
    }

    AstNode *vTable = makeVTableVariable(ctx, node, type, value.first);
    return makeVTableMemberInit(ctx, node, vTable);
}
