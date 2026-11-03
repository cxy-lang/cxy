//
// Created by Carter on 2023-09-22.
//

#pragma once

#include "lang/frontend/ast.h"
#include "lang/frontend/visitor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Log *L;
    MemPool *pool;
    struct StrPool *strings;
    AstNodeList testCases;
    // `_task_f` wrappers of `@task` functions, appended to the enclosing
    // program or class once its members have been visited
    AstNodeList taskWrappers;
    AstNode *testSetup;
    AstNode *testCleanup;
    AstModifier block;
    AstModifier root;
    u64 lastTestId;
    bool exceptionTrace;
    // f-string that is only the receiver of a member access (borrowed, not moved)
    AstNode *borrowedStringExpr;
    union {
        struct {
            bool inClassOrStruct;
            bool inFunction;
            AstNode *currentClsOrStruct;
            AstNode *catchLhs;
            AstNode *catchRhs;
            cstring fun;
            cstring cls;
            cstring mod;
            cstring path;
        };
        struct {
            bool inClassOrStruct;
            bool inFunction;
            AstNode *currentClsOrStruct;
            AstNode *catchLhs;
            AstNode *catchRhs;
            cstring fun;
            cstring cls;
            cstring mod;
            cstring path;
        } stack;
    };
} ShakeAstContext;

AstNode *createClassOrStructBuiltins(MemPool *pool, AstNode *node);

void shakeClosureExpr(struct AstVisitor *visitor, AstNode *node);

#ifdef __cplusplus
}
#endif
