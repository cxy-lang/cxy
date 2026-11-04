#include "codegen.h"
#include "core/format.h"
#include "typegraph.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/middle/ir/context.h"
#include "lang/middle/ir/dump.h"
#include "lang/middle/ir/lower.h"
#include "lang/middle/ir/node.h"
#include "lang/middle/ir/reachability.h"
#include "lang/middle/ir/type.h"
#include "lang/middle/ir/visitor.h"
#include "lang/operations.h"
#include "src/prologue.h"
#include "src/epilogue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

static void getOutputPath(FormatState *cmd,
                          StrPool *strings,
                          Options *opts,
                          cstring ext)
{
    (void)strings;
    if (opts->output == NULL) {
        format(cmd,
               "{s}/app{s}",
               (FormatArg[]){{.s = opts->buildDir ?: "."}, {.s = ext}});
    }
    else if (opts->output[0] == '/') {
        format(cmd, "{s}{s}", (FormatArg[]){{.s = opts->output}, {.s = ext}});
    }
    else {
        format(cmd,
               "{s}/{s}{s}",
               (FormatArg[]){{.s = opts->buildDir ?: "."},
                             {.s = opts->output},
                             {.s = ext}});
    }
}

void *initCompilerBackend(CompilerDriver *driver, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    csAssert0(driver->backend == NULL);
    Options *options = &driver->options;

    bool toStdout = (options->output == NULL && options->cmd == cmdDev &&
                     options->dev.lastStage == ccsCodegen);

    FILE *f = NULL;
    cstring filename = NULL;
    if (toStdout) {
        f = stdout;
    }
    else {
        if (options->output == NULL) {
            filename =
                joinPath(driver->strings, options->buildDir ?: "./", "app.c");
        }
        else if (options->output[0] == '/') {
            filename = makeStringConcat(driver->strings, options->output, ".c");
        }
        else {
            filename =
                joinPath(driver->strings,
                         options->buildDir ?: "./",
                         makeStringConcat(driver->strings, options->output, ".c"));
        }

        struct stat st;
        if (stat(filename, &st) == 0) {
            remove(filename);
        }

        f = fopen(filename, "w+");
        if (f == NULL) {
            logError(driver->L,
                     builtinLoc(),
                     "creating output file '{s}' failed: '{s}'",
                     (FormatArg[]){{.s = filename}, {.s = strerror(errno)}});
            return NULL;
        }
    }

    CBackend *backend = callocFromMemPool(driver->pool, 1, sizeof(CBackend));
    driver->backend = backend;
    backend->output = f;
    backend->filename = filename;
    backend->testMode = options->cmd == cmdTest;

    if (!toStdout || (!options->dev.cleanAst && !options->withoutBuiltins)) {
        if (driver->options.withMemoryTrace) {
            fputs("#define ENABLE_MEMORY_TRACE 1\n", f);
        }
        fwrite(CXY_PROLOGUE_SOURCE, 1, CXY_PROLOGUE_SOURCE_SIZE - 1, f);
    }
    return backend;
}

static void emitCTypeName(FormatState *state, const IrType *type)
{
    if (type == NULL) {
        format(state, "void", NULL);
        return;
    }

    if (type->category == irTypeCatEnum && type->name) {
        format(state, "{s}", (FormatArg[]){{.s = type->name}});
        return;
    }

    switch (type->tag) {
    case irTypeVoid: format(state, "void", NULL); break;
    case irTypeBool: format(state, "bool", NULL); break;
    case irTypeChar: format(state, "char", NULL); break;
    case irTypeI8: format(state, "int8_t", NULL); break;
    case irTypeI16: format(state, "int16_t", NULL); break;
    case irTypeI32: format(state, "int32_t", NULL); break;
    case irTypeI64: format(state, "int64_t", NULL); break;
    case irTypeI128: format(state, "int128_t", NULL); break;
    case irTypeU8: format(state, "uint8_t", NULL); break;
    case irTypeU16: format(state, "uint16_t", NULL); break;
    case irTypeU32: format(state, "uint32_t", NULL); break;
    case irTypeU64: format(state, "uint64_t", NULL); break;
    case irTypeU128: format(state, "uint128_t", NULL); break;
    case irTypeF32: format(state, "float", NULL); break;
    case irTypeF64: format(state, "double", NULL); break;
    case irTypePointer:
        emitCTypeName(state, type->pointer.pointee);
        format(state, " *", NULL);
        break;
    case irTypeArray:
        if (type->name)
            format(state, "{s}", (FormatArg[]){{.s = type->name}});
        else {
            emitCTypeName(state, type->array.elem);
            format(state, " *", NULL);
        }
        break;
    case irTypeStruct:
    case irTypeUntaggedUnion:
        if (type->name)
            format(state, "{s}", (FormatArg[]){{.s = type->name}});
        else
            format(state, "struct _anon_struct", NULL);
        break;
    case irTypeFunc:
        if (type->name)
            format(state, "{s}", (FormatArg[]){{.s = type->name}});
        else
            format(state, "void *", NULL);
        break;
    default:
        format(state, "void", NULL);
        break;
    }
}

static inline void emitLineDirective(CodegenContext *ctx, const FileLoc *loc)
{
    if (!ctx->debug.enabled || loc == NULL || loc->fileName == NULL || loc->begin.row == 0)
        return;
    if (strcmp(loc->fileName, "<builtin>") == 0)
        return;
    if (loc->fileName == ctx->debug.fileName && loc->begin.row == ctx->debug.line)
        return;

    ctx->debug.fileName = loc->fileName;
    ctx->debug.line = loc->begin.row;
    format(&ctx->state, "#line {u32} \"{s}\"\n", (FormatArg[]){
        {.u32 = loc->begin.row},
        {.s = loc->fileName}
    });
}

static void emitNullConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->type && (node->type->tag == irTypeStruct || node->type->tag == irTypeUntaggedUnion) && node->type->category != irTypeCatClass) {
        format(&ctx->state, "(", NULL);
        emitCTypeName(&ctx->state, node->type);
        format(&ctx->state,
               node->type->strct.count ? "){{0}" : "){{}",
               NULL);
    }
    else {
        format(&ctx->state, "nullptr", NULL);
    }
}

static void emitBoolConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{s}", (FormatArg[]){{.s = node->boolConst.value ? "true" : "false"}});
}

static void emitCharConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    u32 value = (u32)node->charConst.value;
    if (value > 0x7F) {
        // A C character literal holds one byte, write the code as a number
        format(&ctx->state, "((", NULL);
        if (node->type)
            emitCTypeName(&ctx->state, node->type);
        else
            format(&ctx->state, "uint32_t", NULL);
        format(&ctx->state, "){u32})", (FormatArg[]){{.u32 = value}});
        return;
    }
    format(&ctx->state, "'", NULL);
    printUtf8(&ctx->state, value, true);
    format(&ctx->state, "'", NULL);
}

static void emitIntConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    u128 value = node->intConst.value;
    IrTypeTag tag = node->type ? node->type->tag : irTypeI32;

    // INT64_MIN can't be written as a negated literal
    if (tag == irTypeI64 && node->intConst.isNegative && value == ((u128)1 << 63)) {
        format(&ctx->state, "(-9223372036854775807LL - 1)", NULL);
        return;
    }

    cstring suffix = "";
    switch (tag) {
    case irTypeI64: suffix = "LL"; break;
    case irTypeU64: suffix = "ULL"; break;
    case irTypeU32: suffix = "U"; break;
    default: break;
    }

    if (node->intConst.isNegative)
        format(&ctx->state,
               "(-{u128}{s})",
               (FormatArg[]){{.u128 = value}, {.s = suffix}});
    else
        format(&ctx->state, "{u128}{s}", (FormatArg[]){{.u128 = value}, {.s = suffix}});
}

static void emitFloatConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{f64}", (FormatArg[]){{.f64 = node->floatConst.value}});
}

static void emitStringConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    cstring p = node->stringConst.value ?: "";
    format(&ctx->state, "\"", NULL);
    while (*p != '\0') {
        if (p[0] == '\\' && (p[1] == 'x' || p[1] == 'X' || isdigit(p[1]))) {
            // Pre-existing hex/octal escape — pass through unescaped
            printUtf8(&ctx->state, (u8)*p++, false);
        }
        else {
            // Decode full UTF-8 code point before passing to printUtf8
            uint32_t cp = decodeUtf8Char(&p);
            printUtf8(&ctx->state, cp, true);
        }
    }
    format(&ctx->state, "\"", NULL);
}

static void emitArrayConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{{", NULL);
    u64 i = 0;
    for (const IrNode *elem = node->arrayConst.elements; elem; elem = elem->next, i++) {
        if (i > 0)
            format(&ctx->state, ", ", NULL);
        irConstVisit(visitor, elem);
    }
    format(&ctx->state, "}", NULL);
}

static void emitFieldConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->fieldConst.name) {
        format(&ctx->state, ".{s} = ", (FormatArg[]){{.s = node->fieldConst.name}});
    }
    irConstVisit(visitor, node->fieldConst.value);
}

static void emitStructConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(", NULL);
    emitCTypeName(&ctx->state, node->type);
    format(&ctx->state, "){{", NULL);
    u64 i = 0;
    for (const IrNode *fld = node->structConst.fields; fld; fld = fld->next, i++) {
        if (i > 0)
            format(&ctx->state, ", ", NULL);
        irConstVisit(visitor, fld);
    }
    format(&ctx->state, "}", NULL);
}

static void emitVarRef(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{s}", (FormatArg[]){{.s = node->varRef.name ?: "var"}});
}

static void emitGlobalRef(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    cstring name = node->globalRef.name;
    if (name == NULL && node->globalRef.target && node->globalRef.target->tag == irFunction)
        name = node->globalRef.target->function.name;
    else if (name == NULL && node->globalRef.target && node->globalRef.target->tag == irGlobalVar)
        name = node->globalRef.target->globalVar.name;
    format(&ctx->state, "{s}", (FormatArg[]){{.s = name ?: "global"}});
}

