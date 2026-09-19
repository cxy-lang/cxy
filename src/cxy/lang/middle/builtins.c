//
// Created by Carter on 2023-09-01.
//

#include "builtins.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"

typedef enum BuiltinContractKind {
    bckClass,
    bckStruct,
    bckFunction
} BuiltinContractKind;

typedef struct BuiltinContract {
    cstring name;
    BuiltinContractKind kind;
    cstring requiredBase;
    bool (*validate)(Log *L, const AstNode *decl, const Type *type);
} BuiltinContract;

static bool validateOutputStreamContract(Log *L, const AstNode *decl, const Type *type)
{
    const NamedTypeMember *appendMember = findClassMember(type, S_append);
    if (!appendMember || !typeIs(appendMember->type, Func)) {
        logError(L, &decl->loc,
                 "builtin class 'OutputStream' must implement virtual method 'append(str: ^const void, size: u64)'",
                 NULL);
        return false;
    }
    return true;
}

static bool validateStringContract(Log *L, const AstNode *decl, const Type *type)
{
    if (!type->tClass.inheritance || !type->tClass.inheritance->base ||
        type->tClass.inheritance->base->name != S_OutputStream) {
        logError(L, &decl->loc, "builtin class 'String' must inherit from 'OutputStream'", NULL);
        return false;
    }
    const NamedTypeMember *initMember = findClassMember(type, S_InitOverload);
    if (!initMember) {
        logError(L, &decl->loc, "builtin class 'String' must define constructor 'init'", NULL);
        return false;
    }
    return true;
}

static bool validateExceptionContract(Log *L, const AstNode *decl, const Type *type)
{
    const NamedTypeMember *nameMember = findClassMember(type, S_name);
    if (!nameMember) {
        logError(L, &decl->loc, "builtin class 'Exception' must declare a 'name' member", NULL);
        return false;
    }
    const NamedTypeMember *pushMember = findClassMember(type, S_push);
    if (!pushMember || !typeIs(pushMember->type, Func)) {
        logError(L, &decl->loc, "builtin class 'Exception' must declare a 'push' method", NULL);
        return false;
    }
    return true;
}

static const BuiltinContract builtinContracts[] = {
    {.name = "OutputStream", .kind = bckClass, .requiredBase = NULL, .validate = validateOutputStreamContract},
    {.name = "String", .kind = bckClass, .requiredBase = "OutputStream", .validate = validateStringContract},
    {.name = "Exception", .kind = bckClass, .requiredBase = NULL, .validate = validateExceptionContract},
};

static const BuiltinContract *findBuiltinContract(cstring name)
{
    for (size_t i = 0; i < sizeof(builtinContracts) / sizeof(builtinContracts[0]); ++i) {
        if (builtinContracts[i].name == name || strcmp(builtinContracts[i].name, name) == 0)
            return &builtinContracts[i];
    }
    return NULL;
}

bool checkBuiltinAttribute(Log *L, const AstNode *decl, bool isBuiltinModule)
{
    const AstNode *attr = findAttribute(decl, S_builtin);
    if (attr == NULL && nodeIs(decl, GenericDecl))
        attr = findAttribute(decl->genericDecl.decl, S_builtin);
    if (attr == NULL)
        return true;

    if (!isBuiltinModule) {
        logError(L, &attr->loc, "@builtin attribute is only allowed in the builtins module", NULL);
        return false;
    }

    if (attr->attr.args != NULL) {
        logError(L, &attr->loc, "@builtin attribute does not accept arguments", NULL);
        return false;
    }

    cstring name = getDeclarationName(decl);
    const BuiltinContract *contract = findBuiltinContract(name);
    if (contract == NULL) {
        logError(L, &decl->loc, "@builtin on '{s}', but '{s}' is not a known builtin contract",
                 (FormatArg[]){{.s = name}, {.s = name}});
        return false;
    }

    if (contract->kind == bckClass && !nodeIs(decl, ClassDecl)) {
        logError(L, &decl->loc, "builtin contract '{s}' requires a class declaration",
                 (FormatArg[]){{.s = name}});
        return false;
    }

    if (contract->requiredBase != NULL) {
        if (decl->classDecl.base == NULL ||
            decl->classDecl.base->path.elements->pathElement.name != contract->requiredBase &&
            strcmp(decl->classDecl.base->path.elements->pathElement.name, contract->requiredBase) != 0) {
            logError(L, &decl->loc, "builtin class '{s}' must inherit from '{s}'",
                     (FormatArg[]){{.s = name}, {.s = contract->requiredBase}});
            return false;
        }
    }
    else if (nodeIs(decl, ClassDecl) && decl->classDecl.base != NULL) {
        logError(L, &decl->loc, "builtin class '{s}' must not have a base class",
                 (FormatArg[]){{.s = name}});
        return false;
    }

    return true;
}

bool validateBuiltinContracts(Log *L, const Type *module)
{
    if (module == NULL)
        return false;
    for (size_t i = 0; i < sizeof(builtinContracts) / sizeof(builtinContracts[0]); ++i) {
        const BuiltinContract *contract = &builtinContracts[i];
        const NamedTypeMember *member = findModuleMember(module, contract->name);
        if (member == NULL) {
            logError(L, builtinLoc(), "missing required builtin contract '{s}'",
                     (FormatArg[]){{.s = contract->name}});
            return false;
        }
        if (contract->validate && !contract->validate(L, member->decl, member->type))
            return false;
    }
    return true;
}

