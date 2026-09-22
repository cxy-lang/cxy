#include "codegen.h"
#include "typegraph.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/middle/ir/context.h"
#include "lang/middle/ir/dump.h"
#include "lang/middle/ir/lower.h"
#include "lang/middle/ir/node.h"
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
    cstring filename = NULL;
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

    FILE *f = fopen(filename, "w+");
    if (f == NULL) {
        logError(driver->L,
                 builtinLoc(),
                 "creating output file '{s}' failed: '{s}'",
                 (FormatArg[]){{.s = filename}, {.s = strerror(errno)}});
        return NULL;
    }

    CBackend *backend = callocFromMemPool(driver->pool, 1, sizeof(CBackend));
    driver->backend = backend;
    backend->output = f;
    backend->filename = filename;
    backend->testMode = options->cmd == cmdTest;

    if (driver->options.withMemoryTrace) {
        fputs("#define ENABLE_MEMORY_TRACE 1\n", f);
    }
    fwrite(CXY_PROLOGUE_SOURCE, 1, CXY_PROLOGUE_SOURCE_SIZE - 1, f);
    return backend;
}

static void emitCTypeName(FormatState *state, const IrType *type)
{
    if (type == NULL) {
        format(state, "void", NULL);
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

static void emitNullConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "nullptr", NULL);
}

static void emitBoolConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{s}", (FormatArg[]){{.s = node->boolConst.value ? "true" : "false"}});
}

static void emitCharConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "'", NULL);
    printEscapedChar(&ctx->state, (char)node->charConst.value);
    format(&ctx->state, "'", NULL);
}

static void emitIntConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (node->intConst.isNegative)
        format(&ctx->state, "(-{u128})", (FormatArg[]){{.u128 = node->intConst.value}});
    else
        format(&ctx->state, "{u128}", (FormatArg[]){{.u128 = node->intConst.value}});
}

static void emitFloatConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "{f64}", (FormatArg[]){{.f64 = node->floatConst.value}});
}

static void emitStringConst(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "\"", NULL);
    cstring p = node->stringConst.value ?: "";
    while (*p) {
        printEscapedChar(&ctx->state, *p++);
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
    format(&ctx->state, "{s}", (FormatArg[]){{.s = node->globalRef.name ?: "global"}});
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
    format(&ctx->state, "((", NULL);
    emitCTypeName(&ctx->state, node->type);
    format(&ctx->state, ")(", NULL);
    irConstVisit(visitor, node->cast.expr);
    format(&ctx->state, "))", NULL);
}

static void emitUnary(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "(%s(", (FormatArg[]){{.s = getUnaryOpString(node->unary.op)}});
    irConstVisit(visitor, node->unary.operand);
    format(&ctx->state, "))", NULL);
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

static void emitCall(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->call.callee);
    format(&ctx->state, "(", NULL);
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
    format(&ctx->state, "(&(", NULL);
    irConstVisit(visitor, node->addrOf.target);
    format(&ctx->state, "))", NULL);
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