// Trace builds: edge code for the value of `type` stored at the C expression
// `expr`. Owned references are reported from `obj`: class handles and closure
// environments. References, raw pointers, unions and arrays are not owners as
// far as this glue can tell; containers report their buffers and elements from
// a `__visit` member.
static void emitVisitValue(FormatState *out, const IrType *type, cstring expr, u32 depth)
{
    if (type == NULL || depth > 6)
        return;

    char sub[1024];
    switch (type->category) {
    case irTypeCatClass:
        if (isIrPointerType(type)) {
            format(out, "  __visit_edge(v, obj, {s});\n", (FormatArg[]){{.s = expr}});
            return;
        }
        break; // an embedded base class: its fields follow
    case irTypeCatClosure:
        format(out,
               "  if ({s}.dtor) __visit_edge(v, obj, {s}.env);\n",
               (FormatArg[]){{.s = expr}, {.s = expr}});
        return;
    case irTypeCatOptional:
        if (type->strct.count > 1) {
            format(out, "  if ({s}.ok) {{\n", (FormatArg[]){{.s = expr}});
            snprintf(sub, sizeof(sub), "%s.val", expr);
            emitVisitValue(out, type->strct.fields[1].type, sub, depth + 1);
            format(out, "  }\n", NULL);
        }
        return;
    case irTypeCatStruct:
    case irTypeCatTuple: break;
    default: return;
    }

    for (u64 i = 0; i < type->strct.count; i++) {
        const IrField *f = &type->strct.fields[i];
        if (f->name)
            snprintf(sub, sizeof(sub), "%s.%s", expr, f->name);
        else
            snprintf(sub, sizeof(sub), "%s._%u", expr, (unsigned)i);
        emitVisitValue(out, f->type, sub, depth + 1);
    }
}

// `static void __visit_<Class>(void *obj, void *v)`, the edge visitor stored in
// the class's TypeInfo
static void emitVisitFunction(CodegenContext *ctx, const IrType *st)
{
    FormatState *out = &ctx->typeInfos;
    format(out, "static void __visit_{s}(void *obj, void *v) {{\n  ", (FormatArg[]){{.s = st->name}});
    emitCTypeName(out, st);
    format(out, " *o = obj;\n  (void)o;\n", NULL);

    char expr[1024];
    for (u64 i = 0; i < st->strct.count; i++) {
        const IrField *f = &st->strct.fields[i];
        if (f->name)
            snprintf(expr, sizeof(expr), "o->%s", f->name);
        else
            snprintf(expr, sizeof(expr), "o->_%u", (unsigned)i);
        emitVisitValue(out, f->type, expr, 0);
    }
    if (st->strct.visit)
        format(out, "  {s}(o, v);\n", (FormatArg[]){{.s = st->strct.visit->function.name}});
    format(out, "}\n", NULL);
}

// Emits the `const __TypeInfo` of a class once, under the symbol
// `__ti_<type name>`. `dropFields` holds the whole destructor chain until the
// allocation phase splits `deinit` from it.
static void emitTypeInfo(CodegenContext *ctx, const IrType *st)
{
    for (u32 i = 0; i < ctx->typeInfoTypes.size; i++) {
        const IrType *seen = dynArrayAt(const IrType **, &ctx->typeInfoTypes, i);
        if (seen == st || (seen->name && st->name && strcmp(seen->name, st->name) == 0))
            return;
    }
    u32 index = ctx->typeInfoTypes.size;
    pushOnDynArray(&ctx->typeInfoTypes, &st);

    FormatState *out = &ctx->typeInfos;
    if (ctx->memTraceEnabled)
        emitVisitFunction(ctx, st);
    format(out, "static const __TypeInfo __ti_{s} = {{ .name = \"{s}\", .size = sizeof(",
           (FormatArg[]){{.s = st->name}, {.s = st->display ?: st->name}});
    emitCTypeName(out, st);
    format(out, "), .align = _Alignof(", NULL);
    emitCTypeName(out, st);
    format(out, "), .index = {u32}, .dropFields = ", (FormatArg[]){{.u32 = index}});
    if (st->strct.dctorFwd)
        format(out, "{s}", (FormatArg[]){{.s = st->strct.dctorFwd->function.name}});
    else if (st->strct.dctor)
        format(out, "{s}", (FormatArg[]){{.s = st->strct.dctor->function.name}});
    else
        format(out, "nullptr", NULL);
    if (ctx->memTraceEnabled)
        format(out, ", .visit = __visit_{s}", (FormatArg[]){{.s = st->name}});
    format(out, " };\n", NULL);
}

// Trace builds: one `const __Site` per (file, line, column, function), the
// returned index names it as `__site_<index>`
static u32 emitSite(CodegenContext *ctx, const FileLoc *loc)
{
    cstring file = loc->fileName ?: "";
    cstring function = ctx->functionName ?: "";
    for (u32 i = 0; i < ctx->sites.size; i++) {
        const CodegenSite *s = &dynArrayAt(CodegenSite *, &ctx->sites, i);
        if (s->line == loc->begin.row && s->column == loc->begin.col &&
            strcmp(s->file, file) == 0 && strcmp(s->function, function) == 0)
            return i;
    }

    u32 index = ctx->sites.size;
    CodegenSite site = {
        .file = file, .function = function, .line = loc->begin.row, .column = loc->begin.col};
    pushOnDynArray(&ctx->sites, &site);
    format(&ctx->typeInfos,
           "static const __Site __site_{u32} = {{ .file = \"{s}\", .line = {u32}, .column = {u32}, "
           ".function = \"{s}\", .index = {u32} };\n",
           (FormatArg[]){{.u32 = index},
                         {.s = file},
                         {.u32 = loc->begin.row},
                         {.u32 = loc->begin.col},
                         {.s = function},
                         {.u32 = index}});
    return index;
}

// Trace builds: functions with the CallSite attribute (generated `Class(args)`
// constructors) take the caller's site as a hidden first parameter `__site`
static bool hasCallSite(const CodegenContext *ctx, const IrNode *func)
{
    return ctx->memTraceEnabled && func != NULL && func->tag == irFunction &&
           (func->function.attrs & irFuncAttrCallSite) != 0;
}

static bool compareFuncNames(const void *lhs, const void *rhs)
{
    return strcmp(*(cstring *)lhs, *(cstring *)rhs) == 0;
}

// A callee can reference a function that is lowered later, so it is matched by
// symbol name against the set collected before emission
static bool calleeHasCallSite(CodegenContext *ctx, const IrNode *callee)
{
    if (!ctx->memTraceEnabled || callee == NULL || callee->tag != irGlobalRef)
        return false;
    cstring name = callee->globalRef.name;
    if (name == NULL && callee->globalRef.target &&
        callee->globalRef.target->tag == irFunction)
        name = callee->globalRef.target->function.name;
    if (name == NULL)
        return false;
    return findInHashTable(&ctx->callSiteFuncs,
                           &name,
                           hashStr(hashInit(), name),
                           sizeof(cstring),
                           compareFuncNames) != NULL;
}

// The incoming site inside a function that has one, otherwise the site of the
// expression at `loc`
static void emitSiteArg(CodegenContext *ctx, const FileLoc *loc)
{
    if (hasCallSite(ctx, ctx->function))
        format(&ctx->state, "__site", NULL);
    else
        format(&ctx->state, "&__site_{u32}", (FormatArg[]){{.u32 = emitSite(ctx, loc)}});
}

static void emitAlloc(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    const IrType *type = node->alloc.type;
    const IrType *st = isIrPointerType(type) ? type->pointer.pointee : type;
    emitTypeInfo(ctx, st);
    if (ctx->memTraceEnabled) {
        format(&ctx->state, "__mm_alloc_trace(&__ti_{s}, ", (FormatArg[]){{.s = st->name}});
        emitSiteArg(ctx, &node->loc);
        format(&ctx->state, ")", NULL);
    }
    else {
        format(&ctx->state, "__mm_alloc(&__ti_{s}, nullptr)", (FormatArg[]){{.s = st->name}});
    }
}

static void emitMember(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->member.target);
    if (node->member.target && isIrPointerType(node->member.target->type))
        format(&ctx->state, "->", NULL);
    else
        format(&ctx->state, ".", NULL);
    if (node->member.name)
        format(&ctx->state, "{s}", (FormatArg[]){{.s = node->member.name}});
    else
        format(&ctx->state, "_{u32}", (FormatArg[]){{.u32 = node->member.index}});
}

static void emitIndex(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->index.target);
    format(&ctx->state, "[", NULL);
    irConstVisit(visitor, node->index.index);
    format(&ctx->state, "]", NULL);
}

static void emitCast(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->type && node->type->tag == irTypeFunc) {
        format(&ctx->state, "((", NULL);
        emitCTypeName(&ctx->state, node->type->func.ret);
        format(&ctx->state, " (*)(", NULL);
        for (u64 i = 0; i < node->type->func.count; i++) {
            if (i > 0)
                format(&ctx->state, ", ", NULL);
            emitCTypeName(&ctx->state, node->type->func.params[i]);
        }
        if (node->type->func.count == 0)
            format(&ctx->state, "void", NULL);
        format(&ctx->state, "))(", NULL);
        irConstVisit(visitor, node->cast.expr);
        format(&ctx->state, "))", NULL);
        return;
    }
    // `null` coerced to a struct is the zero value of that struct
    if (node->cast.expr && node->cast.expr->tag == irNullConst && node->type &&
        (node->type->tag == irTypeStruct ||
         node->type->tag == irTypeUntaggedUnion) &&
        node->type->category != irTypeCatClass) {
        format(&ctx->state, "(", NULL);
        emitCTypeName(&ctx->state, node->type);
        format(&ctx->state,
               node->type->strct.count ? "){{0}" : "){{}",
               NULL);
        return;
    }
    format(&ctx->state, "((", NULL);
    emitCTypeName(&ctx->state, node->type);
    format(&ctx->state, ")(", NULL);
    irConstVisit(visitor, node->cast.expr);
    format(&ctx->state, "))", NULL);
}

static void emitUnary(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->unary.op == opPostInc || node->unary.op == opPostDec) {
        format(&ctx->state, "((", NULL);
        irConstVisit(visitor, node->unary.operand);
        format(&ctx->state, "){s})", (FormatArg[]){{.s = getUnaryOpString(node->unary.op)}});
    }
    else {
        format(&ctx->state, "({s}(", (FormatArg[]){{.s = getUnaryOpString(node->unary.op)}});
        irConstVisit(visitor, node->unary.operand);
        format(&ctx->state, "))", NULL);
    }
}

