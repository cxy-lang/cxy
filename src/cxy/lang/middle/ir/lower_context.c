#include "lower_internal.h"

#include "core/hash.h"
#include "core/strpool.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "lang/frontend/strings.h"
#include "lang/middle/mangle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool compareVarNameEntries(const void *lhs, const void *rhs)
{
    const VarNameEntry *l = lhs;
    const VarNameEntry *r = rhs;
    return l->name == r->name;
}

cstring makeUniqueVarName(LowerContext *ctx, cstring displayName)
{
    if (displayName == NULL)
        return displayName;

    VarNameEntry *found = findInHashTable(&ctx->varNames,
                                          &(VarNameEntry){.name = displayName},
                                          hashPtr(hashInit(), displayName),
                                          sizeof(VarNameEntry),
                                          compareVarNameEntries);

    if (found == NULL) {
        VarNameEntry entry = {.name = displayName, .count = 1};
        insertInHashTable(&ctx->varNames,
                          &entry,
                          hashPtr(hashInit(), displayName),
                          sizeof(VarNameEntry),
                          compareVarNameEntries);
        return displayName;
    }

    u32 suffix = found->count++;
    return makeStringf(ctx->strings, "%s_%u", displayName, suffix);
}
