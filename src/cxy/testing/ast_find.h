#pragma once

#include "lang/frontend/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

// The `index`th (in source order) node below `root`, `root` included, that is a
// declaration of kind `tag` (FuncDecl, ClassDecl or VarDecl) called `name`.
const AstNode *testingFindAstDecl(const AstNode *root,
                                  AstTag tag,
                                  const char *name,
                                  int index);

#ifdef __cplusplus
}
#endif
