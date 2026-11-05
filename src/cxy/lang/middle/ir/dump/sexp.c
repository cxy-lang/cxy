#include "sexp.h"

#include "core/alloc.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/operator.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct NameMapping {
    cstring original;
    char canonical[96];
} NameMapping;

typedef struct SexpContext {
    FormatState *state;
    const IrSexpOptions *options;
    int depth;
    NameMapping *names;
    u32 nameCount, nameCapacity;
} SexpContext;

static void expr(SexpContext *ctx, const IrNode *node);
static void exprNode(SexpContext *ctx, const IrNode *node);

static void out(SexpContext *ctx, const char *fmt, const FormatArg *args)
{
    format(ctx->state, fmt, args);
}

static void space(SexpContext *ctx) { out(ctx, " ", NULL); }

static bool atomNeedsQuotes(cstring s)
{
    if (s == NULL || s[0] == '\0')
        return true;
    for (; *s; s++) {
        if (isspace((unsigned char)*s) || *s == '(' || *s == ')' || *s == '"' ||
            *s == ';' || *s == ',')
            return true;
    }
    return false;
}

static void atom(SexpContext *ctx, cstring s)
{
    if (!atomNeedsQuotes(s)) {
        out(ctx, "{s}", (FormatArg[]){{.s = s}});
        return;
    }
    out(ctx, "\"", NULL);
    for (cstring p = s ?: ""; *p; p++) {
        if (*p == '"' || *p == '\\')
            out(ctx, "\\{c}", (FormatArg[]){{.c = (u32)(unsigned char)*p}});
        else if (*p == '\n')
            out(ctx, "\\n", NULL);
        else
            out(ctx, "{c}", (FormatArg[]){{.c = (u32)(unsigned char)*p}});
    }
    out(ctx, "\"", NULL);
}

static void open(SexpContext *ctx, cstring tag)
{
    out(ctx, "({s}", (FormatArg[]){{.s = tag}});
}

static void close(SexpContext *ctx) { out(ctx, ")", NULL); }

