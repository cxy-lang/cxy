#include "dump.h"
#include "visitor.h"
#include "lang/frontend/operator.h"
#include <unistd.h>

typedef struct IrDumpContext {
    FormatState *state;
    CompilerDriver *driver;
} IrDumpContext;

void dumpIrType(FormatState *state, const IrType *type)
{
    if (type == NULL) {
        printKeyword(state, "void");
        return;
    }

    if (type->display) {
        if (type->tag <= irTypeOpaque)
            printKeyword(state, type->display);
        else
            format(state, "{s}", (FormatArg[]){{.s = type->display}});
        return;
    }

    switch (type->tag) {
    case irTypeVoid:
    case irTypeBool:
    case irTypeChar:
    case irTypeI8:
    case irTypeI16:
    case irTypeI32:
    case irTypeI64:
    case irTypeI128:
    case irTypeU8:
    case irTypeU16:
    case irTypeU32:
    case irTypeU64:
    case irTypeU128:
    case irTypeF32:
    case irTypeF64:
    case irTypeOpaque:
        printKeyword(state, type->name ?: "unknown");
        break;
    case irTypePointer:
        format(state, "^", NULL);
        dumpIrType(state, type->pointer.pointee);
        break;
    case irTypeArray:
        format(state, "[", NULL);
        dumpIrType(state, type->array.elem);
        if (type->array.len != UINT64_MAX)
            format(state, ", {u64}", (FormatArg[]){{.u64 = type->array.len}});
        format(state, "]", NULL);
        break;
    case irTypeStruct:
    case irTypeUntaggedUnion:
        if (type->name)
            format(state, "{s}", (FormatArg[]){{.s = type->name}});
        else
            printKeyword(state, "struct");
        break;
    case irTypeFunc:
        printKeyword(state, "func");
        format(state, "(", NULL);
        for (u64 i = 0; i < type->func.count; i++) {
            if (i > 0)
                format(state, ", ", NULL);
            dumpIrType(state, type->func.params[i]);
        }
        if (type->func.variadic)
            format(state, ", ...", NULL);
        format(state, "): ", NULL);
        dumpIrType(state, type->func.ret);
        break;
    default:
        format(state, "unknown", NULL);
        break;
    }
}

static void dumpNullConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    printKeyword(ctx->state, "null");
}

static void dumpBoolConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    printKeyword(ctx->state, node->boolConst.value ? "true" : "false");
}

static void dumpCharConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{$}'", (FormatArg[]){{.style = literalStyle}});
    printEscapedChar(ctx->state, (char)node->charConst.value);
    format(ctx->state, "'{$}", (FormatArg[]){{.style = resetStyle}});
}

static void dumpIntConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    if (node->intConst.isNegative)
        format(ctx->state, "{$}-{u128}{$}",
               (FormatArg[]){{.style = literalStyle}, {.u128 = node->intConst.value}, {.style = resetStyle}});
    else
        format(ctx->state, "{$}{u128}{$}",
               (FormatArg[]){{.style = literalStyle}, {.u128 = node->intConst.value}, {.style = resetStyle}});
}

static void dumpFloatConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{$}{f64}{$}",
           (FormatArg[]){{.style = literalStyle}, {.f64 = node->floatConst.value}, {.style = resetStyle}});
}

static void dumpStringConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{$}\"", (FormatArg[]){{.style = stringStyle}});
    cstring p = node->stringConst.value ?: "";
    while (*p) {
        printEscapedChar(ctx->state, *p++);
    }
    format(ctx->state, "\"{$}", (FormatArg[]){{.style = resetStyle}});
}

static void dumpArrayConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "[", NULL);
    u64 i = 0;
    for (const IrNode *elem = node->arrayConst.elements; elem; elem = elem->next, i++) {
        if (i > 0)
            format(ctx->state, ", ", NULL);
        irConstVisit(visitor, elem);
    }
    format(ctx->state, "]", NULL);
}

