#include "memory.h"
#include "lang/frontend/flag.h"

static void stripScopeMarkers(IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;

        IrNode *prev = NULL;
        IrNode *stmt = bb->basicBlock.first;
        while (stmt) {
            IrNode *next = stmt->next;
            if (stmt->tag == irScopeEnter || stmt->tag == irScopeExit) {
                if (prev) {
                    prev->next = next;
                }
                else {
                    bb->basicBlock.first = next;
                }
                if (bb->basicBlock.last == stmt) {
                    bb->basicBlock.last = prev;
                }
            }
            else {
                prev = stmt;
            }
            stmt = next;
        }
    }
}

static IrNode *resolveTrampolineTarget(IrNode *target)
{
    u32 depth = 0;
    while (target && target->tag == irBasicBlock && depth++ < 32) {
        if (target->basicBlock.first &&
            target->basicBlock.first == target->basicBlock.last &&
            target->basicBlock.first->tag == irBranch) {
            IrNode *nextTarget = target->basicBlock.first->branch.target;
            if (nextTarget == target || nextTarget == NULL)
                break;
            target = nextTarget;
        }
        else {
            break;
        }
    }
    return target;
}

static void simplifyBlockBranches(IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;
        for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
            if (stmt->tag == irBranch) {
                stmt->branch.target = resolveTrampolineTarget(stmt->branch.target);
            }
            else if (stmt->tag == irBranchIf) {
                stmt->branchIf.trueTarget = resolveTrampolineTarget(stmt->branchIf.trueTarget);
                stmt->branchIf.falseTarget = resolveTrampolineTarget(stmt->branchIf.falseTarget);
            }
            else if (stmt->tag == irSwitch) {
                for (u64 i = 0; i < stmt->switchStmt.casesCount; i++) {
                    stmt->switchStmt.cases[i].target = resolveTrampolineTarget(stmt->switchStmt.cases[i].target);
                }
                if (stmt->switchStmt.defaultTarget)
                    stmt->switchStmt.defaultTarget = resolveTrampolineTarget(stmt->switchStmt.defaultTarget);
            }
        }
    }
}

static void eliminateEmptyTrampolineBlocks(IrNode *func)
{
    IrNode *prev = NULL;
    IrNode *bb = func->function.firstBlock;
    while (bb) {
        IrNode *next = bb->next;
        bool isTrampoline = (bb != func->function.firstBlock &&
                             bb->basicBlock.first &&
                             bb->basicBlock.first == bb->basicBlock.last &&
                             bb->basicBlock.first->tag == irBranch);
        if (isTrampoline) {
            if (prev)
                prev->next = next;
            else
                func->function.firstBlock = next;
            if (func->function.lastBlock == bb)
                func->function.lastBlock = prev;
        }
        else {
            prev = bb;
        }
        bb = next;
    }
}

static void verifyBlockTerminators(MemPool *pool, IrNode *func)
{
    for (IrNode *bb = func->function.firstBlock; bb; bb = bb->next) {
        if (bb->tag != irBasicBlock)
            continue;

        IrNode *last = bb->basicBlock.last;
        if (last == NULL)
            continue;

        bool hasTerminator = (last->tag == irBranch ||
                              last->tag == irBranchIf ||
                              last->tag == irSwitch ||
                              last->tag == irReturn ||
                              last->tag == irUnreachable);

        if (!hasTerminator) {
            IrNode *unreach = makeIrUnreachable(pool, &last->loc);
            irAppendBlockStmt(bb, unreach);
        }
    }
}

static bool hasFunctionDeclaration(const IrModule *module, cstring name)
{
    for (const IrNode *func = module->module.functions; func; func = func->next) {
        if (func->tag == irFunction && func->function.name == name)
            return true;
    }
    return false;
}

static void resolveReferencedDeclarations(CompilerDriver *driver, IrModule *module, const IrNode *node)
{
    if (node == NULL)
        return;

    if (node->tag == irGlobalRef && node->globalRef.target == NULL && node->type && node->type->tag == irTypeFunc) {
        cstring name = node->globalRef.name;
        if (name && !hasFunctionDeclaration(module, name)) {
            IrNode *externFunc = makeIrFunction(driver->pool, &node->loc, flgExtern, name, name, node->type, NULL, 0);
            if (module->module.functions) {
                IrNode *last = module->module.functions;
                while (last->next)
                    last = last->next;
                last->next = externFunc;
            }
            else {
                module->module.functions = externFunc;
            }
        }
    }

    if (node->tag == irCall) {
        resolveReferencedDeclarations(driver, module, node->call.callee);
        for (const IrNode *arg = node->call.args; arg; arg = arg->next) {
            resolveReferencedDeclarations(driver, module, arg);
        }
    }
}

IrModule *runFinalize(CompilerDriver *driver, IrModule *module)
{
    if (module == NULL)
        return NULL;

    for (IrNode *decl = module->module.functions; decl; decl = decl->next) {
        if (decl->tag == irFunction && decl->function.firstBlock) {
            stripScopeMarkers(decl);
            simplifyBlockBranches(decl);
            eliminateEmptyTrampolineBlocks(decl);
            verifyBlockTerminators(driver->pool, decl);

            for (IrNode *bb = decl->function.firstBlock; bb; bb = bb->next) {
                if (bb->tag != irBasicBlock)
                    continue;
                for (IrNode *stmt = bb->basicBlock.first; stmt; stmt = stmt->next) {
                    resolveReferencedDeclarations(driver, module, stmt);
                }
            }
        }
    }

    return module;
}