static void emitBinary(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(", NULL);
    irConstVisit(visitor, node->binary.lhs);
    format(&ctx->state, " {s} ", (FormatArg[]){{.s = getBinaryOpString(node->binary.op)}});
    irConstVisit(visitor, node->binary.rhs);
    format(&ctx->state, ")", NULL);
}

// Container storage allocators: the trace builds call `<name>_trace(site, ...)`
static cstring bufferAllocatorName(const CodegenContext *ctx, const IrNode *callee)
{
    if (!ctx->memTraceEnabled || callee == NULL || callee->tag != irGlobalRef)
        return NULL;
    cstring name = callee->globalRef.name;
    if (name == NULL)
        return NULL;
    if (strcmp(name, "__alloc_buffer") == 0 || strcmp(name, "__calloc_buffer") == 0 ||
        strcmp(name, "__realloc_buffer") == 0)
        return name;
    return NULL;
}

static void emitCall(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    const IrNode *callee = node->call.callee;
    cstring buffer = bufferAllocatorName(ctx, callee);
    if (buffer != NULL) {
        // `ptr, bytes` becomes `ptr, bytes, site`: the site goes last here
        format(&ctx->state, "{s}_trace(", (FormatArg[]){{.s = buffer}});
        for (const IrNode *arg = node->call.args; arg; arg = arg->next) {
            irConstVisit(visitor, arg);
            format(&ctx->state, ", ", NULL);
        }
        emitSiteArg(ctx, &node->loc);
        format(&ctx->state, ")", NULL);
        return;
    }

    irConstVisit(visitor, node->call.callee);
    format(&ctx->state, "(", NULL);
    if (calleeHasCallSite(ctx, callee)) {
        emitSiteArg(ctx, &node->loc);
        if (node->call.args)
            format(&ctx->state, ", ", NULL);
    }
    for (const IrNode *arg = node->call.args; arg; arg = arg->next) {
        irConstVisit(visitor, arg);
        if (arg->next)
            format(&ctx->state, ", ", NULL);
    }
    format(&ctx->state, ")", NULL);
}

static void emitAddrOf(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "&", NULL);
    irConstVisit(visitor, node->addrOf.target);
}