static void dumpStructConst(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    if (node->type && node->type->name) {
        format(ctx->state, "{s} ", (FormatArg[]){{.s = node->type->name}});
    }
    format(ctx->state, "{{", NULL);
    u64 i = 0;
    for (const IrNode *fld = node->structConst.fields; fld; fld = fld->next, i++) {
        if (i > 0)
            format(ctx->state, ", ", NULL);
        irConstVisit(visitor, fld);
    }
    format(ctx->state, "}", NULL);
}

static void dumpVarRef(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->varRef.name ?: "var"}});
}

static void dumpGlobalRef(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->globalRef.name ?: "global"}});
}

static void dumpMember(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->member.target);
    if (node->member.name)
        format(ctx->state, ".{s}", (FormatArg[]){{.s = node->member.name}});
    else
        format(ctx->state, ".{u32}", (FormatArg[]){{.u32 = node->member.index}});
}

static void dumpIndex(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->index.target);
    format(ctx->state, "[", NULL);
    irConstVisit(visitor, node->index.index);
    format(ctx->state, "]", NULL);
}

static void dumpCast(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "<", NULL);
    dumpIrType(ctx->state, node->type);
    format(ctx->state, ">(", NULL);
    irConstVisit(visitor, node->cast.expr);
    format(ctx->state, ")", NULL);
}

static void dumpUnary(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = getUnaryOpString(node->unary.op)}});
    irConstVisit(visitor, node->unary.operand);
}

static void dumpBinary(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->binary.lhs);
    format(ctx->state, " {s} ", (FormatArg[]){{.s = getBinaryOpString(node->binary.op)}});
    irConstVisit(visitor, node->binary.rhs);
}

static void dumpCall(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    irConstVisit(visitor, node->call.callee);
    format(ctx->state, "(", NULL);
    for (const IrNode *arg = node->call.args; arg; arg = arg->next) {
        irConstVisit(visitor, arg);
        if (arg->next)
            format(ctx->state, ", ", NULL);
    }
    format(ctx->state, ")", NULL);
}

static void dumpAddrOf(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "&", NULL);
    irConstVisit(visitor, node->addrOf.target);
}

static void dumpDeref(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "*", NULL);
    irConstVisit(visitor, node->deref.target);
}

static void dumpSizeof(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    printKeyword(ctx->state, "sizeof");
    format(ctx->state, "(", NULL);
    dumpIrType(ctx->state, node->sizeofVal.targetType);
    format(ctx->state, ")", NULL);
}

static void dumpVarDecl(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "var");
    format(ctx->state, " {s}: ", (FormatArg[]){{.s = node->varDecl.display ?: (node->varDecl.name ?: "__v")}});
    dumpIrType(ctx->state, node->type);
    format(ctx->state, "\n", NULL);
}

static void dumpAssign(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    irConstVisit(visitor, node->assign.lhs);
    format(ctx->state, " = ", NULL);
    irConstVisit(visitor, node->assign.rhs);
    format(ctx->state, "\n", NULL);
}

static void dumpScopeEnter(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "scope.enter");
    format(ctx->state, " {u32}\n", (FormatArg[]){{.u32 = node->scopeEnter.scopeId}});
}

static void dumpScopeExit(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "scope.exit");
    format(ctx->state, " {u32}\n", (FormatArg[]){{.u32 = node->scopeExit.scopeId}});
}

static void dumpBranch(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "goto");
    format(ctx->state, " {s}\n", (FormatArg[]){{.s = node->branch.target ? node->branch.target->basicBlock.name : "?"}});
}

