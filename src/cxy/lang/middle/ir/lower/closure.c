#include "internal.h"

#include "core/alloc.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lowerClosureValue(AstVisitor *visitor, AstNode *node)
{
    LowerContext *ctx = getAstVisitorContext(visitor);
    const IrType *closureType =
        lowerType(ctx, node->closureValue.canonicalType ?: node->type);

    IrNode *envVal = NULL;
    IrNode *dtorVal = NULL;
    if (node->closureValue.env && !nodeIs(node->closureValue.env, NullLit)) {
        IrNode *envStruct =
            lowerFlattenedOperand(visitor, node->closureValue.env);

        if (node->type && node->type->closure.isEscaping) {
            const IrType *envPtrType =
                makeIrPointerType(ctx->types, flgNone, irTypeCatNone, envStruct->type);
            IrNode *allocNode =
                makeIrAlloc(ctx->pool, &node->loc, envStruct->type);
            cstring tempName =
                makeStringf(ctx->strings, "__env%u", ctx->tempCounter++);
            IrNode *envVar = makeIrVarDecl(ctx->pool,
                                           &node->loc,
                                           flgNone,
                                           tempName,
                                           tempName,
                                           envPtrType,
                                           ctx->currentScopeId);
            markCompilerVar(envVar, irClassTransient);
            emitStmt(ctx, envVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, envVar),
                                  allocNode));
            emitStmt(
                ctx,
                makeIrAssign(
                    ctx->pool,
                    &node->loc,
                    makeIrDeref(ctx->pool,
                                &node->loc,
                                makeIrVarRef(ctx->pool, &node->loc, envVar),
                                envStruct->type),
                    envStruct));
            envVal = makeIrVarRef(ctx->pool, &node->loc, envVar);
        }
        else {
            const IrType *envStructType = lowerType(ctx, node->closureValue.env->type);
            cstring envVarName =
                makeStringf(ctx->strings, "__env%u", ctx->tempCounter++);
            Flags envFlags = flgNone;
            if (isIrTypeDestructible(envStructType))
                envFlags |= flgReferenceMembers;
            IrNode *envVar = makeIrVarDecl(ctx->pool,
                                           &node->loc,
                                           envFlags,
                                           envVarName,
                                           envVarName,
                                           envStructType,
                                           ctx->currentScopeId);
            emitStmt(ctx, envVar);
            emitStmt(ctx,
                     makeIrAssign(ctx->pool,
                                  &node->loc,
                                  makeIrVarRef(ctx->pool, &node->loc, envVar),
                                  envStruct));
            envVal = makeIrAddrOf(
                ctx->pool,
                &node->loc,
                makeIrVarRef(ctx->pool, &node->loc, envVar),
                makeIrPointerType(ctx->types, flgNone, irTypeCatNone, envStructType));
        }
    }

    IrNode *fnVal = lowerFlattenedOperand(visitor, node->closureValue.func);

    node->codegen = makeIrClosureValue(
        ctx->pool, &node->loc, envVal, fnVal, dtorVal, closureType);
}