static void newline(SexpContext *ctx)
{
    out(ctx, "\n", NULL);
    for (int i = 0; i < ctx->depth; i++)
        out(ctx, "  ", NULL);
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

// `__t14`, `_m4`, `__call_res_7`: a prefix of letters and underscores, then digits
static bool isGeneratedName(cstring name)
{
    if (name == NULL || name[0] != '_')
        return false;
    size_t len = strlen(name);
    if (len < 2 || !isdigit((unsigned char)name[len - 1]))
        return false;
    size_t digits = len;
    while (digits > 0 && isdigit((unsigned char)name[digits - 1]))
        digits--;
    return digits > 1;
}

static cstring varName(SexpContext *ctx, cstring name)
{
    if (!ctx->options->canonicalNames || !isGeneratedName(name))
        return name;

    for (u32 i = 0; i < ctx->nameCount; i++) {
        if (ctx->names[i].original == name || strcmp(ctx->names[i].original, name) == 0)
            return ctx->names[i].canonical;
    }

    size_t len = strlen(name), digits = len;
    while (digits > 0 && isdigit((unsigned char)name[digits - 1]))
        digits--;
    u32 index = 0;
    for (u32 i = 0; i < ctx->nameCount; i++) {
        const char *other = ctx->names[i].canonical;
        size_t otherLen = strlen(other), otherDigits = otherLen;
        while (otherDigits > 0 && isdigit((unsigned char)other[otherDigits - 1]))
            otherDigits--;
        if (otherDigits == digits && memcmp(other, name, digits) == 0)
            index++;
    }

    if (ctx->nameCount == ctx->nameCapacity) {
        ctx->nameCapacity = ctx->nameCapacity ? ctx->nameCapacity * 2 : 16;
        ctx->names =
            reallocOrDie(ctx->names, sizeof(NameMapping) * ctx->nameCapacity);
    }
    NameMapping *m = &ctx->names[ctx->nameCount++];
    m->original = name;
    snprintf(m->canonical, sizeof(m->canonical), "%.*s%u", (int)digits, name, index);
    return m->canonical;
}

static void clearNames(SexpContext *ctx) { ctx->nameCount = 0; }

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

static cstring typeText(const IrType *type, char *buf, size_t size)
{
    if (type == NULL)
        return "void";
    if (type->tag == irTypePointer && type->category == irTypeCatReference) {
        char inner[160];
        cstring pointee = typeText(type->pointer.pointee, inner, sizeof(inner));
        // The pointee of a class reference is the class struct, which has no `^`
        snprintf(buf, size, "&%s", pointee[0] == '^' ? pointee + 1 : pointee);
        return buf;
    }
    if (type->display)
        return type->display;
    if (type->name)
        return type->name;
    return "?";
}

static void typeAtom(SexpContext *ctx, const IrType *type)
{
    char buf[200];
    atom(ctx, typeText(type, buf, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

static void exprList(SexpContext *ctx, const IrNode *first)
{
    for (const IrNode *n = first; n; n = n->next) {
        space(ctx);
        expr(ctx, n);
    }
}

static void unary1(SexpContext *ctx, cstring tag, const IrNode *a)
{
    open(ctx, tag);
    space(ctx);
    expr(ctx, a);
    close(ctx);
}

static void sizedType(SexpContext *ctx, cstring tag, const IrType *type, const IrNode *size)
{
    open(ctx, tag);
    space(ctx);
    typeAtom(ctx, isIrPointerType(type) ? type->pointer.pointee : type);
    if (size) {
        space(ctx);
        expr(ctx, size);
    }
    close(ctx);
}

static void exprConst(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irNullConst:
        out(ctx, "null", NULL);
        break;
    case irBoolConst:
        out(ctx, node->boolConst.value ? "true" : "false", NULL);
        break;
    case irCharConst:
        out(ctx, "(Char {u32})", (FormatArg[]){{.u32 = node->charConst.value}});
        break;
    case irIntConst:
        out(ctx,
            node->intConst.isNegative ? "-{u128}" : "{u128}",
            (FormatArg[]){{.u128 = node->intConst.value}});
        break;
    case irFloatConst:
        out(ctx, "{f64}", (FormatArg[]){{.f64 = node->floatConst.value}});
        break;
    case irStringConst:
        atom(ctx, node->stringConst.value ?: "");
        break;
    case irArrayConst:
        open(ctx, "Array");
        exprList(ctx, node->arrayConst.elements);
        close(ctx);
        break;
    case irFieldConst:
        open(ctx, "Field");
        space(ctx);
        atom(ctx, node->fieldConst.name ?: "_");
        space(ctx);
        expr(ctx, node->fieldConst.value);
        close(ctx);
        break;
    case irStructConst:
        open(ctx, "Struct");
        space(ctx);
        typeAtom(ctx, node->type);
        exprList(ctx, node->structConst.fields);
        close(ctx);
        break;
    case irGlobalRef:
        open(ctx, "GlobalRef");
        space(ctx);
        atom(ctx, node->globalRef.name ?: "?");
        close(ctx);
        break;
    default:
        break;
    }
}

static void exprAccess(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irVarRef:
        open(ctx, "VarRef");
        space(ctx);
        atom(ctx, varName(ctx, node->varRef.name ?: "?"));
        close(ctx);
        break;
    case irMember:
        open(ctx, "Member");
        space(ctx);
        expr(ctx, node->member.target);
        space(ctx);
        if (node->member.name)
            atom(ctx, node->member.name);
        else
            out(ctx, "{u32}", (FormatArg[]){{.u32 = node->member.index}});
        close(ctx);
        break;
    case irIndex:
        open(ctx, "Index");
        space(ctx);
        expr(ctx, node->index.target);
        space(ctx);
        expr(ctx, node->index.index);
        close(ctx);
        break;
    case irAddrOf:
        unary1(ctx, "AddrOf", node->addrOf.target);
        break;
    case irDeref:
        unary1(ctx, "Deref", node->deref.target);
        break;
    case irAlias:
        expr(ctx, node->alias.expr);
        break;
    default:
        break;
    }
}

static void exprOperation(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irCast:
        open(ctx, "Cast");
        space(ctx);
        typeAtom(ctx, node->type);
        space(ctx);
        expr(ctx, node->cast.expr);
        close(ctx);
        break;
    case irUnary:
        open(ctx, "Unary");
        space(ctx);
        atom(ctx, getUnaryOpString(node->unary.op));
        space(ctx);
        expr(ctx, node->unary.operand);
        close(ctx);
        break;
    case irBinary:
        open(ctx, "Binary");
        space(ctx);
        atom(ctx, getBinaryOpString(node->binary.op));
        space(ctx);
        expr(ctx, node->binary.lhs);
        space(ctx);
        expr(ctx, node->binary.rhs);
        close(ctx);
        break;
    case irCall:
        open(ctx, "Call");
        space(ctx);
        expr(ctx, node->call.callee);
        exprList(ctx, node->call.args);
        close(ctx);
        break;
    case irAlloca:
        sizedType(ctx, "Alloca", node->alloca.elemType, node->alloca.size);
        break;
    case irMalloc:
        sizedType(ctx, "Malloc", node->malloc.elemType, node->malloc.size);
        break;
    case irSizeof:
        open(ctx, "Sizeof");
        space(ctx);
        typeAtom(ctx, node->sizeofVal.targetType);
        close(ctx);
        break;
    default:
        break;
    }
}

static void exprValue(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irOptionalValue:
        if (node->optionalValue.hasValue)
            unary1(ctx, "Some", node->optionalValue.value);
        else
            out(ctx, "None", NULL);
        break;
    case irUnionValue:
        open(ctx, "Variant");
        out(ctx, " {u32} ", (FormatArg[]){{.u32 = node->unionValue.index}});
        expr(ctx, node->unionValue.value);
        close(ctx);
        break;
    case irClosureValue:
        open(ctx, "Closure");
        space(ctx);
        if (node->closureValue.env)
            expr(ctx, node->closureValue.env);
        else
            out(ctx, "null", NULL);
        space(ctx);
        expr(ctx, node->closureValue.func);
        if (node->closureValue.dtor) {
            space(ctx);
            expr(ctx, node->closureValue.dtor);
        }
        close(ctx);
        break;
    case irAlloc:
        open(ctx, "Alloc");
        space(ctx);
        typeAtom(ctx, node->alloc.type);
        close(ctx);
        break;
    case irMove:
        unary1(ctx, "Move", node->move.src);
        break;
    default:
        break;
    }
}