static void dumpBranchIf(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "if");
    format(ctx->state, " ", NULL);
    irConstVisit(visitor, node->branchIf.cond);
    format(ctx->state, " ", NULL);
    printKeyword(ctx->state, "goto");
    format(ctx->state, " {s} ", (FormatArg[]){{.s = node->branchIf.trueTarget ? node->branchIf.trueTarget->basicBlock.name : "?"}});
    printKeyword(ctx->state, "else");
    format(ctx->state, " ", NULL);
    printKeyword(ctx->state, "goto");
    format(ctx->state, " {s}\n", (FormatArg[]){{.s = node->branchIf.falseTarget ? node->branchIf.falseTarget->basicBlock.name : "?"}});
}

static void dumpSwitch(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "switch");
    format(ctx->state, " ", NULL);
    irConstVisit(visitor, node->switchStmt.cond);
    format(ctx->state, " {{\n", NULL);
    for (u64 i = 0; i < node->switchStmt.casesCount; i++) {
        format(ctx->state, "    {u64} -> {s}\n",
               (FormatArg[]){
                   {.u64 = node->switchStmt.cases[i].value},
                   {.s = node->switchStmt.cases[i].target ? node->switchStmt.cases[i].target->basicBlock.name : "?"}});
    }
    if (node->switchStmt.defaultTarget) {
        format(ctx->state, "    ", NULL);
        printKeyword(ctx->state, "else");
        format(ctx->state, " -> {s}\n", (FormatArg[]){{.s = node->switchStmt.defaultTarget->basicBlock.name}});
    }
    format(ctx->state, "  }\n", NULL);
}

static void dumpReturn(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "return");
    if (node->returnStmt.value) {
        format(ctx->state, " ", NULL);
        irConstVisit(visitor, node->returnStmt.value);
    }
    format(ctx->state, "\n", NULL);
}

static void dumpUnreachable(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "unreachable");
    format(ctx->state, "\n", NULL);
}

static void dumpCopy(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "copy");
    format(ctx->state, " ", NULL);
    irConstVisit(visitor, node->copy.src);
    format(ctx->state, " -> ", NULL);
    irConstVisit(visitor, node->copy.dst);
    format(ctx->state, "\n", NULL);
}

static void dumpMove(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "&&", NULL);
    irConstVisit(visitor, node->move.src);
}

static void dumpDrop(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    if (node->drop.flag) {
        printKeyword(ctx->state, "drop-if");
        format(ctx->state, " ", NULL);
        irConstVisit(visitor, node->drop.target);
        format(ctx->state, ", ", NULL);
        irConstVisit(visitor, node->drop.flag);
    }
    else {
        printKeyword(ctx->state, "drop");
        format(ctx->state, " ", NULL);
        irConstVisit(visitor, node->drop.target);
    }
    format(ctx->state, "\n", NULL);
}

static void dumpZeromem(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "  ", NULL);
    printKeyword(ctx->state, "zeromem");
    format(ctx->state, " ", NULL);
    irConstVisit(visitor, node->zeromem.target);
    format(ctx->state, "\n", NULL);
}

static bool isStandaloneStmtTag(IrTag tag)
{
    switch (tag) {
    case irVarDecl:
    case irAssign:
    case irScopeEnter:
    case irScopeExit:
    case irBranch:
    case irBranchIf:
    case irSwitch:
    case irReturn:
    case irUnreachable:
    case irCopy:
    case irMove:
    case irDrop:
    case irZeromem:
        return true;
    default:
        return false;
    }
}

static void dumpBasicBlock(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    format(ctx->state, "{s}:\n", (FormatArg[]){{.s = node->basicBlock.name ?: "bb"}});
    for (const IrNode *stmt = node->basicBlock.first; stmt; stmt = stmt->next) {
        if (!isStandaloneStmtTag(stmt->tag)) {
            format(ctx->state, "  ", NULL);
            irConstVisit(visitor, stmt);
            format(ctx->state, "\n", NULL);
        }
        else {
            irConstVisit(visitor, stmt);
        }
    }
}