static void emitDeref(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(*(", NULL);
    irConstVisit(visitor, node->deref.target);
    format(&ctx->state, "))", NULL);
}

static void emitMove(ConstIrVisitor *visitor, const IrNode *node)
{
    irConstVisit(visitor, node->move.src);
}

static void emitAlloca(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    const IrType *type = node->alloca.elemType;
    const IrType *st = isIrPointerType(type) ? type->pointer.pointee : type;
    format(&ctx->state, "alloca(sizeof(", NULL);
    emitCTypeName(&ctx->state, st);
    format(&ctx->state, ")", NULL);
    if (node->alloca.size) {
        format(&ctx->state, " * (", NULL);
        irConstVisit(visitor, node->alloca.size);
        format(&ctx->state, ")", NULL);
    }
    format(&ctx->state, ")", NULL);
}

static void emitMalloc(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    const IrType *type = node->malloc.elemType;
    const IrType *st = isIrPointerType(type) ? type->pointer.pointee : type;
    format(&ctx->state, "malloc(sizeof(", NULL);
    emitCTypeName(&ctx->state, st);
    format(&ctx->state, ")", NULL);
    if (node->malloc.size) {
        format(&ctx->state, " * (", NULL);
        irConstVisit(visitor, node->malloc.size);
        format(&ctx->state, ")", NULL);
    }
    format(&ctx->state, ")", NULL);
}

static inline bool isSizedInlineArray(const IrType *type)
{
    return type && type->tag == irTypeArray && type->name == NULL &&
           type->array.len != UINT64_MAX;
}

static void emitSizeof(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    const IrType *type = node->sizeofVal.targetType;
    format(&ctx->state, "sizeof(", NULL);
    // An array must keep its extent, `elem *` would be the size of a pointer
    const IrType *base = type;
    while (isSizedInlineArray(base))
        base = base->array.elem;
    emitCTypeName(&ctx->state, base);
    for (const IrType *t = type; isSizedInlineArray(t); t = t->array.elem)
        format(&ctx->state, "[{u64}]", (FormatArg[]){{.u64 = t->array.len}});
    format(&ctx->state, ")", NULL);
}

static void emitAlias(ConstIrVisitor *visitor, const IrNode *node)
{
    irConstVisit(visitor, node->alias.expr);
}

static void emitDrop(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    IrNode *target = node->drop.target;
    if (target == NULL || target->type == NULL)
        return;

    const IrType *type = target->type;
    if (node->drop.flag) {
        format(&ctx->state, "  if (", NULL);
        irConstVisit(visitor, node->drop.flag);
        format(&ctx->state, ") ", NULL);
    }
    else {
        format(&ctx->state, "  ", NULL);
    }

    switch (type->category) {
    case irTypeCatClass: {
        cstring dropFunc = ctx->memTraceEnabled ? "__smart_ptr_drop_trace" : "__smart_ptr_drop";
        format(&ctx->state, "{s}(", (FormatArg[]){{.s = dropFunc}});
        irConstVisit(visitor, target);
        if (ctx->memTraceEnabled) {
            format(&ctx->state, ", \"{s}\", {u64});\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
        }
        else {
            format(&ctx->state, ");\n", NULL);
        }
        break;
    }
    case irTypeCatResult: {
        cstring dropFunc = ctx->memTraceEnabled ? "__smart_ptr_drop_trace" : "__smart_ptr_drop";
        const IrType *valType = type->strct.count > 1 ? type->strct.fields[1].type : NULL;
        format(&ctx->state, "  if (", NULL);
        irConstVisit(visitor, target);
        format(&ctx->state, isIrPointerType(target->type) ? "->tag == 1) {\n    " : ".tag == 1) {\n    ", NULL);
        format(&ctx->state, "{s}(", (FormatArg[]){{.s = dropFunc}});
        irConstVisit(visitor, target);
        if (valType && valType->tag == irTypeUntaggedUnion) {
            format(&ctx->state, isIrPointerType(target->type) ? "->val._1" : ".val._1", NULL);
        } else {
            format(&ctx->state, isIrPointerType(target->type) ? "->val" : ".val", NULL);
        }
        if (ctx->memTraceEnabled) {
            format(&ctx->state, ", \"{s}\", {u64});\n  }\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
        }
        else {
            format(&ctx->state, ");\n  }\n", NULL);
        }

        if (valType && valType->tag == irTypeUntaggedUnion && valType->strct.count > 0) {
            const IrType *elemType = valType->strct.fields[0].type;
            if (elemType && isIrTypeDestructible(elemType)) {
                format(&ctx->state, "  else if (", NULL);
                irConstVisit(visitor, target);
                format(&ctx->state, isIrPointerType(target->type) ? "->tag == 0) {\n    " : ".tag == 0) {\n    ", NULL);
                if (elemType->category == irTypeCatClass) {
                    format(&ctx->state, "{s}(", (FormatArg[]){{.s = dropFunc}});
                    irConstVisit(visitor, target);
                    format(&ctx->state, isIrPointerType(target->type) ? "->val._0" : ".val._0", NULL);
                    if (ctx->memTraceEnabled) {
                        format(&ctx->state, ", \"{s}\", {u64});\n  }\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
                    }
                    else {
                        format(&ctx->state, ");\n  }\n", NULL);
                    }
                }
                else if (elemType->name) {
                    format(&ctx->state, "{s}_op__destructor(&(", (FormatArg[]){{.s = elemType->name}});
                    irConstVisit(visitor, target);
                    format(&ctx->state, isIrPointerType(target->type) ? "->val._0));\n  }\n" : ".val._0));\n  }\n", NULL);
                }
                else {
                    format(&ctx->state, "}\n", NULL);
                }
            }
        }
        break;
    }
    case irTypeCatOptional: {
        const IrType *elemType = type->strct.count > 1 ? type->strct.fields[1].type : NULL;
        if (elemType && isIrTypeDestructible(elemType)) {
            format(&ctx->state, "  if (", NULL);
            irConstVisit(visitor, target);
            if (isIrPointerType(target->type))
                format(&ctx->state, "->ok) ", NULL);
            else
                format(&ctx->state, ".ok) ", NULL);

            if (elemType->category == irTypeCatClass) {
                cstring dropFunc = ctx->memTraceEnabled ? "__smart_ptr_drop_trace" : "__smart_ptr_drop";
                format(&ctx->state, "{s}(", (FormatArg[]){{.s = dropFunc}});
                irConstVisit(visitor, target);
                if (isIrPointerType(target->type))
                    format(&ctx->state, "->val", NULL);
                else
                    format(&ctx->state, ".val", NULL);
                if (ctx->memTraceEnabled) {
                    format(&ctx->state, ", \"{s}\", {u64});\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
                }
                else {
                    format(&ctx->state, ");\n", NULL);
                }
            }
            else if (elemType->category == irTypeCatTuple || elemType->category == irTypeCatUnion || elemType->category == irTypeCatStruct) {
                if (elemType->strct.dctor) {
                    format(&ctx->state, "{s}(&(", (FormatArg[]){{.s = elemType->strct.dctor->function.name}});
                    irConstVisit(visitor, target);
                    if (isIrPointerType(target->type))
                        format(&ctx->state, "->val));\n", NULL);
                    else
                        format(&ctx->state, ".val));\n", NULL);
                }
            }
        }
        break;
    }
    case irTypeCatTuple:
    case irTypeCatUnion:
    case irTypeCatStruct:
        if (type->strct.dctor) {
            format(&ctx->state, "{s}(&(", (FormatArg[]){{.s = type->strct.dctor->function.name}});
            irConstVisit(visitor, target);
            format(&ctx->state, "));\n", NULL);
        }
        break;
    case irTypeCatArray:
        if (type->array.dctor) {
            format(&ctx->state, "{s}(({s}){{ .ptr = ", (FormatArg[]){{.s = type->array.dctor->function.name}, {.s = type->array.dctor->function.params->type->name}});
            irConstVisit(visitor, target);
            format(&ctx->state, ", .len = {u64} });\n", (FormatArg[]){{.u64 = type->array.len}});
        }
        break;
    case irTypeCatClosure: {
        format(&ctx->state, "if (", NULL);
        irConstVisit(visitor, target);
        if (isIrPointerType(target->type))
            format(&ctx->state, "->dtor) ", NULL);
        else
            format(&ctx->state, ".dtor) ", NULL);
        irConstVisit(visitor, target);
        if (isIrPointerType(target->type))
            format(&ctx->state, "->dtor(", NULL);
        else
            format(&ctx->state, ".dtor(", NULL);
        irConstVisit(visitor, target);
        if (isIrPointerType(target->type))
            format(&ctx->state, "->env);\n", NULL);
        else
            format(&ctx->state, ".env);\n", NULL);
        break;
    }
    default:
        if (type->tag == irTypePointer && type->category == irTypeCatClass) {
            cstring dropFunc = ctx->memTraceEnabled ? "__smart_ptr_drop_trace" : "__smart_ptr_drop";
            format(&ctx->state, "{s}(", (FormatArg[]){{.s = dropFunc}});
            irConstVisit(visitor, target);
            if (ctx->memTraceEnabled) {
                format(&ctx->state, ", \"{s}\", {u64});\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
            }
            else {
                format(&ctx->state, ");\n", NULL);
            }
        }
        break;
    }
}

static void emitCopy(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    IrNode *dst = node->copy.dst;
    IrNode *src = node->copy.src;
    if (src == NULL || dst == NULL || src->type == NULL)
        return;

    const IrType *type = src->type;
    while (type && (type->category == irTypeCatReference || (type->tag == irTypePointer && type->category != irTypeCatClass))) {
        type = type->pointer.pointee;
    }
    switch (type->category) {
    case irTypeCatClass: {
        cstring getFunc = ctx->memTraceEnabled ? "__smart_ptr_get_trace" : "__smart_ptr_get";
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, dst);
        format(&ctx->state, " = {s}(", (FormatArg[]){{.s = getFunc}});
        irConstVisit(visitor, src);
        if (ctx->memTraceEnabled) {
            format(&ctx->state, ", \"{s}\", {u64});\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
        }
        else {
            format(&ctx->state, ");\n", NULL);
        }
        break;
    }
    case irTypeCatResult: {
        cstring getFunc = ctx->memTraceEnabled ? "__smart_ptr_get_trace" : "__smart_ptr_get";
        const IrType *valType = type->strct.count > 1 ? type->strct.fields[1].type : NULL;
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, dst);
        format(&ctx->state, ".tag = ", NULL);
        irConstVisit(visitor, src);
        format(&ctx->state, ".tag;\n  if (", NULL);
        irConstVisit(visitor, src);
        format(&ctx->state, ".tag == 1) {\n    ", NULL);
        irConstVisit(visitor, dst);
        if (valType && valType->tag == irTypeUntaggedUnion) {
            format(&ctx->state, ".val._1 = {s}(", (FormatArg[]){{.s = getFunc}});
            irConstVisit(visitor, src);
            format(&ctx->state, ".val._1", NULL);
        } else {
            format(&ctx->state, ".val = {s}(", (FormatArg[]){{.s = getFunc}});
            irConstVisit(visitor, src);
            format(&ctx->state, ".val", NULL);
        }
        if (ctx->memTraceEnabled) {
            format(&ctx->state, ", \"{s}\", {u64});\n  }\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
        } else {
            format(&ctx->state, ");\n  }\n", NULL);
        }

        if (valType && valType->tag == irTypeUntaggedUnion && valType->strct.count > 0) {
            const IrType *elemType = valType->strct.fields[0].type;
            if (elemType && isIrTypeDestructible(elemType)) {
                format(&ctx->state, "  else if (", NULL);
                irConstVisit(visitor, src);
                format(&ctx->state, ".tag == 0) {\n    ", NULL);
                if (elemType->category == irTypeCatClass) {
                    irConstVisit(visitor, dst);
                    format(&ctx->state, ".val._0 = {s}(", (FormatArg[]){{.s = getFunc}});
                    irConstVisit(visitor, src);
                    format(&ctx->state, ".val._0", NULL);
                    if (ctx->memTraceEnabled) {
                        format(&ctx->state, ", \"{s}\", {u64});\n  }\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
                    } else {
                        format(&ctx->state, ");\n  }\n", NULL);
                    }
                } else if (elemType->name) {
                    format(&ctx->state, "{s}_op__copy(&(", (FormatArg[]){{.s = elemType->name}});
                    irConstVisit(visitor, dst);
                    format(&ctx->state, ".val._0), &(", NULL);
                    irConstVisit(visitor, src);
                    format(&ctx->state, ".val._0));\n  }\n", NULL);
                } else {
                    format(&ctx->state, "}\n", NULL);
                }
            } else {
                format(&ctx->state, "  else {\n    ", NULL);
                irConstVisit(visitor, dst);
                format(&ctx->state, ".val._0 = ", NULL);
                irConstVisit(visitor, src);
                format(&ctx->state, ".val._0;\n  }\n", NULL);
            }
        }
        break;
    }
    case irTypeCatOptional: {
        const IrType *elemType = type->strct.count > 1 ? type->strct.fields[1].type : NULL;
        if (elemType && isIrTypeDestructible(elemType)) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            format(&ctx->state, ".ok = ", NULL);
            irConstVisit(visitor, src);
            format(&ctx->state, ".ok;\n", NULL);
            format(&ctx->state, "  if (", NULL);
            irConstVisit(visitor, src);
            format(&ctx->state, ".ok) ", NULL);

            if (elemType->category == irTypeCatClass) {
                cstring getFunc = ctx->memTraceEnabled ? "__smart_ptr_get_trace" : "__smart_ptr_get";
                irConstVisit(visitor, dst);
                format(&ctx->state, ".val = {s}(", (FormatArg[]){{.s = getFunc}});
                irConstVisit(visitor, src);
                if (ctx->memTraceEnabled) {
                    format(&ctx->state, ".val, \"{s}\", {u64});\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
                }
                else {
                    format(&ctx->state, ".val);\n", NULL);
                }
            }
            else if (elemType->category == irTypeCatTuple || elemType->category == irTypeCatUnion || elemType->category == irTypeCatStruct) {
                if (elemType->strct.copy) {
                    irConstVisit(visitor, dst);
                    format(&ctx->state, ".val = {s}(&(", (FormatArg[]){{.s = elemType->strct.copy->function.name}});
                    irConstVisit(visitor, src);
                    format(&ctx->state, ".val));\n", NULL);
                }
                else if (elemType->category == irTypeCatStruct && elemType->name) {
                    irConstVisit(visitor, dst);
                    format(&ctx->state, ".val = {s}_op__copy(&(", (FormatArg[]){{.s = elemType->name}});
                    irConstVisit(visitor, src);
                    format(&ctx->state, ".val));\n", NULL);
                }
            }
        }
        else {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            format(&ctx->state, " = ", NULL);
            irConstVisit(visitor, src);
            format(&ctx->state, ";\n", NULL);
        }
        break;
    }
    case irTypeCatTuple:
    case irTypeCatUnion:
    case irTypeCatStruct:
        if (type->strct.copy) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            if (src->type->category == irTypeCatReference || (src->type->tag == irTypePointer && src->type->category != irTypeCatClass)) {
                format(&ctx->state, " = {s}(", (FormatArg[]){{.s = type->strct.copy->function.name}});
                irConstVisit(visitor, src);
                format(&ctx->state, ");\n", NULL);
            }
            else {
                format(&ctx->state, " = {s}(&(", (FormatArg[]){{.s = type->strct.copy->function.name}});
                irConstVisit(visitor, src);
                format(&ctx->state, "));\n", NULL);
            }
        }
        else {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            format(&ctx->state, " = ", NULL);
            if (src->type->category == irTypeCatReference || (src->type->tag == irTypePointer && src->type->category != irTypeCatClass)) {
                format(&ctx->state, "*(", NULL);
                irConstVisit(visitor, src);
                format(&ctx->state, ");\n", NULL);
            }
            else {
                irConstVisit(visitor, src);
                format(&ctx->state, ";\n", NULL);
            }
        }
        break;
    case irTypeCatClosure: {
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, dst);
        format(&ctx->state, " = ", NULL);
        irConstVisit(visitor, src);
        format(&ctx->state, ";\n", NULL);
        format(&ctx->state, "  if (", NULL);
        irConstVisit(visitor, src);
        format(&ctx->state, ".dtor) __smart_ptr_get(", NULL);
        irConstVisit(visitor, src);
        format(&ctx->state, ".env);\n", NULL);
        break;
    }
    default:
        if (type->tag == irTypePointer && type->category == irTypeCatClass) {
            cstring getFunc = ctx->memTraceEnabled ? "__smart_ptr_get_trace" : "__smart_ptr_get";
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            format(&ctx->state, " = {s}(", (FormatArg[]){{.s = getFunc}});
            irConstVisit(visitor, src);
            if (ctx->memTraceEnabled) {
                format(&ctx->state, ", \"{s}\", {u64});\n", (FormatArg[]){{.s = node->loc.fileName ?: ""}, {.u64 = node->loc.begin.row}});
            }
            else {
                format(&ctx->state, ");\n", NULL);
            }
        }
        else {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            format(&ctx->state, " = ", NULL);
            irConstVisit(visitor, src);
            format(&ctx->state, ";\n", NULL);
        }
        break;
    }
}

static void emitZeromem(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    IrNode *target = node->zeromem.target;
    if (target == NULL || target->type == NULL)
        return;

    if (target->type->tag == irTypePointer) {
        format(&ctx->state, "  memset(", NULL);
        irConstVisit(visitor, target);
        format(&ctx->state, ", 0, sizeof(", NULL);
        emitCTypeName(&ctx->state, target->type->pointer.pointee);
        format(&ctx->state, "));\n", NULL);
    }
    else {
        format(&ctx->state, "  memset(&(", NULL);
        irConstVisit(visitor, target);
        format(&ctx->state, "), 0, sizeof(", NULL);
        emitCTypeName(&ctx->state, target->type);
        format(&ctx->state, "));\n", NULL);
    }
}

static void emitOptionalValue(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(", NULL);
    emitCTypeName(&ctx->state, node->type);
    if (node->optionalValue.hasValue) {
        format(&ctx->state, "){{ .ok = true, .val = ", NULL);
        irConstVisit(visitor, node->optionalValue.value);
        format(&ctx->state, " }", NULL);
    }
    else {
        format(&ctx->state, "){{ .ok = false }", NULL);
    }
}

static void emitUnionValue(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(", NULL);
    emitCTypeName(&ctx->state, node->type);
    format(&ctx->state, "){{ .tag = {u32}", (FormatArg[]){{.u32 = node->unionValue.index}});
    if (node->unionValue.value) {
        format(&ctx->state, ", .val = {{ ._{u32} = ", (FormatArg[]){{.u32 = node->unionValue.index}});
        irConstVisit(visitor, node->unionValue.value);
        format(&ctx->state, " }", NULL);
    }
    format(&ctx->state, " }", NULL);
}

static void emitClosureValue(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(", NULL);
    emitCTypeName(&ctx->state, node->type);
    format(&ctx->state, "){{ .env = ", NULL);
    if (node->closureValue.env)
        irConstVisit(visitor, node->closureValue.env);
    else
        format(&ctx->state, "nullptr", NULL);
    format(&ctx->state, ", .fn = ", NULL);
    irConstVisit(visitor, node->closureValue.func);
    format(&ctx->state, ", .dtor = ", NULL);
    if (node->closureValue.dtor)
        irConstVisit(visitor, node->closureValue.dtor);
    else if (node->closureValue.env && hasFlag(node->type, ReferenceMembers))
        format(&ctx->state, "__smart_ptr_drop", NULL);
    else
        format(&ctx->state, "nullptr", NULL);
    format(&ctx->state, " }", NULL);
}

static void emitVarDecl(ConstIrVisitor *visitor, const IrNode *node)
{
    (void)visitor;
    (void)node;
}

static void emitAssign(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    IrNode *lhs = node->assign.lhs;
    IrNode *rhs = node->assign.rhs;

    if (rhs && rhs->tag == irOptionalValue) {
        if (rhs->optionalValue.hasValue) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, ".ok = true;\n  ", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, ".val = ", NULL);
            irConstVisit(visitor, rhs->optionalValue.value);
            format(&ctx->state, ";\n", NULL);
        }
        else {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, ".ok = false;\n", NULL);
        }
        return;
    }

    if (rhs && rhs->tag == irUnionValue) {
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, lhs);
        format(&ctx->state, ".tag = {u32};\n", (FormatArg[]){{.u32 = rhs->unionValue.index}});
        if (rhs->unionValue.value) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, ".val._{u32} = ", (FormatArg[]){{.u32 = rhs->unionValue.index}});
            irConstVisit(visitor, rhs->unionValue.value);
            format(&ctx->state, ";\n", NULL);
        }
        return;
    }

    if (rhs && rhs->tag == irStructConst) {
        bool hasArrayField = false;
        for (const IrNode *fld = rhs->structConst.fields; fld; fld = fld->next) {
            if (fld->tag == irArrayConst) {
                hasArrayField = true;
                break;
            }
        }

        if (hasArrayField) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, " = (", NULL);
            emitCTypeName(&ctx->state, rhs->type);
            format(&ctx->state, "){{", NULL);
            u64 fi = 0;
            for (const IrNode *fld = rhs->structConst.fields; fld; fld = fld->next, fi++) {
                const IrNode *val = (fld->tag == irFieldConst) ? fld->fieldConst.value : fld;
                if (val && val->tag == irArrayConst)
                    continue;
                if (fi > 0)
                    format(&ctx->state, ", ", NULL);
                irConstVisit(visitor, fld);
            }
            format(&ctx->state, "};\n", NULL);

            fi = 0;
            for (const IrNode *fld = rhs->structConst.fields; fld; fld = fld->next, fi++) {
                const IrNode *val = (fld->tag == irFieldConst) ? fld->fieldConst.value : fld;
                if (val && val->tag == irArrayConst) {
                    cstring fldName = (fld->tag == irFieldConst)
                                          ? fld->fieldConst.name
                                          : ((rhs->type && fi < rhs->type->strct.count) ? rhs->type->strct.fields[fi].name : NULL);
                    u64 idx = 0;
                    for (const IrNode *elem = val->arrayConst.elements; elem; elem = elem->next, idx++) {
                        format(&ctx->state, "  ", NULL);
                        irConstVisit(visitor, lhs);
                        if (fldName)
                            format(&ctx->state, ".{s}[{u64}] = ", (FormatArg[]){{.s = fldName}, {.u64 = idx}});
                        else
                            format(&ctx->state, "._{u64}[{u64}] = ", (FormatArg[]){{.u64 = fi}, {.u64 = idx}});
                        irConstVisit(visitor, elem);
                        format(&ctx->state, ";\n", NULL);
                    }
                }
            }
            return;
        }
    }

    if (rhs && rhs->tag == irArrayConst) {
        // `[]` and partial literals zero the remaining elements (an element
        // being replaced is dropped later, so it must not be garbage)
        u64 given = 0;
        for (const IrNode *elem = rhs->arrayConst.elements; elem; elem = elem->next)
            given++;
        if (lhs && lhs->type && lhs->type->tag == irTypeArray &&
            lhs->type->array.len != UINT64_MAX && given < lhs->type->array.len) {
            format(&ctx->state, "  memset(", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, ", 0, sizeof(", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, "));\n", NULL);
        }
        u64 idx = 0;
        for (const IrNode *elem = rhs->arrayConst.elements; elem; elem = elem->next, idx++) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, lhs);
            format(&ctx->state, "[{u64}] = ", (FormatArg[]){{.u64 = idx}});
            irConstVisit(visitor, elem);
            format(&ctx->state, ";\n", NULL);
        }
        return;
    }

    if (lhs && lhs->type && lhs->type->tag == irTypeArray && rhs && rhs->type && rhs->type->tag == irTypeArray) {
        format(&ctx->state, "  memcpy(", NULL);
        irConstVisit(visitor, lhs);
        format(&ctx->state, ", ", NULL);
        irConstVisit(visitor, rhs);
        format(&ctx->state, ", sizeof(", NULL);
        irConstVisit(visitor, lhs);
        format(&ctx->state, "));\n", NULL);
        return;
    }

    if (rhs && rhs->tag == irNullConst && lhs && lhs->type &&
        (lhs->type->tag == irTypeStruct || lhs->type->tag == irTypeUntaggedUnion) &&
        lhs->type->category != irTypeCatClass) {
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, lhs);
        format(&ctx->state, " = (", NULL);
        emitCTypeName(&ctx->state, lhs->type);
        format(&ctx->state,
               lhs->type->strct.count ? "){{0};\n" : "){{};\n",
               NULL);
        return;
    }

    format(&ctx->state, "  ", NULL);
    irConstVisit(visitor, node->assign.lhs);
    format(&ctx->state, " = ", NULL);
    irConstVisit(visitor, node->assign.rhs);
    format(&ctx->state, ";\n", NULL);
}