static void exprNode(SexpContext *ctx, const IrNode *node)
{
    if (node == NULL) {
        out(ctx, "null", NULL);
        return;
    }
    switch (node->tag) {
    case irNullConst ... irGlobalRef:
        exprConst(ctx, node);
        break;
    case irVarRef:
    case irMember:
    case irIndex:
    case irAddrOf:
    case irDeref:
    case irAlias:
        exprAccess(ctx, node);
        break;
    case irCast:
    case irUnary:
    case irBinary:
    case irCall:
    case irAlloca:
    case irMalloc:
    case irSizeof:
        exprOperation(ctx, node);
        break;
    default:
        exprValue(ctx, node);
        break;
    }
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

static cstring blockName(const IrNode *bb)
{
    return bb ? bb->basicBlock.name ?: "bb" : "?";
}

static void marker(SexpContext *ctx, cstring name)
{
    out(ctx, " :{s}", (FormatArg[]){{.s = name}});
}

static void infoMarkers(SexpContext *ctx, const IrNode *node)
{
    static const cstring classNames[] = {
        NULL, "class-in-flight", "class-statement", "class-extended",
        "class-result", "class-return-slot", "class-pending", "class-transient"};
    static const struct {
        u32 bit;
        cstring name;
    } bits[] = {
        {irInfoOwnedPosition, "owned-position"},
        {irInfoMoved, "moved"},
        {irInfoUserWritten, "user-written"},
        {irInfoImplicit, "implicit"},
        {irInfoGlue, "glue"},
        {irInfoSynthetic, "synthetic"},
    };

    IrValueClass cls = irValueClass(node);
    if (cls != irClassNone && cls < sizeof(classNames) / sizeof(classNames[0]))
        marker(ctx, classNames[cls]);
    for (u32 i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
        if (irHasInfo(node, bits[i].bit))
            marker(ctx, bits[i].name);
    }
}

// Renders a node, then puts its info markers before the closing parenthesis.
// Atoms (constants) have no place for them and print as before.
static void withInfo(SexpContext *ctx,
                     const IrNode *node,
                     void (*render)(SexpContext *, const IrNode *))
{
    if (node == NULL || node->info == 0) {
        render(ctx, node);
        return;
    }

    FormatState *saved = ctx->state;
    FormatState scratch = newFormatState("  ", true);
    ctx->state = &scratch;
    render(ctx, node);
    ctx->state = saved;
    char *text = formatStateToString(&scratch);
    freeFormatState(&scratch);
    if (text == NULL)
        return;

    size_t len = strlen(text);
    if (len > 1 && text[0] == '(' && text[len - 1] == ')') {
        text[len - 1] = '\0';
        out(ctx, "{s}", (FormatArg[]){{.s = text}});
        infoMarkers(ctx, node);
        close(ctx);
    }
    else {
        out(ctx, "{s}", (FormatArg[]){{.s = text}});
    }
    free(text);
}

static void expr(SexpContext *ctx, const IrNode *node)
{
    withInfo(ctx, node, exprNode);
}

static void stmtVarDecl(SexpContext *ctx, const IrNode *node)
{
    open(ctx, "VarDecl");
    space(ctx);
    atom(ctx, varName(ctx, node->varDecl.name ?: "?"));
    space(ctx);
    typeAtom(ctx, node->type);

    close(ctx);
}

static void stmtSwitch(SexpContext *ctx, const IrNode *node)
{
    open(ctx, "Switch");
    space(ctx);
    expr(ctx, node->switchStmt.cond);
    ctx->depth++;
    for (u64 i = 0; i < node->switchStmt.casesCount; i++) {
        const IrCase *c = &node->switchStmt.cases[i];
        newline(ctx);
        open(ctx, "Case");
        space(ctx);
        expr(ctx, c->valNode);
        if (c->endNode) {
            space(ctx);
            expr(ctx, c->endNode);
        }
        space(ctx);
        atom(ctx, blockName(c->target));
        close(ctx);
    }
    if (node->switchStmt.defaultTarget) {
        newline(ctx);
        open(ctx, "Default");
        space(ctx);
        atom(ctx, blockName(node->switchStmt.defaultTarget));
        close(ctx);
    }
    ctx->depth--;
    close(ctx);
}

static void stmtControl(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irBranch:
        open(ctx, "Goto");
        space(ctx);
        atom(ctx, blockName(node->branch.target));
        close(ctx);
        break;
    case irBranchIf:
        open(ctx, "If");
        space(ctx);
        expr(ctx, node->branchIf.cond);
        space(ctx);
        atom(ctx, blockName(node->branchIf.trueTarget));
        space(ctx);
        atom(ctx, blockName(node->branchIf.falseTarget));
        close(ctx);
        break;
    case irSwitch:
        stmtSwitch(ctx, node);
        break;
    case irReturn:
        open(ctx, "Return");
        if (node->returnStmt.value) {
            space(ctx);
            expr(ctx, node->returnStmt.value);
        }
        close(ctx);
        break;
    case irUnreachable:
        out(ctx, "(Unreachable)", NULL);
        break;
    default:
        break;
    }
}

