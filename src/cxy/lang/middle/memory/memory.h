#pragma once

#include <stddef.h>
#include "core/array.h"
#include "core/htable.h"
#include "core/mempool.h"
#include "core/strpool.h"
#include "driver/driver.h"
#include "lang/middle/ir/node.h"
#include "lang/middle/ir/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IrVariableState {
    irVarUninitialized = 0, // Declared, no value stored
    irVarAssigned,          // Valid initialized value, must be dropped on exit
    irVarMoved,             // Ownership transferred (&&x), must not be dropped
    irVarMaybeAssigned      // Conditionally assigned/moved across branches (needs drop flag)
} IrVariableState;

typedef struct TrackedVar {
    IrNode *decl;           // The irVarDecl or irFuncParam node
    cstring name;           // Variable name
    u32 scopeId;            // Scope ID where declared
    u32 dropFlagIndex;      // Bit index in __drop_flags struct
    bool needsDropFlag;     // True if variable state ever becomes MaybeAssigned
    bool isTemporary;       // True if compiler temporary (__tN, dropped eagerly)
    const IrType *type;     // IR type of the variable
    IrVariableState state;  // Current state during traversal
} TrackedVar;

typedef struct MemScope {
    u32 scopeId;            // Scope ID
    u32 parentScopeId;      // Parent scope ID
    DynArray vars;          // DynArray of (TrackedVar *) declared in this scope
    IrNode *cleanupBlock;   // Landing pad basic block for this scope
    bool isLoop;            // True if this scope represents a loop iteration
} MemScope;

typedef struct BlockVarState {
    IrNode *block;          // The BasicBlock node
    HashTable inStates;     // IrNode* (decl) -> VariableState
    HashTable outStates;    // IrNode* (decl) -> VariableState
    bool visited;
} BlockVarState;

typedef struct MemMgmtContext {
    CompilerDriver *driver;
    MemPool *pool;
    StrPool *strings;
    IrTypeTable *types;
    IrModule *module;
    IrFunction *currentFunc;

    HashTable trackedVars;  // IrNode* (decl) -> TrackedVar*
    DynArray scopes;        // Stack of active MemScope
    HashTable blockStates;  // IrNode* (basicBlock) -> BlockVarState*
    DynArray dropFlagVars;  // TrackedVar* needing drop flags in current function
    IrNode *dropFlagsDecl;  // The __drop_flags varDecl in current function

    u32 currentScopeId;
    u32 tempCounter;
} MemMgmtContext;

bool isIrTypeDestructible(const IrType *type);
bool isIrTypeCopyable(const IrType *type);

IrModule *runMemoryManagement(CompilerDriver *driver, IrModule *module);
IrModule *runFinalize(CompilerDriver *driver, IrModule *module);

#ifdef __cplusplus
}
#endif