static void emitBranch(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "  goto {s};\n",
           (FormatArg[]){{.s = node->branch.target ? node->branch.target->basicBlock.name : ""}});
}

static void emitBranchIf(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->branchIf.cond && node->branchIf.cond->tag == irBinary) {
        format(&ctx->state, "  if ", NULL);
        irConstVisit(visitor, node->branchIf.cond);
        format(&ctx->state, " goto {s}; else goto {s};\n",
               (FormatArg[]){
                   {.s = node->branchIf.trueTarget ? node->branchIf.trueTarget->basicBlock.name : ""},
                   {.s = node->branchIf.falseTarget ? node->branchIf.falseTarget->basicBlock.name : ""}});
    }
    else {
        format(&ctx->state, "  if (", NULL);
        irConstVisit(visitor, node->branchIf.cond);
        format(&ctx->state, ") goto {s}; else goto {s};\n",
               (FormatArg[]){
                   {.s = node->branchIf.trueTarget ? node->branchIf.trueTarget->basicBlock.name : ""},
                   {.s = node->branchIf.falseTarget ? node->branchIf.falseTarget->basicBlock.name : ""}});
    }
}

static void emitSwitch(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "  switch (", NULL);
    irConstVisit(visitor, node->switchStmt.cond);
    format(&ctx->state, ") {{\n", NULL);
    for (u64 i = 0; i < node->switchStmt.casesCount; i++) {
        format(&ctx->state, "    case ", NULL);
        irConstVisit(visitor, node->switchStmt.cases[i].valNode);
        if (node->switchStmt.cases[i].endNode) {
            format(&ctx->state, " ... ", NULL);
            irConstVisit(visitor, node->switchStmt.cases[i].endNode);
        }
        format(&ctx->state, ": goto {s};\n",
               (FormatArg[]){
                   {.s = node->switchStmt.cases[i].target ? node->switchStmt.cases[i].target->basicBlock.name : ""}});
    }
    if (node->switchStmt.defaultTarget) {
        format(&ctx->state, "    default: goto {s};\n",
               (FormatArg[]){{.s = node->switchStmt.defaultTarget->basicBlock.name}});
    }
    format(&ctx->state, "  }\n", NULL);
}

static void emitInlineAsm(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    const AstNode *ast = node->ast;
    if (node->inlineAsm.isVolatile)
        format(&ctx->state, "  __asm__ __volatile__(\"", NULL);
    else
        format(&ctx->state, "  __asm__(\"", NULL);

    cstring p = node->inlineAsm.text ?: (ast ? ast->inlineAssembly.text : "");
    while (p && *p != '\0') {
        switch (*p) {
        case '$':
            if (p[1] == '$') {
                format(&ctx->state, "$", NULL);
                p++;
            }
            else
                format(&ctx->state, "%", NULL);
            break;
        case '%':
            format(&ctx->state, "%%", NULL);
            break;
        default:
            if (p[0] == '\\' && (p[1] == 'x' || p[1] == 'X' || isdigit(p[1]))) {
                printUtf8(&ctx->state, (u8)*p, false);
            }
            else {
                uint32_t cp = decodeUtf8Char(&p);
                printUtf8(&ctx->state, cp, true);
                continue;
            }
            break;
        }
        p++;
    }
    format(&ctx->state, "\"", NULL);

    if (ast && ast->inlineAssembly.outputs) {
        AstNode *output = ast->inlineAssembly.outputs;
        format(&ctx->state, "\n  :", NULL);
        for (; output; output = output->next) {
            format(&ctx->state,
                   "\"{s}\" (",
                   (FormatArg[]){{.s = output->asmOperand.constraint}});
            if (output->asmOperand.operand && output->asmOperand.operand->codegen)
                irConstVisit(visitor, (IrNode *)output->asmOperand.operand->codegen);
            format(&ctx->state, ")", NULL);
            if (output->next)
                format(&ctx->state, ", ", NULL);
        }
    }
    else if (ast && (ast->inlineAssembly.inputs || ast->inlineAssembly.clobbers)) {
        format(&ctx->state, " :", NULL);
    }

    if (ast && ast->inlineAssembly.inputs) {
        AstNode *input = ast->inlineAssembly.inputs;
        format(&ctx->state, "\n  :", NULL);
        for (; input; input = input->next) {
            format(&ctx->state,
                   "\"{s}\" (",
                   (FormatArg[]){{.s = input->asmOperand.constraint}});
            if (input->asmOperand.operand && input->asmOperand.operand->codegen)
                irConstVisit(visitor, (IrNode *)input->asmOperand.operand->codegen);
            format(&ctx->state, ")", NULL);
            if (input->next)
                format(&ctx->state, ", ", NULL);
        }
    }
    else if (ast && ast->inlineAssembly.clobbers) {
        format(&ctx->state, " :", NULL);
    }

    if (ast && ast->inlineAssembly.clobbers) {
        AstNode *clobber = ast->inlineAssembly.clobbers;
        format(&ctx->state, "\n  :", NULL);
        for (; clobber; clobber = clobber->next) {
            if (clobber->codegen)
                irConstVisit(visitor, (IrNode *)clobber->codegen);
            else if (nodeIs(clobber, StringLit))
                format(&ctx->state, "\"{s}\"", (FormatArg[]){{.s = clobber->stringLiteral.value}});
            if (clobber->next)
                format(&ctx->state, ", ", NULL);
        }
    }
    format(&ctx->state, ");\n", NULL);
}

static void emitReturn(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "  return", NULL);
    if (node->returnStmt.value) {
        format(&ctx->state, " ", NULL);
        irConstVisit(visitor, node->returnStmt.value);
    }
    format(&ctx->state, ";\n", NULL);
}