static void stmtOwnership(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irAssign:
        open(ctx, "Assign");
        space(ctx);
        expr(ctx, node->assign.lhs);
        space(ctx);
        expr(ctx, node->assign.rhs);
        if (hasFlag(node, Unmanaged))
            marker(ctx, "unmanaged");
        close(ctx);
        break;
    case irCopy:
        open(ctx, "Copy");
        space(ctx);
        expr(ctx, node->copy.src);
        space(ctx);
        expr(ctx, node->copy.dst);
        close(ctx);
        break;
    case irDrop:
        open(ctx, node->drop.flag ? "DropIf" : "Drop");
        space(ctx);
        expr(ctx, node->drop.target);
        if (node->drop.flag) {
            space(ctx);
            expr(ctx, node->drop.flag);
        }
        close(ctx);
        break;
    case irZeromem:
        unary1(ctx, "Zeromem", node->zeromem.target);
        break;
    default:
        break;
    }
}

static void stmtNode(SexpContext *ctx, const IrNode *node)
{
    switch (node->tag) {
    case irVarDecl:
        stmtVarDecl(ctx, node);
        break;
    case irScopeEnter:
        out(ctx, "(ScopeEnter {u32})", (FormatArg[]){{.u32 = node->scopeEnter.scopeId}});
        break;
    case irScopeExit:
        out(ctx, "(ScopeExit {u32})", (FormatArg[]){{.u32 = node->scopeExit.scopeId}});
        break;
    case irAssign:
    case irCopy:
    case irDrop:
    case irZeromem:
        stmtOwnership(ctx, node);
        break;
    case irBranch:
    case irBranchIf:
    case irSwitch:
    case irReturn:
    case irUnreachable:
        stmtControl(ctx, node);
        break;
    case irInlineAsm:
        out(ctx, "(Asm)", NULL);
        break;
    default:
        // an expression statement: a call, or a bare move
        exprNode(ctx, node);
        break;
    }
}