static struct {
    Log *L;
    MemPool *pool;
    const Type *module;
    HashTable overrides;
    HashTable aliases;
    FileLoc loc;
} cxy_builtins = {NULL};

typedef struct BuiltinAlias {
    cstring name;
    AstNode *decl;
    const Type *type;
} BuiltinAlias;

typedef struct BuiltinOverride {
    cstring name;
    AstNode *node;
} BuiltinOverride;

static inline const BuiltinAlias *findBuiltinAlias(cstring name)
{
    if (cxy_builtins.aliases.elems == NULL)
        return NULL;
    return findInHashTable(&cxy_builtins.aliases,
                           &(BuiltinAlias){.name = name},
                           hashPtr(hashInit(), name),
                           sizeof(BuiltinAlias),
                           comparePointers);
}

static inline void addBuiltinAlias(cstring name, AstNode *decl, const Type *type)
{
    insertInHashTable(&cxy_builtins.aliases,
                      &(BuiltinAlias){.name = name, .decl = decl, .type = type},
                      hashPtr(hashInit(), name),
                      sizeof(BuiltinAlias),
                      comparePointers);
}

static inline AstNode *findBuiltinOverride(cstring name)
{
    BuiltinOverride *override =
        findInHashTable(&cxy_builtins.overrides,
                        &(BuiltinOverride){.name = name},
                        hashPtr(hashInit(), name),
                        sizeof(BuiltinOverride),
                        comparePointers);
    return override ? override->node : NULL;
}

static inline void addOverridableBuiltin(cstring name)
{
    insertInHashTable(&cxy_builtins.overrides,
                      &(BuiltinOverride){.name = name},
                      hashPtr(hashInit(), name),
                      sizeof(BuiltinOverride),
                      comparePointers);
}

bool isBuiltinsInitialized() { return cxy_builtins.module != NULL; }

void initializeBuiltins(Log *L, MemPool *pool)
{
    if (cxy_builtins.L != NULL) {
        logWarning(
            L, NULL, "re-initializing builtins should not allowed", NULL);
        return;
    }
    csAssert0(cxy_builtins.module == NULL);
    cxy_builtins.L = L;
    cxy_builtins.pool = pool;
    cxy_builtins.module = NULL;
    cxy_builtins.overrides = newHashTable(sizeof(BuiltinOverride), pool);
    cxy_builtins.aliases = newHashTable(sizeof(BuiltinAlias), pool);
#define f(name) addOverridableBuiltin(S_##name);
    OVERRIDABLE_BUILTINS(f)
#undef f
}

void setBuiltinsModule(const Type *module, const FileLoc *loc)
{
    csAssert0(cxy_builtins.module == NULL);
    cxy_builtins.module = module;
    cxy_builtins.loc = *loc;

    if (module && module->module.members) {
        for (u64 i = 0; i < module->module.members->count; i++) {
            const NamedTypeMember *member = &module->module.members->members[i];
            const AstNode *decl = member->decl;
            if (decl == NULL)
                continue;
            const AstNode *aliasAttr = findAttribute(decl, S_alias);
            if (aliasAttr) {
                const AstNode *arg = findAttributeArgument(aliasAttr, S_name)
                                         ?: getAttributeArgument(NULL, NULL, aliasAttr, 0);
                if (arg && nodeIs(arg, StringLit)) {
                    addBuiltinAlias(arg->stringLiteral.value, (AstNode *)decl, member->type);
                }
            }
        }
    }

    validateBuiltinContracts(cxy_builtins.L, module);
}

AstNode *findBuiltinDecl(cstring name)
{
    AstNode *node = findBuiltinOverride(name);
    if (node == NULL) {
        csAssert0(cxy_builtins.module);
        const NamedTypeMember *member =
            findModuleMember(cxy_builtins.module, name);
        if (member)
            node = (AstNode *)member->decl;
        else {
            const BuiltinAlias *alias = findBuiltinAlias(name);
            node = alias ? alias->decl : NULL;
        }
    }
    return node;
}

const Type *findBuiltinType(cstring name)
{
    if (cxy_builtins.module) {
        const NamedTypeMember *member =
            findModuleMember(cxy_builtins.module, name);
        if (member)
            return member->type;
        const BuiltinAlias *alias = findBuiltinAlias(name);
        return alias ? alias->type : NULL;
    }
    return NULL;
}

bool isBuiltinString(const Type *type)
{
    static const Type *tString = NULL, *tCString = NULL;
    if (tString == NULL && isBuiltinsInitialized()) {
        tString = findBuiltinType(S_String);
        tCString = findBuiltinType(S___string);
    }
    return tString == type || tCString == type;
}

bool overrideBuiltin(cstring name, AstNode *node)
{
    BuiltinOverride *override =
        findInHashTable(&cxy_builtins.overrides,
                        &(BuiltinOverride){.name = name},
                        hashPtr(hashInit(), name),
                        sizeof(BuiltinOverride),
                        comparePointers);
    if (override == NULL) {
        logError(cxy_builtins.L,
                 &node->loc,
                 "builtin override not supported `{s}` is not a builtin",
                 (FormatArg[]){{.s = name}});
        return false;
    }

    if (override->node != NULL) {
        logError(cxy_builtins.L,
                 &node->loc,
                 "builtin `{s}` already has an override, overriding again can "
                 "lead to undefined behaviour",
                 (FormatArg[]){{.s = name}});
        logNote(cxy_builtins.L,
                &override->node->loc,
                "override provided here",
                NULL);
        return false;
    }
    node->flags |= flgPublic;
    override->node = node;
    return true;
}