static void emitUnreachable(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "  unreachable();\n", NULL);
}

static void emitBasicBlock(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{s}:;\n", (FormatArg[]){{.s = node->basicBlock.name}});
    for (const IrNode *stmt = node->basicBlock.first; stmt; stmt = stmt->next) {
        if (stmt->tag == irVarDecl || stmt->tag == irVarRef || isIrConst(stmt))
            continue;
        emitLineDirective(ctx, &stmt->loc);
        if (stmt->tag == irCall || stmt->tag == irUnary || stmt->tag == irBinary) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, stmt);
            format(&ctx->state, ";\n", NULL);
        }
        else {
            irConstVisit(visitor, stmt);
        }
    }
}

static void emitCVarDecl(FormatState *state, const IrType *type, cstring name)
{
    if (type && type->tag == irTypeArray) {
        emitCTypeName(state, type->array.elem);
        format(state, " {s}[{u64}];\n", (FormatArg[]){{.s = name}, {.u64 = type->array.len}});
    }
    else if (type && type->tag == irTypeFunc) {
        emitCTypeName(state, type->func.ret);
        format(state, " (*{s})(", (FormatArg[]){{.s = name}});
        for (u64 i = 0; i < type->func.count; i++) {
            if (i > 0)
                format(state, ", ", NULL);
            emitCTypeName(state, type->func.params[i]);
        }
        if (type->func.count == 0)
            format(state, "void", NULL);
        format(state, ");\n", NULL);
    }
    else {
        emitCTypeName(state, type);
        format(state, " {s};\n", (FormatArg[]){{.s = name}});
    }
}

// A parameter, function pointers need a declarator (`ret (*name)(args)`)
static void emitCParam(FormatState *state, const IrType *type, cstring name)
{
    if (type && type->tag == irTypeFunc && type->name == NULL) {
        emitCTypeName(state, type->func.ret);
        format(state, " (*{s})(", (FormatArg[]){{.s = name}});
        for (u64 i = 0; i < type->func.count; i++) {
            if (i > 0)
                format(state, ", ", NULL);
            emitCTypeName(state, type->func.params[i]);
        }
        if (type->func.count == 0)
            format(state, "void", NULL);
        format(state, ")", NULL);
        return;
    }
    emitCTypeName(state, type);
    format(state, " {s}", (FormatArg[]){{.s = name}});
}

// Only guaranteed to hold the register as an operand of an inline asm
static void emitCRegisterVarDecl(FormatState *state, const IrNode *var)
{
    format(state, "register ", NULL);
    emitCTypeName(state, var->type);
    format(state,
           " {s} asm(\"{s}\");\n",
           (FormatArg[]){{.s = var->varDecl.name}, {.s = var->varDecl.reg}});
}

static void emitExternFunctionAttrs(FormatState *state, const IrNode *func)
{
    static const struct {
        IrFuncAttr attr;
        cstring name;
    } attrs[] = {{irFuncAttrNaked, "naked"},
                 {irFuncAttrNoReturn, "noreturn"},
                 {irFuncAttrReturnsTwice, "returns_twice"}};

    bool first = true;
    for (u32 i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
        if (!(func->function.attrs & attrs[i].attr))
            continue;
        format(state,
               first ? "__attribute__(({s}" : ", {s}",
               (FormatArg[]){{.s = attrs[i].name}});
        first = false;
    }
    if (!first)
        format(state, ")) ", NULL);
}

static void emitFunctionLinkage(FormatState *state, const IrNode *func)
{
    if (hasFlag(func, ModuleInit) || (func->function.name && strstr(func->function.name, "___init")))
        format(state, "__attribute__((constructor)) static ", NULL);
    else
        format(state, "static ", NULL);
}

static void emitFunction(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (hasFlag(node, Extern))
        return;

    ctx->functionName = node->function.display ?: node->function.name;
    ctx->function = node;
    emitLineDirective(ctx, &node->loc);
    emitFunctionLinkage(&ctx->state, node);
    emitCTypeName(&ctx->state, node->type ? node->type->func.ret : NULL);
    format(&ctx->state, " {s}(", (FormatArg[]){{.s = node->function.name}});
    if (hasCallSite(ctx, node)) {
        format(&ctx->state, "const __Site *__site", NULL);
        if (node->function.params)
            format(&ctx->state, ", ", NULL);
    }
    for (const IrNode *p = node->function.params; p; p = p->next) {
        emitCParam(&ctx->state, p->type, p->funcParam.name);
        if (p->next)
            format(&ctx->state, ", ", NULL);
    }
    format(&ctx->state, ") {{\n", NULL);

    for (const IrNode *bb = node->function.firstBlock; bb; bb = bb->next) {
        for (const IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irVarDecl) {
                format(&ctx->state, "  ", NULL);
                if (stmt->varDecl.reg)
                    emitCRegisterVarDecl(&ctx->state, stmt);
                else
                    emitCVarDecl(&ctx->state, stmt->type, stmt->varDecl.name ?: "__v");
            }
        }
    }

    for (const IrNode *bb = node->function.firstBlock; bb; bb = bb->next) {
        irConstVisit(visitor, bb);
    }

    format(&ctx->state, "}\n\n", NULL);
    ctx->function = NULL;
    ctx->functionName = NULL;

    int finiKind = irModuleFiniKind(node);
    if (finiKind != 0) {
        format(&ctx->state,
               "__attribute__((constructor)) static void {s}_reg(void) {{ {s}({s}); }\n\n",
               (FormatArg[]){{.s = node->function.name},
                             {.s = finiKind == 1 ? "__cxy_register_process_fini"
                                                 : "__cxy_register_thread_fini"},
                             {.s = node->function.name}});
    }
}

static void emitGlobalVar(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->globalVar.isThreadLocal)
        format(&ctx->state, "__thread ", NULL);
    if (node->type && node->type->tag == irTypeArray) {
        emitCTypeName(&ctx->state, node->type->array.elem);
        format(&ctx->state, " {s}[{u64}]", (FormatArg[]){{.s = node->globalVar.name}, {.u64 = node->type->array.len}});
    }
    else {
        emitCTypeName(&ctx->state, node->type);
        format(&ctx->state, " {s}", (FormatArg[]){{.s = node->globalVar.name}});
    }
    // `null` is zero and globals are zero initialized, whatever the type
    if (node->globalVar.init && node->globalVar.init->tag != irNullConst) {
        format(&ctx->state, " = ", NULL);
        irConstVisit(visitor, node->globalVar.init);
    }
    format(&ctx->state, ";\n", NULL);
}

static void emitCFieldDecl(FormatState *state, const IrType *type, cstring name, u32 bits)
{
    if (type && type->tag == irTypeArray) {
        emitCTypeName(state, type->array.elem);
        format(state, " {s}[{u64}];\n", (FormatArg[]){{.s = name ?: "_a"}, {.u64 = type->array.len}});
    }
    else if (type && type->tag == irTypeFunc) {
        emitCTypeName(state, type->func.ret);
        format(state, " (*{s})(", (FormatArg[]){{.s = name ?: "_f"}});
        for (u64 i = 0; i < type->func.count; i++) {
            if (i > 0)
                format(state, ", ", NULL);
            emitCTypeName(state, type->func.params[i]);
        }
        if (type->func.count == 0)
            format(state, "void", NULL);
        format(state, ");\n", NULL);
    }
    else if (bits > 0) {
        emitCTypeName(state, type);
        format(state, " {s}: {u32};\n", (FormatArg[]){{.s = name ?: "_f"}, {.u32 = bits}});
    }
    else {
        emitCTypeName(state, type);
        format(state, " {s};\n", (FormatArg[]){{.s = name ?: "_f"}});
    }
}

static void emitTypeGraphDefinitions(FormatState *state, const DynArray *modules, MemPool *pool)
{
    IrTypeGraph *g = newIrTypeGraph(pool);
    for (size_t i = 0; i < modules->size; i++) {
        const IrModule *module = dynArrayAt(const IrNode **, modules, i);
        if (module)
            collectIrTypesFromModule(g, module);
    }
    sortIrTypeGraph(g);

    // Pass 1: Forward typedefs and defines for enums, structs, and unions
    dynArrayFor(it, const IrType *, &g->sorted) {
        const IrType *t = *it;
        if (t->category == irTypeCatEnum && t->name) {
            format(state, "typedef ", NULL);
            // Emit underlying primitive integer type
            switch (t->tag) {
            case irTypeI8: format(state, "int8_t", NULL); break;
            case irTypeI16: format(state, "int16_t", NULL); break;
            case irTypeI32: format(state, "int32_t", NULL); break;
            case irTypeI64: format(state, "int64_t", NULL); break;
            case irTypeU8: format(state, "uint8_t", NULL); break;
            case irTypeU16: format(state, "uint16_t", NULL); break;
            case irTypeU32: format(state, "uint32_t", NULL); break;
            case irTypeU64: format(state, "uint64_t", NULL); break;
            default: format(state, "int64_t", NULL); break;
            }
            format(state, " {s};\n", (FormatArg[]){{.s = t->name}});
            bool isSigned = (t->tag == irTypeI8 || t->tag == irTypeI16 || t->tag == irTypeI32 || t->tag == irTypeI64);
            for (u64 o = 0; o < t->enm.count; o++) {
                if (isSigned) {
                    format(state, "#define {s}_{s} (({s}){i64})\n",
                           (FormatArg[]){
                               {.s = t->name},
                               {.s = t->enm.options[o].name},
                               {.s = t->name},
                               {.i64 = t->enm.options[o].value}});
                }
                else {
                    format(state, "#define {s}_{s} (({s}){u64})\n",
                           (FormatArg[]){
                               {.s = t->name},
                               {.s = t->enm.options[o].name},
                               {.s = t->name},
                               {.u64 = (u64)t->enm.options[o].value}});
                }
            }
        }
        else if (t->tag == irTypeStruct && t->name) {
            format(state, "typedef struct {s} {s};\n", (FormatArg[]){{.s = t->name}, {.s = t->name}});
        }
        else if (t->tag == irTypeUntaggedUnion && t->name) {
            format(state, "typedef union {s} {s};\n", (FormatArg[]){{.s = t->name}, {.s = t->name}});
        }
    }
    format(state, "\n", NULL);

    // Pass 2: Definitions in topological order
    dynArrayFor(it, const IrType *, &g->sorted) {
        const IrType *t = *it;
        if (t->tag == irTypeStruct && t->name) {
            format(state, "struct ", NULL);
            if (t->isPacked)
                format(state, "__attribute__((packed)) ", NULL);
            if (t->align > 8)
                format(state, "__attribute__((aligned({u64}))) ", (FormatArg[]){{.u64 = t->align}});
            format(state, "{s} {{\n", (FormatArg[]){{.s = t->name}});
            for (u64 f = 0; f < t->strct.count; f++) {
                format(state, "  ", NULL);
                emitCFieldDecl(state, t->strct.fields[f].type, t->strct.fields[f].name, t->strct.fields[f].bits);
            }
            format(state, "};\n\n", NULL);
        }
        else if (t->tag == irTypeUntaggedUnion && t->name) {
            format(state, "union {s} {{\n", (FormatArg[]){{.s = t->name}});
            for (u64 f = 0; f < t->strct.count; f++) {
                format(state, "  ", NULL);
                emitCFieldDecl(state, t->strct.fields[f].type, t->strct.fields[f].name, t->strct.fields[f].bits);
            }
            format(state, "};\n\n", NULL);
        }
        else if (t->tag == irTypeArray && t->name) {
            format(state, "typedef ", NULL);
            emitCTypeName(state, t->array.elem);
            format(state, " {s}[{u64}];\n\n", (FormatArg[]){{.s = t->name}, {.u64 = t->array.len}});
        }
    }

    freeIrTypeGraph(g);
}

