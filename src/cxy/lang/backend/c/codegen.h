//
// Created by Carter Mbotho on 2026-02-11.
//

#pragma once

#include "driver/driver.h"

typedef struct TypeGraph TypeGraph;
struct IrNode;

typedef struct CBackend {
    cstring filename;
    FILE *output;
    bool testMode;
} CBackend;

// An allocation point emitted as a `const __Site` (trace builds)
typedef struct CodegenSite {
    cstring file;
    cstring function;
    u32 line, column;
} CodegenSite;

typedef struct CodegenContext {
    CBackend *backend;
    StrPool *strings;
    cstring moduleName;
    FormatState types;
    FormatState state;
    FormatState typeInfos;  // `const __TypeInfo` definitions, emitted before bodies
    DynArray typeInfoTypes; // const IrType * with a TypeInfo already emitted
    DynArray visitElemTypes; // const IrType * with a buffer element visitor emitted
    DynArray sites;         // CodegenSite, index = `__site_<index>` (trace builds)
    cstring functionName;   // function being emitted
    const struct IrNode *function;
    HashTable callSiteFuncs; // cstring names of functions with a hidden site parameter
    TypeTable *table;
    cstring loopUpdate;
    bool loopUpdateUsed;
    bool hasTestCases;
    bool memTraceEnabled;
    bool inLoop;
    // a call used as a statement, its result is discarded so it needs no check
    const struct IrNode *discardedCall;

    struct {
        bool enabled;
        cstring fileName;
        u32 line;
    } debug;
} CodegenContext;

void preCodeGen(TypeGraph *g, const AstNode *node);