static void emitSizeof(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "sizeof(", NULL);
    emitCTypeName(&ctx->state, node->sizeofVal.targetType);
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
    case irTypeCatClass:
        format(&ctx->state, "__smart_ptr_drop(", NULL);
        irConstVisit(visitor, target);
        format(&ctx->state, ");\n", NULL);
        break;
    case irTypeCatTuple:
        format(&ctx->state, "__tuple_dctor_{u32}(&(", (FormatArg[]){{.u32 = (unsigned)type->index}});
        irConstVisit(visitor, target);
        format(&ctx->state, "));\n", NULL);
        break;
    case irTypeCatUnion:
        format(&ctx->state, "__union_dctor_{u32}(&(", (FormatArg[]){{.u32 = (unsigned)type->index}});
        irConstVisit(visitor, target);
        format(&ctx->state, "));\n", NULL);
        break;
    case irTypeCatStruct:
        if (type->name) {
            if (ctx->moduleName && ctx->moduleName[0]) {
                format(&ctx->state, "{s}_{s}_op__destructor(&(", (FormatArg[]){{.s = ctx->moduleName}, {.s = type->name}});
            }
            else {
                format(&ctx->state, "{s}_op__destructor(&(", (FormatArg[]){{.s = type->name}});
            }
            irConstVisit(visitor, target);
            format(&ctx->state, "));\n", NULL);
        }
        break;
    default:
        if (type->tag == irTypePointer) {
            format(&ctx->state, "__smart_ptr_drop(", NULL);
            irConstVisit(visitor, target);
            format(&ctx->state, ");\n", NULL);
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
    switch (type->category) {
    case irTypeCatClass:
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, dst);
        format(&ctx->state, " = __smart_ptr_get(", NULL);
        irConstVisit(visitor, src);
        format(&ctx->state, ");\n", NULL);
        break;
    case irTypeCatTuple:
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, dst);
        format(&ctx->state, " = __tuple_copy_{u32}(&(", (FormatArg[]){{.u32 = (unsigned)type->index}});
        irConstVisit(visitor, src);
        format(&ctx->state, "));\n", NULL);
        break;
    case irTypeCatUnion:
        format(&ctx->state, "  ", NULL);
        irConstVisit(visitor, dst);
        format(&ctx->state, " = __union_copy_{u32}(&(", (FormatArg[]){{.u32 = (unsigned)type->index}});
        irConstVisit(visitor, src);
        format(&ctx->state, "));\n", NULL);
        break;
    case irTypeCatStruct:
        if (type->name) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            if (ctx->moduleName && ctx->moduleName[0]) {
                format(&ctx->state, " = {s}_{s}_op__copy(&(", (FormatArg[]){{.s = ctx->moduleName}, {.s = type->name}});
            }
            else {
                format(&ctx->state, " = {s}_op__copy(&(", (FormatArg[]){{.s = type->name}});
            }
            irConstVisit(visitor, src);
            format(&ctx->state, "));\n", NULL);
        }
        break;
    default:
        if (type->tag == irTypePointer) {
            format(&ctx->state, "  ", NULL);
            irConstVisit(visitor, dst);
            format(&ctx->state, " = __smart_ptr_get(", NULL);
            irConstVisit(visitor, src);
            format(&ctx->state, ");\n", NULL);
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

static void emitVarDecl(ConstIrVisitor *visitor, const IrNode *node)
{
    (void)visitor;
    (void)node;
}

static void emitAssign(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
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
    format(&ctx->state, "  if (", NULL);
    irConstVisit(visitor, node->branchIf.cond);
    format(&ctx->state, ") goto {s}; else goto {s};\n",
           (FormatArg[]){
               {.s = node->branchIf.trueTarget ? node->branchIf.trueTarget->basicBlock.name : ""},
               {.s = node->branchIf.falseTarget ? node->branchIf.falseTarget->basicBlock.name : ""}});
}

static void emitSwitch(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    format(&ctx->state, "  switch (", NULL);
    irConstVisit(visitor, node->switchStmt.cond);
    format(&ctx->state, ") {{\n", NULL);
    for (u64 i = 0; i < node->switchStmt.casesCount; i++) {
        format(&ctx->state, "    case {u64}: goto {s};\n",
               (FormatArg[]){
                   {.u64 = node->switchStmt.cases[i].value},
                   {.s = node->switchStmt.cases[i].target ? node->switchStmt.cases[i].target->basicBlock.name : ""}});
    }
    if (node->switchStmt.defaultTarget) {
        format(&ctx->state, "    default: goto {s};\n",
               (FormatArg[]){{.s = node->switchStmt.defaultTarget->basicBlock.name}});
    }
    format(&ctx->state, "  }\n", NULL);
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
        if (stmt->tag == irVarDecl)
            continue;
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

static void emitFunction(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    if (hasFlag(node, Extern))
        return;

    format(&ctx->state, "static ", NULL);
    emitCTypeName(&ctx->state, node->type ? node->type->func.ret : NULL);
    format(&ctx->state, " {s}(", (FormatArg[]){{.s = node->function.name}});
    for (const IrNode *p = node->function.params; p; p = p->next) {
        emitCTypeName(&ctx->state, p->type);
        format(&ctx->state, " {s}", (FormatArg[]){{.s = p->funcParam.name}});
        if (p->next)
            format(&ctx->state, ", ", NULL);
    }
    format(&ctx->state, ") {{\n", NULL);

    for (const IrNode *bb = node->function.firstBlock; bb; bb = bb->next) {
        for (const IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irVarDecl) {
                format(&ctx->state, "  ", NULL);
                emitCTypeName(&ctx->state, stmt->type);
                format(&ctx->state, " {s};\n", (FormatArg[]){{.s = stmt->varDecl.name ?: "__v"}});
            }
        }
    }

    for (const IrNode *bb = node->function.firstBlock; bb; bb = bb->next) {
        irConstVisit(visitor, bb);
    }

    format(&ctx->state, "}\n\n", NULL);
}

static void emitGlobalVar(ConstIrVisitor *visitor, const IrNode *node)
{
    CodegenContext *ctx = getConstIrVisitorContext(visitor);
    emitCTypeName(&ctx->state, node->type);
    format(&ctx->state, " {s}", (FormatArg[]){{.s = node->globalVar.name}});
    if (node->globalVar.init) {
        format(&ctx->state, " = ", NULL);
        irConstVisit(visitor, node->globalVar.init);
    }
    format(&ctx->state, ";\n", NULL);
}

static void emitCFieldDecl(FormatState *state, const IrType *type, cstring name)
{
    if (type && type->tag == irTypeFunc) {
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
    else {
        emitCTypeName(state, type);
        format(state, " {s};\n", (FormatArg[]){{.s = name ?: "_f"}});
    }
}

static void emitTypeGraphDefinitions(FormatState *state, const IrModule *module, MemPool *pool)
{
    IrTypeGraph *g = newIrTypeGraph(pool);
    buildIrTypeGraph(g, module);

    // Pass 1: Forward typedefs for all structs and unions
    dynArrayFor(it, const IrType *, &g->sorted) {
        const IrType *t = *it;
        if (t->tag == irTypeStruct && t->name) {
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
            format(state, "struct {s} {{\n", (FormatArg[]){{.s = t->name}});
            for (u64 f = 0; f < t->strct.count; f++) {
                format(state, "  ", NULL);
                emitCFieldDecl(state, t->strct.fields[f].type, t->strct.fields[f].name);
            }
            format(state, "};\n\n", NULL);
        }
        else if (t->tag == irTypeUntaggedUnion && t->name) {
            format(state, "union {s} {{\n", (FormatArg[]){{.s = t->name}});
            for (u64 f = 0; f < t->strct.count; f++) {
                format(state, "  ", NULL);
                emitCFieldDecl(state, t->strct.fields[f].type, t->strct.fields[f].name);
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

static void emitFunctionPrototypes(FormatState *state, const IrModule *module)
{
    for (const IrNode *func = module->module.functions; func; func = func->next) {
        if (hasFlag(func, Extern))
            continue;
        format(state, "static ", NULL);
        emitCTypeName(state, func->type ? func->type->func.ret : NULL);
        format(state, " {s}(", (FormatArg[]){{.s = func->function.name}});
        for (const IrNode *p = func->function.params; p; p = p->next) {
            emitCTypeName(state, p->type);
            format(state, " {s}", (FormatArg[]){{.s = p->funcParam.name}});
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
    for (const IrNode *glob = module->module.globals; glob; glob = glob->next)
        irConstVisit(visitor, glob);

    if (module->module.globals)
        format(&ctx->state, "\n", NULL);

    for (const IrNode *func = module->module.functions; func; func = func->next)
        irConstVisit(visitor, func);
}

static bool emitCModule(CompilerDriver *driver, const IrModule *module, FILE *file)
{
    CodegenContext ctx = {
        .backend = driver->backend,
        .strings = driver->strings,
        .moduleName = module ? module->module.name : NULL,
        .state = newFormatState("  ", true),
        .types = newFormatState("  ", true)};

    // Types
    emitTypeGraphDefinitions(&ctx.types, module, driver->pool);
    char *typesCode = formatStateToString(&ctx.types);
    if (typesCode) {
        fputs(typesCode, file);
        free(typesCode);
    }

    // Function prototypes
    emitFunctionPrototypes(&ctx.state, module);

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
        [irMove] = emitMove,
        [irCopy] = emitCopy,
        [irDrop] = emitDrop,
        [irZeromem] = emitZeromem,
        [irSizeof] = emitSizeof,
        [irAlias] = emitAlias
    });
    // clang-format on

    irConstVisit(&visitor, module);

    // Main entry point wrapper
    const IrNode *mainFunc = NULL;
    for (const IrNode *func = module->module.functions; func; func = func->next) {
        if (hasFlag(func, Main) || func->function.display == S_main) {
            mainFunc = func;
            break;
        }
    }

    if (!driver->options.buildPlugin && mainFunc) {
        format(&ctx.state, "int main(int argc, char **argv) {{\n", NULL);
        format(&ctx.state, "  (void)argc;\n  (void)argv;\n", NULL);
        if (mainFunc->type && mainFunc->type->func.ret && mainFunc->type->func.ret->tag != irTypeVoid) {
            format(&ctx.state, "  return {s}();\n", (FormatArg[]){{.s = mainFunc->function.name}});
        }
        else {
            format(&ctx.state, "  {s}();\n  return 0;\n", (FormatArg[]){{.s = mainFunc->function.name}});
        }
        format(&ctx.state, "}\n", NULL);
    }

    char *code = formatStateToString(&ctx.state);
    if (code) {
        fputs(code, file);
        free(code);
    }
    freeFormatState(&ctx.state);
    freeFormatState(&ctx.types);
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

    emitCModule(driver, node->metadata.ir, backend->output);
    return node;
}

void deinitCompilerBackend(CompilerDriver *driver)
{
    if (driver->backend == NULL)
        return;
    CBackend *backend = (CBackend *)driver->backend;
    driver->backend = NULL;
    if (backend->output) {
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