static void emitFunctionPrototypes(FormatState *state, const IrModule *module, bool trace)
{
    for (const IrNode *func = module->module.functions; func; func = func->next) {
        if (!hasFlag(func, Reachable))
            continue;

        if (hasFlag(func, Extern)) {
            if (func->function.isHint)
                continue;
            format(state, "extern ", NULL);
            emitExternFunctionAttrs(state, func);
            emitCTypeName(state, func->type ? func->type->func.ret : NULL);
            format(state, " {s}(", (FormatArg[]){{.s = func->function.name}});
            if (func->function.params == NULL && !hasFlag(func, Variadic)) {
                format(state, "void", NULL);
            }
            else {
                for (const IrNode *p = func->function.params; p; p = p->next) {
                    emitCTypeName(state, p->type);
                    format(state, " {s}", (FormatArg[]){{.s = p->funcParam.name}});
                    if (p->next)
                        format(state, ", ", NULL);
                }
                if (hasFlag(func, Variadic)) {
                    if (func->function.params)
                        format(state, ", ...", NULL);
                    else
                        format(state, "...", NULL);
                }
            }
            format(state, ");\n", NULL);
            continue;
        }
        emitFunctionLinkage(state, func);
        emitCTypeName(state, func->type ? func->type->func.ret : NULL);
        format(state, " {s}(", (FormatArg[]){{.s = func->function.name}});
        if (trace && (func->function.attrs & irFuncAttrCallSite)) {
            format(state, "const __Site *__site", NULL);
            if (func->function.params)
                format(state, ", ", NULL);
        }
        for (const IrNode *p = func->function.params; p; p = p->next) {
            emitCParam(state, p->type, p->funcParam.name);
            if (p->next)
                format(state, ", ", NULL);
        }
        format(state, ");\n", NULL);
    }
    format(state, "\n", NULL);
}

static void emitModule(ConstIrVisitor *visitor, const IrNode *module)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    for (const IrNode *glob = module->module.globals; glob; glob = glob->next) {
        if (hasFlag(glob, Reachable))
            irConstVisit(visitor, glob);
    }

    if (module->module.globals)
        format(&ctx->state, "\n", NULL);

    for (const IrNode *func = module->module.functions; func; func = func->next) {
        if (hasFlag(func, Reachable))
            irConstVisit(visitor, func);
    }
}

static bool emitCModule(CompilerDriver *driver, const IrModule *rootModule, FILE *file)
{
    DynArray modules;
    if (driver->finalizedModules.size > 0) {
        modules = driver->finalizedModules;
    }
    else {
        modules = newDynArray(sizeof(IrNode *));
        if (rootModule) {
            pushOnDynArrayExplicit(&modules, &rootModule, sizeof(IrNode *));
        }
    }

    CodegenContext ctx = {
        .backend = driver->backend,
        .strings = driver->strings,
        .memTraceEnabled = driver->options.withMemoryTrace,
        .moduleName = rootModule ? rootModule->module.name : NULL,
        .state = newFormatState("  ", true),
        .types = newFormatState("  ", true),
        .typeInfos = newFormatState("  ", true),
        .typeInfoTypes = newDynArray(sizeof(const IrType *)),
        .sites = newDynArray(sizeof(CodegenSite)),
        .debug = {.enabled = driver->options.debug}};

    runReachabilityAnalysis(driver);
    if (hasErrors(driver->L)) {
        freeFormatState(&ctx.state);
        freeFormatState(&ctx.types);
        freeFormatState(&ctx.typeInfos);
        freeDynArray(&ctx.typeInfoTypes);
        freeDynArray(&ctx.sites);
        if (driver->finalizedModules.size == 0)
            freeDynArray(&modules);
        return false;
    }

    ctx.callSiteFuncs = newTempHashTable(sizeof(cstring));
    if (ctx.memTraceEnabled) {
        for (size_t i = 0; i < modules.size; i++) {
            const IrModule *mod = dynArrayAt(const IrNode **, &modules, i);
            for (const IrNode *f = mod ? mod->module.functions : NULL; f; f = f->next) {
                if (!(f->function.attrs & irFuncAttrCallSite))
                    continue;
                cstring name = f->function.name;
                insertInHashTable(&ctx.callSiteFuncs,
                                  &name,
                                  hashStr(hashInit(), name),
                                  sizeof(cstring),
                                  compareFuncNames);
            }
        }
    }

    // Types across all finalized modules
    emitTypeGraphDefinitions(&ctx.types, &modules, driver->pool);
    char *typesCode = formatStateToString(&ctx.types);
    if (typesCode) {
        fputs(typesCode, file);
        free(typesCode);
    }

    // Function prototypes across all finalized modules
    for (size_t i = 0; i < modules.size; i++) {
        const IrModule *mod = dynArrayAt(const IrNode **, &modules, i);
        if (mod)
            emitFunctionPrototypes(&ctx.state, mod, ctx.memTraceEnabled);
    }

    // Prototypes go out now so the TypeInfo definitions, collected while the
    // bodies are emitted, can sit between them and the bodies
    char *protoCode = formatStateToString(&ctx.state);
    if (protoCode) {
        fputs(protoCode, file);
        free(protoCode);
    }
    freeFormatState(&ctx.state);
    ctx.state = newFormatState("  ", true);

    // clang-format off
    ConstIrVisitor visitor = makeConstIrVisitor(&ctx, {
        [irModule] = emitModule,
        [irFunction] = emitFunction,
        [irBasicBlock] = emitBasicBlock,
        [irGlobalVar] = emitGlobalVar,
        [irVarDecl] = emitVarDecl,
        [irAssign] = emitAssign,
        [irBranch] = emitBranch,
        [irBranchIf] = emitBranchIf,
        [irSwitch] = emitSwitch,
        [irInlineAsm] = emitInlineAsm,
        [irReturn] = emitReturn,
        [irUnreachable] = emitUnreachable,
        [irCall] = emitCall,
        [irNullConst] = emitNullConst,
        [irBoolConst] = emitBoolConst,
        [irCharConst] = emitCharConst,
        [irIntConst] = emitIntConst,
        [irFloatConst] = emitFloatConst,
        [irStringConst] = emitStringConst,
        [irArrayConst] = emitArrayConst,
        [irFieldConst] = emitFieldConst,
        [irStructConst] = emitStructConst,
        [irVarRef] = emitVarRef,
        [irGlobalRef] = emitGlobalRef,
        [irMember] = emitMember,
        [irIndex] = emitIndex,
        [irCast] = emitCast,
        [irUnary] = emitUnary,
        [irBinary] = emitBinary,
        [irAddrOf] = emitAddrOf,
        [irDeref] = emitDeref,
        [irAlloc] = emitAlloc,
        [irAlloca] = emitAlloca,
        [irMalloc] = emitMalloc,
        [irMove] = emitMove,
        [irCopy] = emitCopy,
        [irDrop] = emitDrop,
        [irZeromem] = emitZeromem,
        [irSizeof] = emitSizeof,
        [irAlias] = emitAlias,
        [irOptionalValue] = emitOptionalValue,
        [irUnionValue] = emitUnionValue,
        [irClosureValue] = emitClosureValue
    });
    // clang-format on

    for (size_t i = 0; i < modules.size; i++) {
        const IrModule *mod = dynArrayAt(const IrNode **, &modules, i);
        if (mod) {
            ctx.moduleName = mod->module.name;
            irConstVisit(&visitor, mod);
        }
    }

    // Main entry point wrapper or test runner wrapper
    const IrNode *mainFunc = NULL;
    const IrNode *allTestCasesGlobal = NULL;
    const IrNode *runTestsFunc = NULL;
    for (size_t i = 0; i < modules.size; i++) {
        const IrModule *mod = dynArrayAt(const IrNode **, &modules, i);
        if (mod) {
            for (const IrNode *func = mod->module.functions; func; func = func->next) {
                if (hasFlag(func, Main) || func->function.display == S_main) {
                    mainFunc = func;
                }
                if (func->function.name == S_runTests ||
                    (func->function.name && strstr(func->function.name, "runTests"))) {
                    runTestsFunc = func;
                }
            }
            for (const IrNode *glob = mod->module.globals; glob; glob = glob->next) {
                if (glob->globalVar.name == S_allTestCases ||
                    (glob->globalVar.name && strstr(glob->globalVar.name, "allTestCases"))) {
                    allTestCasesGlobal = glob;
                }
            }
        }
    }

    if (!driver->options.buildPlugin && (driver->options.cmd == cmdTest || (allTestCasesGlobal && mainFunc == NULL))) {
        format(&ctx.state, "int main(int argc, char **argv) {{\n", NULL);
        format(&ctx.state, "  __cxy_start_runtime();\n", NULL);

        cstring argsSliceName = "Slice_string";
        cstring testsSliceName = "Slice_ptr";
        if (runTestsFunc && runTestsFunc->type && runTestsFunc->type->func.count >= 3) {
            if (runTestsFunc->type->func.params[0] && runTestsFunc->type->func.params[0]->name)
                argsSliceName = runTestsFunc->type->func.params[0]->name;
            if (runTestsFunc->type->func.params[2] && runTestsFunc->type->func.params[2]->name)
                testsSliceName = runTestsFunc->type->func.params[2]->name;
        }

        format(&ctx.state,
               "  {s} _args = {{ (const char **)argv, (uint64_t)argc };\n",
               (FormatArg[]){{.s = argsSliceName}});
        cstring fileName = (rootModule && rootModule->loc.fileName)
                               ? rootModule->loc.fileName
                               : "test";
        format(&ctx.state,
               "  const char *_file = \"{s}\";\n",
               (FormatArg[]){{.s = fileName}});
        if (allTestCasesGlobal) {
            cstring allTestsVar = allTestCasesGlobal->globalVar.name;
            format(&ctx.state,
                   "  {s} _tests = {{ (void *){s}, sizeof({s}) / sizeof({s}[0]) };\n",
                   (FormatArg[]){{.s = testsSliceName},
                                 {.s = allTestsVar},
                                 {.s = allTestsVar},
                                 {.s = allTestsVar}});
        }
        else {
            format(&ctx.state,
                   "  {s} _tests = {{ nullptr, 0 };\n",
                   (FormatArg[]){{.s = testsSliceName}});
        }
        format(&ctx.state, "  return builtins_runTests(_args, _file, _tests);\n", NULL);
        format(&ctx.state, "}\n", NULL);
    }
    else if (!driver->options.buildPlugin && mainFunc) {
        bool hasArgs = mainFunc->function.paramsCount > 0;
        cstring argsPass = hasArgs ? "_args" : "";
        format(&ctx.state, "int main(int argc, char **argv) {{\n", NULL);
        if (hasArgs) {
            const IrType *sliceType = mainFunc->function.params->type;
            cstring sliceName = sliceType ? sliceType->name : "Slice_string";
            format(&ctx.state, "  __cxy_start_runtime();\n", NULL);
            format(&ctx.state,
                   "  {s} _args = {{ (const char **)argv, (uint64_t)argc };\n",
                   (FormatArg[]){{.s = sliceName}});
        }
        else {
            format(&ctx.state, "  (void)argc;\n  (void)argv;\n", NULL);
            format(&ctx.state, "  __cxy_start_runtime();\n", NULL);
        }
        if (mainFunc->type && mainFunc->type->func.ret &&
            mainFunc->type->func.ret->category == irTypeCatResult) {
            const IrType *retType = mainFunc->type->func.ret;
            format(&ctx.state,
                   "  {s} res = {s}({s});\n",
                   (FormatArg[]){{.s = retType->name},
                                 {.s = mainFunc->function.name},
                                 {.s = argsPass}});
            format(&ctx.state, "  if (res.tag == 1) {{\n", NULL);
            if (retType->strct.count > 1 && retType->strct.fields[1].type &&
                retType->strct.fields[1].type->tag == irTypeUntaggedUnion) {
                format(&ctx.state,
                       "    builtins_Exception *ex = (builtins_Exception *)res.val._1;\n",
                       NULL);
            }
            else {
                format(&ctx.state,
                       "    builtins_Exception *ex = (builtins_Exception *)res.val;\n",
                       NULL);
            }
            format(&ctx.state,
                   "    if (ex) {{\n"
                   "      builtins_panicOnUnhandledException(ex);\n"
                   "      __smart_ptr_drop(ex);\n"
                   "    }\n"
                   "    return 1;\n"
                   "  }\n",
                   NULL);
            if (retType->strct.count > 1 && retType->strct.fields[1].type &&
                retType->strct.fields[1].type->tag == irTypeUntaggedUnion) {
                const IrType *elemType =
                    retType->strct.fields[1].type->strct.fields[0].type;
                if (elemType &&
                    (elemType->tag == irTypeI32 || elemType->tag == irTypeU32 ||
                     elemType->tag == irTypeI8 || elemType->tag == irTypeU8)) {
                    format(&ctx.state, "  return (int)res.val._0;\n", NULL);
                }
                else {
                    format(&ctx.state, "  return 0;\n", NULL);
                }
            }
            else {
                format(&ctx.state, "  return 0;\n", NULL);
            }
        }
        else if (mainFunc->type && mainFunc->type->func.ret &&
                 mainFunc->type->func.ret->tag != irTypeVoid) {
            format(&ctx.state,
                   "  return {s}({s});\n",
                   (FormatArg[]){{.s = mainFunc->function.name},
                                 {.s = argsPass}});
        }
        else {
            format(&ctx.state,
                   "  {s}({s});\n  return 0;\n",
                   (FormatArg[]){{.s = mainFunc->function.name},
                                 {.s = argsPass}});
        }
        format(&ctx.state, "}\n", NULL);
    }

    if (ctx.memTraceEnabled) {
        // Read by libcxy-trace for names and per-site / per-type counters
        format(&ctx.state, "\nconst __Site *const __cxy_sites[] = {{ ", NULL);
        for (u32 i = 0; i < ctx.sites.size; i++)
            format(&ctx.state, "&__site_{u32}, ", (FormatArg[]){{.u32 = i}});
        format(&ctx.state,
               "nullptr };\nconst uint32_t __cxy_site_count = {u32};\n",
               (FormatArg[]){{.u32 = ctx.sites.size}});
        format(&ctx.state, "const __TypeInfo *const __cxy_types[] = {{ ", NULL);
        for (u32 i = 0; i < ctx.typeInfoTypes.size; i++) {
            const IrType *t = dynArrayAt(const IrType **, &ctx.typeInfoTypes, i);
            format(&ctx.state, "&__ti_{s}, ", (FormatArg[]){{.s = t->name}});
        }
        format(&ctx.state,
               "nullptr };\nconst uint32_t __cxy_type_count = {u32};\n",
               (FormatArg[]){{.u32 = ctx.typeInfoTypes.size}});
    }

    char *typeInfoCode = formatStateToString(&ctx.typeInfos);
    if (typeInfoCode) {
        fputs(typeInfoCode, file);
        fputs("\n", file);
        free(typeInfoCode);
    }
    char *code = formatStateToString(&ctx.state);
    if (code) {
        fputs(code, file);
        free(code);
    }
    freeFormatState(&ctx.state);
    freeFormatState(&ctx.types);
    freeFormatState(&ctx.typeInfos);
    freeDynArray(&ctx.typeInfoTypes);
    freeDynArray(&ctx.sites);
    freeHashTable(&ctx.callSiteFuncs);

    if (driver->finalizedModules.size == 0) {
        freeDynArray(&modules);
    }
    return true;
}