static void stmt(SexpContext *ctx, const IrNode *node)
{
    withInfo(ctx, node, stmtNode);
}

static void block(SexpContext *ctx, const IrNode *bb)
{
    open(ctx, "Block");
    space(ctx);
    atom(ctx, blockName(bb));
    ctx->depth++;
    for (const IrNode *s = bb->basicBlock.first; s; s = s->next) {
        newline(ctx);
        stmt(ctx, s);
    }
    ctx->depth--;
    close(ctx);
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

static void param(SexpContext *ctx, const IrNode *p)
{
    open(ctx, "Param");
    space(ctx);
    atom(ctx, p->funcParam.name ?: "?");
    space(ctx);
    typeAtom(ctx, p->type);
    close(ctx);
}

static void function(SexpContext *ctx, const IrNode *fn)
{
    clearNames(ctx);
    open(ctx, "Function");
    space(ctx);
    atom(ctx, fn->function.display ?: fn->function.name ?: "?");
    for (const IrNode *p = fn->function.params; p; p = p->next) {
        space(ctx);
        param(ctx, p);
    }
    space(ctx);
    open(ctx, "Ret");
    space(ctx);
    typeAtom(ctx, fn->type ? fn->type->func.ret : NULL);
    close(ctx);
    infoMarkers(ctx, fn);

    if (hasFlag(fn, Extern) || fn->function.firstBlock == NULL) {
        out(ctx, " (Extern)", NULL);
        close(ctx);
        return;
    }

    ctx->depth++;
    for (const IrNode *bb = fn->function.firstBlock; bb; bb = bb->next) {
        newline(ctx);
        block(ctx, bb);
    }
    ctx->depth--;
    close(ctx);
}

static void global(SexpContext *ctx, const IrNode *g)
{
    open(ctx, "Global");
    space(ctx);
    atom(ctx, g->globalVar.name ?: "?");
    space(ctx);
    typeAtom(ctx, g->type);
    if (g->globalVar.init) {
        space(ctx);
        expr(ctx, g->globalVar.init);
    }
    close(ctx);
}

static void module(SexpContext *ctx, const IrNode *m)
{
    open(ctx, "Module");
    space(ctx);
    atom(ctx, m->module.name ?: "_");
    ctx->depth++;
    for (const IrNode *g = m->module.globals; g; g = g->next) {
        newline(ctx);
        global(ctx, g);
    }
    for (const IrNode *fn = m->module.functions; fn; fn = fn->next) {
        newline(ctx);
        function(ctx, fn);
    }
    ctx->depth--;
    close(ctx);
}

void dumpIrToSexpState(const IrNode *node,
                       FormatState *state,
                       const IrSexpOptions *options)
{
    static const IrSexpOptions defaults = {.canonicalNames = false};
    SexpContext ctx = {.state = state, .options = options ? options : &defaults};

    if (node == NULL) {
        out(&ctx, "(null)", NULL);
        return;
    }

    // Number the names of the enclosing function first, off the record
    if (ctx.options->canonicalNames && ctx.options->function &&
        ctx.options->function != node) {
        FormatState scratch = newFormatState("  ", true);
        ctx.state = &scratch;
        function(&ctx, ctx.options->function);
        ctx.state = state;
        ctx.depth = 0;
        freeFormatState(&scratch);
    }
    switch (node->tag) {
    case irModule:
        module(&ctx, node);
        break;
    case irFunction:
        function(&ctx, node);
        break;
    case irBasicBlock:
        block(&ctx, node);
        break;
    case irGlobalVar:
        global(&ctx, node);
        break;
    case irFuncParam:
        param(&ctx, node);
        break;
    default:
        // a statement, or an expression (statements fall back to expressions)
        stmt(&ctx, node);
        break;
    }
    free(ctx.names);
}

AstNode *dumpIrToSexp(CompilerDriver *driver, const IrNode *module, FILE *file)
{
    (void)driver;
    // Stable output: generated names are numbered per function
    static const IrSexpOptions options = {.canonicalNames = true};
    FormatState state = newFormatState("  ", true);
    dumpIrToSexpState(module, &state, &options);
    char *str = formatStateToString(&state);
    if (str) {
        fputs(str, file);
        fputs("\n", file);
        free(str);
    }
    freeFormatState(&state);
    return NULL;
}