static void dumpFunction(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    printKeyword(ctx->state, "func");
    format(ctx->state, " {s}(", (FormatArg[]){{.s = node->function.display ?: (node->function.name ?: "fn")}});
    for (const IrNode *param = node->function.params; param; param = param->next) {
        format(ctx->state, "{s}: ", (FormatArg[]){{.s = param->funcParam.name ?: "p"}});
        dumpIrType(ctx->state, param->type);
        if (param->next)
            format(ctx->state, ", ", NULL);
    }
    format(ctx->state, "): ", NULL);
    dumpIrType(ctx->state, node->type ? node->type->func.ret : NULL);
    format(ctx->state, " {{\n", NULL);

    for (const IrNode *block = node->function.firstBlock; block; block = block->next)
        irConstVisit(visitor, block);

    format(ctx->state, "}\n\n", NULL);
}

static void dumpGlobalVar(ConstIrVisitor *visitor, const IrNode *node)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);
    printKeyword(ctx->state, "global");
    format(ctx->state, " {s}: ", (FormatArg[]){{.s = node->globalVar.name}});
    dumpIrType(ctx->state, node->type);
    if (node->globalVar.init) {
        format(ctx->state, " = ", NULL);
        irConstVisit(visitor, node->globalVar.init);
    }
    format(ctx->state, "\n", NULL);
}

static void dumpModule(ConstIrVisitor *visitor, const IrNode *module)
{
    IrDumpContext *ctx = getConstIrVisitorContext(visitor);

    format(ctx->state, "\n// Generate IR for module {s}\n", (FormatArg[]){{.s = module->loc.fileName}});
    for (const IrNode *glob = module->module.globals; glob; glob = glob->next)
        irConstVisit(visitor, glob);

    if (module->module.globals)
        format(ctx->state, "\n", NULL);

    for (const IrNode *func = module->module.functions; func; func = func->next)
        irConstVisit(visitor, func);
}

AstNode *dumpIr(CompilerDriver *driver, const IrNode *module, FILE *file)
{
    FormatState state = newFormatState("  ", !isatty(fileno(file)));
    IrDumpContext context = {.state = &state, .driver = driver};

    // clang-format off
    ConstIrVisitor visitor = makeConstIrVisitor(&context, {
        [irModule] = dumpModule,
        [irFunction] = dumpFunction,
        [irBasicBlock] = dumpBasicBlock,
        [irGlobalVar] = dumpGlobalVar,
        [irVarDecl] = dumpVarDecl,
        [irAssign] = dumpAssign,
        [irScopeEnter] = dumpScopeEnter,
        [irScopeExit] = dumpScopeExit,
        [irBranch] = dumpBranch,
        [irBranchIf] = dumpBranchIf,
        [irSwitch] = dumpSwitch,
        [irReturn] = dumpReturn,
        [irUnreachable] = dumpUnreachable,
        [irCopy] = dumpCopy,
        [irMove] = dumpMove,
        [irDrop] = dumpDrop,
        [irZeromem] = dumpZeromem,
        [irCall] = dumpCall,
        [irNullConst] = dumpNullConst,
        [irBoolConst] = dumpBoolConst,
        [irCharConst] = dumpCharConst,
        [irIntConst] = dumpIntConst,
        [irFloatConst] = dumpFloatConst,
        [irStringConst] = dumpStringConst,
        [irArrayConst] = dumpArrayConst,
        [irStructConst] = dumpStructConst,
        [irVarRef] = dumpVarRef,
        [irGlobalRef] = dumpGlobalRef,
        [irMember] = dumpMember,
        [irIndex] = dumpIndex,
        [irCast] = dumpCast,
        [irUnary] = dumpUnary,
        [irBinary] = dumpBinary,
        [irAddrOf] = dumpAddrOf,
        [irDeref] = dumpDeref,
        [irSizeof] = dumpSizeof
    });
    // clang-format on

    irConstVisit(&visitor, module);

    char *str = formatStateToString(&state);
    if (str) {
        fputs(str, file);
        free(str);
    }
    freeFormatState(&state);
    return NULL;
}
