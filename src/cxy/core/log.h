#pragma once

#include <stdint.h>

#include "format.h"
#include "htable.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define CXY_COMPILER_WARNINGS(f)        \
    f(MissingStage,      0)             \
    f(UnusedVariable,    1)             \
    f(RedundantStmt,     2)             \
    f(CMacroRedefine,    3)             \
    f(CUnsupportedField, 4)             \
    f(MaybeUninitialized,5)             \
    f(OwnerLeak,         6)             \
    f(UseAfterMove,      7)             \
    f(DoubleDrop,        8)             \
    f(UnclearedMove,     9)             \
    f(OverwrittenOwner,  10)            \
    f(DropFlagMismatch,  11)            \
    f(RedeclaredOwner,   12)            \
    f(ReturnedBorrow,    13)            \
    f(DanglingBorrow,    14)             \
    f(MoveOfReference,   15)

// clang-format on

enum {
#define f(NAME, IDX) wrn##NAME = IDX,
    CXY_COMPILER_WARNINGS(f)
#undef f
};

#define wrnNone (0ull)
#define wrnAll (~(0ull) >> 1)
#define wrn_Error (1ull << 63)
// The ownership verifier runs only when one of these is enabled
#define wrnOwnership                                                           \
    (BIT(wrnOwnerLeak) | BIT(wrnUseAfterMove) | BIT(wrnDoubleDrop) |           \
     BIT(wrnUnclearedMove) | BIT(wrnOverwrittenOwner) |                        \
     BIT(wrnDropFlagMismatch) | BIT(wrnRedeclaredOwner) |                      \
     BIT(wrnReturnedBorrow) | BIT(wrnDanglingBorrow) |                         \
     BIT(wrnMoveOfReference))
#define wrnDefault                                                             \
    wrnAll & ~(BIT(wrnMissingStage) | BIT(wrnCMacroRedefine) |                 \
               BIT(wrnMaybeUninitialized) | wrnOwnership)

typedef u64 WarningId;

/*
 * The log object is used to
 * report messages from
 * various passes of the
 * compiler. It also caches
 * what files, to print
 * error diagnostics
 * efficiently.
 */
typedef struct {
    uint32_t row, col;
    size_t byteOffset;
} FilePos;

typedef struct FileLoc {
    const char *fileName;
    FilePos begin, end;
} FileLoc;

typedef enum { dkError, dkWarning, dkNote } DiagnosticKind;

typedef struct Diagnostic {
    DiagnosticKind kind;
    FileLoc loc;
    cstring fmt;
    const FormatArg *args;
    cstring code; // warning name, NULL for other diagnostics
} Diagnostic;

typedef void (*DiagnosticHandler)(const Diagnostic *, void *);

/*
 * A context the compiler is currently working in, e.g. a generic
 * instantiation. Frames are only formatted when an error is reported,
 * so `arg` must outlive the frame (types and strings are interned).
 */
typedef struct LogFrame {
    FileLoc loc;
    cstring fmt;
    FormatArg arg;
} LogFrame;

#define LOG_DEFAULT_MAX_FRAME_NOTES 10

typedef struct Log {
    HashTable fileCache;
    DiagnosticHandler handler;
    void *handlerCtx;
    size_t errorCount;
    size_t warningCount;
    size_t maxErrors;
    bool ignoreStyles;
    FormatState *state;
    struct {
        LogFrame *items;
        u32 count, capacity;
    } frames;
    u32 maxFrameNotes; // 0 shows every frame
    struct {
        cstring str;
        u64 num;
    } enabledWarnings;
    u64 warningsAsErrors; // warnings reported as errors
    bool showDiagnostics;
    bool progress;
} Log;

typedef struct DiagnosticMemoryPrintCtx {
    FormatState *state;
    Log *L;
} DiagnosticMemoryPrintCtx;

Log newLog(MemPool *pool, DiagnosticHandler handler, void *ctx);
void freeLog(Log *);

void logError(Log *, const FileLoc *, const char *, const FormatArg *);
void logWarning(Log *, const FileLoc *, const char *, const FormatArg *);
void logWarningWithId(
    Log *, u8, const FileLoc *, const char *, const FormatArg *);
void logNote(Log *, const FileLoc *, const char *, const FormatArg *);

// Errors reported between push and pop are followed by a note per frame
void logPushFrame(Log *, const FileLoc *, cstring fmt, FormatArg arg);
void logPopFrame(Log *);
void printStatus(Log *L, const char *fmt, ...);
void printStatusAlways(Log *L, const char *fmt, ...);
void printStatusSticky(Log *L, const char *fmt, ...);

void printDiagnosticToConsole(const Diagnostic *diag, void *ctx);
void printDiagnosticToMemory(const Diagnostic *diag, void *ctx);

static inline bool hasErrors(Log *L) { return L->errorCount > 0; }

const FileLoc *builtinLoc(void);
static inline FileLoc locSubRange(const FileLoc *start, const FileLoc *end)
{
    csAssert0(start->fileName == end->fileName);
    return (FileLoc){
        .fileName = start->fileName, .begin = start->begin, .end = end->begin};
}

static inline FileLoc *locExtend_(FileLoc *dst,
                                  const FileLoc *start,
                                  const FileLoc *end)
{
    if (start->fileName == end->fileName) {
        csAssert0(start->begin.byteOffset <= end->end.byteOffset);
        *dst = (FileLoc){.fileName = start->fileName,
                         .begin = start->begin,
                         .end = end->end};
    }
    else {
        *dst = *start;
    }
    return dst;
}

#define locExtend(start, end) locExtend_(&(FileLoc){}, (start), (end))

static inline FileLoc *locAfter(FileLoc *dst, const FileLoc *loc)
{
    *dst = (FileLoc){
        .fileName = loc->fileName, .begin = loc->end, .end = loc->end};
    return dst;
}

u64 parseWarningLevels(Log *L, cstring str);
u64 parseWarningLevelsFrom(Log *L, u64 base, cstring str);

static inline bool isWarningEnabled_(Log *L, WarningId id)
{
    return L->enabledWarnings.num & id;
}
#define isWarningEnabled(L, NAME) isWarningEnabled_((L), BIT(wrn##NAME))

#ifdef __cplusplus
}
#endif