AstNode *generateCode(CompilerDriver *driver, AstNode *node)
{
    csAssert0(nodeIs(node, Metadata));
    CBackend *backend = driver->backend;
    csAssert0(backend && backend->output);

    if (node->metadata.ir == NULL) {
        node->metadata.ir = lowerToIr(driver, node->metadata.node);
    }

    if (hasErrors(driver->L)) {
        return NULL;
    }

    if (!emitCModule(driver, node->metadata.ir, backend->output))
        return NULL;
    return node;
}

void deinitCompilerBackend(CompilerDriver *driver)
{
    if (driver->backend == NULL)
        return;
    CBackend *backend = (CBackend *)driver->backend;
    driver->backend = NULL;
    if (backend->output && backend->output != stdout) {
        fwrite(CXY_EPILOGUE_SOURCE, 1, CXY_EPILOGUE_SOURCE_SIZE, backend->output);
        fclose(backend->output);
        backend->output = NULL;
    }
}

bool compilerBackendMakeExecutable(CompilerDriver *driver)
{
    CBackend *backend = (CBackend *)driver->backend;
    // compile the source file
    Options *opts = &driver->options;
    FormatState cmd = newFormatState("\t", true);
    appendString(&cmd, "clang");
    if (opts->debug)
        appendString(&cmd, " -g");
    else
        appendString(&cmd, " -DNDEBUG");
    if (opts->optimizationLevel != O0)
        format(&cmd, " -O{c}", (FormatArg[]){{.c = opts->optimizationLevel}});

    for (int i = 0; i < opts->cDefines.size; i++) {
        format(&cmd,
               " {s}",
               (FormatArg[]){
                   {.s = dynArrayAt(const char **, &opts->cDefines, i)}});
    }

    for (int i = 0; i < opts->cflags.size; i++) {
        format(
            &cmd,
            " {s}",
            (FormatArg[]){{.s = dynArrayAt(const char **, &opts->cflags, i)}});
    }

    for (int i = 0; i < opts->librarySearchPaths.size; i++) {
        format(
            &cmd,
            " -L{s}",
            (FormatArg[]){{.s = dynArrayAt(
                               const char **, &opts->librarySearchPaths, i)}});
    }

    for (int i = 0; i < opts->libraries.size; i++) {
        format(&cmd,
               " -l{s}",
               (FormatArg[]){
                   {.s = dynArrayAt(const char **, &opts->libraries, i)}});
    }

    for (int i = 0; i < opts->frameworkSearchPaths.size; i++) {
        format(&cmd,
               " -I{s}",
               (FormatArg[]){{.s = dynArrayAt(const char **,
                                              &opts->frameworkSearchPaths,
                                              i)}});
    }

    for (int i = 0; i < opts->importSearchPaths.size; i++) {
        format(
            &cmd,
            " -I{s}",
            (FormatArg[]){
                {.s = dynArrayAt(const char **, &opts->importSearchPaths, i)}});
    }

    HashtableIt it = newHashTableIt(&driver->nativeSources, sizeof(cstring));
    while (hashTableItHasNext(&it)) {
        cstring *src = hashTableItNext(&it);
        format(&cmd, " {s}", (FormatArg[]){{.s = *src}});
    }

    format(&cmd, " {s}", (FormatArg[]){{.s = backend->filename}});

    it = newHashTableIt(&driver->linkLibraries, sizeof(cstring));
    while (hashTableItHasNext(&it)) {
        cstring *lib = hashTableItNext(&it);
        format(&cmd, " -l{s}", (FormatArg[]){{.s = *lib}});
    }

    if (opts->withMemoryTrace) {
        format(&cmd, " -lcxy-trace", NULL);
    }

    appendString(&cmd, " -o ");
    getOutputPath(&cmd, driver->strings, opts, "");

    cstring command = formatStateToString(&cmd);
    freeFormatState(&cmd);

    if (backend->output) {
        fwrite(
            CXY_EPILOGUE_SOURCE, 1, CXY_EPILOGUE_SOURCE_SIZE, backend->output);
        fprintf(backend->output, "\n/* %s */\n", command);
        fclose(backend->output);
        backend->output = NULL;
    }

    int rc = system(command);
    if (rc != 0) {
        logError(driver->L,
                 builtinLoc(),
                 "compile command failed: {s}",
                 (FormatArg[]){{.s = command}});
        free((void *)command);
        return false;
    }

    free((void *)command);
    return true;
}

bool compilerBackendExecuteTestCase(CompilerDriver *driver)
{
    Options *opts = &driver->options;
    FormatState cmd = newFormatState("", true);
    getOutputPath(&cmd, driver->strings, opts, "");
    char *command = formatStateToString(&cmd);
    freeFormatState(&cmd);
    int rc = system(command);
    free(command);
    return rc == 0;
}
