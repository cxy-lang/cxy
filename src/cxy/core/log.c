#include "core/log.h"
#include "core/alloc.h"
#include "core/hash.h"
#include "core/utils.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LINE_SEARCH_RANGE 64
#define LINE_BUF_CAPACITY 64

typedef struct {
    const char *fileName;
    FILE *file;
} FileEntry;

typedef struct {
    const char *name;
    size_t len;
    WarningId value;
} Warning;

static bool compareWarningIds(const void *left, const void *right)
{
    return ((Warning *)left)->len == ((Warning *)right)->len &&
           !memcmp(((Warning *)left)->name,
                   ((Warning *)right)->name,
                   ((Warning *)left)->len);
}

static void registerWarnings(HashTable *warnings)
{
#define f(name, IDX)                                                           \
    insertInHashTable(warnings,                                                \
                      &(Warning){#name, strlen(#name), (u64)1 << IDX},         \
                      hashStr(hashInit(), #name),                              \
                      sizeof(Warning),                                         \
                      compareWarningIds);
    CXY_COMPILER_WARNINGS(f)
#undef f
    insertInHashTable(warnings,
                      &(Warning){"All", 3, wrnAll},
                      hashStr(hashInit(), "All"),
                      sizeof(Warning),
                      compareWarningIds);
    insertInHashTable(warnings,
                      &(Warning){"Ownership", 9, wrnOwnership},
                      hashStr(hashInit(), "Ownership"),
                      sizeof(Warning),
                      compareWarningIds);
    insertInHashTable(warnings,
                      &(Warning){"None", 4, wrnNone},
                      hashStr(hashInit(), "None"),
                      sizeof(Warning),
                      compareWarningIds);
}

// `name` is NUL terminated and already trimmed
static WarningId parseNextWarningId(Log *L, const char *name)
{
    static bool initialized = false;
    static HashTable warningIds = {};
    if (!initialized) {
        initialized = true;
        warningIds = newTempHashTable(sizeof(Warning));
        registerWarnings(&warningIds);
    }

    u64 len = strlen(name);
    if (len == 0) {
        logError(L,
                 NULL,
                 "parsing warning failed, expecting a warning name",
                 NULL);
        return wrn_Error;
    }

    Warning *warning = findInHashTable(&warningIds,
                                       &(Warning){.name = name, .len = len},
                                       hashRawBytes(hashInit(), name, len),
                                       sizeof(Warning),
                                       compareWarningIds);

    if (warning == NULL) {
        logError(L,
                 NULL,
                 "parsing warning failed, unknown warning name ('{s}')",
                 (FormatArg[]){{.s = name}});
        return wrn_Error;
    }

    return warning->value;
}

Log newLog(MemPool *pool, DiagnosticHandler handler, void *ctx)
{
    handler = handler ?: printDiagnosticToConsole;

    Log L = (Log){.fileCache = newHashTable(sizeof(FileEntry), pool),
                  .showDiagnostics = true,
                  .handler = handler,
                  .handlerCtx = ctx,
                  .maxErrors = SIZE_MAX,
                  .maxFrameNotes = LOG_DEFAULT_MAX_FRAME_NOTES,
                  .enabledWarnings.num = wrnDefault};
    if (handler == printDiagnosticToConsole) {
        L.handlerCtx = NULL;
        L.handler = NULL;
    }
    return L;
}

void freeLog(Log *log)
{
    HashtableIt it = newHashTableIt(&log->fileCache, sizeof(FileEntry));
    while (hashTableItHasNext(&it)) {
        FileEntry *entry = hashTableItNext(&it);
        fclose(entry->file);
    }
    freeHashTable(&log->fileCache);
    free(log->frames.items);
    log->frames = (__typeof(log->frames)){};
}

static bool compareFileEntries(const void *left, const void *right)
{
    return !strcmp(((FileEntry *)left)->fileName,
                   ((FileEntry *)right)->fileName);
}

static FILE *getCachedFile(Log *log, const char *fileName)
{
    uint32_t hash = hashStr(hashInit(), fileName);
    FileEntry *entry = findInHashTable(&log->fileCache,
                                       &(FileEntry){.fileName = fileName},
                                       hash,
                                       sizeof(FileEntry),
                                       compareFileEntries);
    if (entry)
        return entry->file;
    FILE *file = fopen(fileName, "rb");
    if (!file)
        return NULL;
    if (!insertInHashTable(&log->fileCache,
                           &(FileEntry){.fileName = fileName, .file = file},
                           hash,
                           sizeof(FileEntry),
                           compareFileEntries))
        assert(false && "cannot insert file in what file cache");
    return file;
}

static size_t findLineBegin(FILE *file, size_t offset)
{
    char data[LINE_SEARCH_RANGE];
    while (offset > 0) {
        size_t range = offset > LINE_SEARCH_RANGE ? LINE_SEARCH_RANGE : offset;
        offset -= range;
        fseek(file, offset, SEEK_SET);
        fread(data, 1, range, file);
        for (size_t i = range; i-- > 0;) {
            if (data[i] == '\n')
                return offset + i + 1;
        }
    }
    return 0;
}

static size_t findLineEnd(FILE *file, size_t offset)
{
    char data[LINE_SEARCH_RANGE];
    fseek(file, offset, SEEK_SET);
    while (true) {
        size_t read_count = fread(data, 1, LINE_SEARCH_RANGE, file);
        for (size_t i = 0; i < read_count; ++i) {
            if (data[i] == '\n')
                return offset + i;
        }
        offset += read_count;
        if (read_count < LINE_SEARCH_RANGE)
            return offset;
    }
}

static size_t countDigits(size_t i)
{
    size_t n = 1;
    while (i >= 10) {
        i /= 10;
        n++;
    }
    return n;
}

static void printEmptySpace(FormatState *state, size_t lineNumberLen)
{
    if (lineNumberLen >= 4)
        format(state, "    ", NULL), lineNumberLen -= 4;
    for (size_t i = 0; i < lineNumberLen; ++i)
        format(state, " ", NULL);
}

static void printFileLine(FormatState *state, size_t line_begin, FILE *file)
{
    char line_buf[LINE_BUF_CAPACITY];
    fseek(file, line_begin, SEEK_SET);
    while (true) {
        if (!fgets(line_buf, LINE_BUF_CAPACITY, file))
            break;
        char *end_line = strrchr(line_buf, '\n');
        if (end_line)
            *end_line = '\0';
        format(state, "{s}", (FormatArg[]){{.s = line_buf}});
        if (end_line)
            break;
    }
    format(state, "\n", NULL);
}

static void printLineMarkers(FormatState *state,
                             FormatStyle style,
                             size_t count)
{
    format(state, "{$}", (FormatArg[]){{.style = style}});
    for (size_t i = 0; i < count; ++i)
        format(state, "^", NULL);
    format(state, "{$}\n", (FormatArg[]){{.style = resetStyle}});
}

static bool isMultilineFileLoc(const FileLoc *fileLoc)
{
    return fileLoc->begin.row != fileLoc->end.row;
}

static void printDiagnostic(Log *log,
                            FormatState *state,
                            FormatStyle style,
                            const FileLoc *fileLoc)
{
    FILE *file =
        fileLoc->fileName ? getCachedFile(log, fileLoc->fileName) : NULL;

    if (!file)
        return;

    size_t lineNumberLen = countDigits(fileLoc->end.row);
    size_t beginOffset = findLineBegin(file, fileLoc->begin.byteOffset);

    printEmptySpace(state, lineNumberLen + 1);
    format(state,
           "{$}|{$}\n",
           (FormatArg[]){{.style = style}, {.style = resetStyle}});

    printEmptySpace(state, lineNumberLen - countDigits(fileLoc->begin.row));
    format(state,
           "{$}{u}{$} {$}|{$}",
           (FormatArg[]){{.style = locStyle},
                         {.u = fileLoc->begin.row},
                         {.style = resetStyle},
                         {.style = style},
                         {.style = resetStyle}});
    printFileLine(state, beginOffset, file);

    printEmptySpace(state, lineNumberLen + 1);
    format(state,
           "{$}|{$}",
           (FormatArg[]){{.style = style}, {.style = resetStyle}});

    printEmptySpace(state, fileLoc->begin.byteOffset - beginOffset);
    if (isMultilineFileLoc(fileLoc)) {
        printLineMarkers(state,
                         style,
                         findLineEnd(file, fileLoc->begin.byteOffset) -
                             fileLoc->begin.byteOffset);

        if (fileLoc->begin.row + 1 < fileLoc->end.row) {
            printEmptySpace(state, lineNumberLen);
            format(state,
                   "{$}...{$}\n",
                   (FormatArg[]){{.style = locStyle}, {.style = resetStyle}});
        }

        format(state,
               "{$}{u}{$} {$}|{$}",
               (FormatArg[]){{.style = locStyle},
                             {.u = fileLoc->end.row},
                             {.style = resetStyle},
                             {.style = style},
                             {.style = resetStyle}});
        size_t end_offset = findLineBegin(file, fileLoc->end.byteOffset);
        printFileLine(state, end_offset, file);

        printEmptySpace(state, lineNumberLen + 1);
        format(state,
               "{$}|{$}",
               (FormatArg[]){{.style = style}, {.style = resetStyle}});

        printLineMarkers(state, style, fileLoc->end.byteOffset - end_offset);
    }
    else
        printLineMarkers(
            state, style, fileLoc->end.byteOffset - fileLoc->begin.byteOffset);
}

static void printDiagnosticToState(FormatState *state,
                                   const Diagnostic *diag,
                                   void *ctx)
{
    Log *log = ctx;
    if (diag->kind == dkError)
        log->errorCount++;
    else if (diag->kind == dkWarning)
        log->warningCount++;

    if (log->errorCount >= log->maxErrors)
        return;

    if ((diag->kind == dkError || diag->kind == dkWarning) &&
        log->errorCount + log->warningCount > 1)
        format(state, "\n", NULL);

    static const FormatStyle header_styles[] = {{STYLE_BOLD, COLOR_RED},
                                                {STYLE_BOLD, COLOR_YELLOW},
                                                {STYLE_BOLD, COLOR_CYAN}};
    static const char *headers[] = {"error", "warning", "note"};

    // A warning's name is what --warnings and --warning-as-error accept
    if (diag->code)
        format(state,
               "{$}{s}[{s}]{$}: ",
               (FormatArg[]){{.style = header_styles[diag->kind]},
                             {.s = headers[diag->kind]},
                             {.s = diag->code},
                             {.style = resetStyle}});
    else
        format(state,
               "{$}{s}{$}: ",
               (FormatArg[]){{.style = header_styles[diag->kind]},
                             {.s = headers[diag->kind]},
                             {.style = resetStyle}});
    format(state, diag->fmt, diag->args);
    format(state, "\n", NULL);
    if (diag->loc.fileName) {
        format(state,
               memcmp(&diag->loc.begin, &diag->loc.end, sizeof(diag->loc.begin))
                   ? "  in {$}{s}:{u32}:{u32} (to {u32}:{u32}){$}\n"
                   : "  in {$}{s}:{u32}:{u32}{$}\n",
               (FormatArg[]){{.style = locStyle},
                             {.s = diag->loc.fileName},
                             {.u32 = diag->loc.begin.row},
                             {.u32 = diag->loc.begin.col},
                             {.u32 = diag->loc.end.row},
                             {.u32 = diag->loc.end.col},
                             {.style = resetStyle}});
        if (log->showDiagnostics)
            printDiagnostic(log, state, header_styles[diag->kind], &diag->loc);
    }
}

static void printCodedMessage(Log *log,
                              DiagnosticKind msg_type,
                              cstring code,
                              const FileLoc *fileLoc,
                              const char *format_str,
                              const FormatArg *args)
{
    Diagnostic diag = {.loc = fileLoc ? *fileLoc : (FileLoc){},
                       .kind = msg_type,
                       .fmt = format_str,
                       .args = args,
                       .code = code};
    if (log->handler) {
        log->handler(&diag, log->handlerCtx);
    }
    else {
        printDiagnosticToConsole(&diag, log);
    }
}

static void printMessage(Log *log,
                         DiagnosticKind msg_type,
                         const FileLoc *fileLoc,
                         const char *format_str,
                         const FormatArg *args)
{
    printCodedMessage(log, msg_type, NULL, fileLoc, format_str, args);
}

void logPushFrame(Log *log, const FileLoc *loc, cstring fmt, FormatArg arg)
{
    if (log->frames.count == log->frames.capacity) {
        log->frames.capacity =
            log->frames.capacity ? log->frames.capacity * 2 : 16;
        log->frames.items = reallocOrDie(
            log->frames.items, sizeof(LogFrame) * log->frames.capacity);
    }
    log->frames.items[log->frames.count++] =
        (LogFrame){.loc = *loc, .fmt = fmt, .arg = arg};
}

void logPopFrame(Log *log)
{
    csAssert0(log->frames.count > 0);
    log->frames.count--;
}

// Frames are reported innermost first, the middle ones are elided when deep
static void printFrameNotes(Log *log)
{
    u32 total = 0;
    for (u32 i = 0; i < log->frames.count; i++)
        total += log->frames.items[i].loc.fileName != NULL;

    u32 limit = log->maxFrameNotes;
    bool elide = limit && total > limit;
    u32 head = (limit + 1) / 2, tail = limit / 2, shown = 0;
    for (u32 i = log->frames.count; i-- > 0;) {
        const LogFrame *frame = &log->frames.items[i];
        if (!frame->loc.fileName)
            continue;

        u32 idx = shown++;
        if (elide && idx >= head && idx < total - tail) {
            if (idx == head)
                printMessage(log,
                             dkNote,
                             NULL,
                             "... {u32} more instantiations skipped",
                             (FormatArg[]){{.u32 = total - limit}});
            continue;
        }
        printMessage(log, dkNote, &frame->loc, frame->fmt, &frame->arg);
    }
}

void logError(Log *log,
              const FileLoc *fileLoc,
              const char *format_str,
              const FormatArg *args)
{
    printMessage(log, dkError, fileLoc, format_str, args);
    printFrameNotes(log);
}

void logWarning(Log *log,
                const FileLoc *fileLoc,
                const char *format_str,
                const FormatArg *args)
{
    printMessage(log, dkWarning, fileLoc, format_str, args);
}

void logWarningWithId(Log *log,
                      u8 warningId,
                      const FileLoc *fileLoc,
                      const char *format_str,
                      const FormatArg *args)
{
    static const cstring names[] = {
#define f(NAME, IDX) [IDX] = #NAME,
        CXY_COMPILER_WARNINGS(f)
#undef f
    };
    u64 bit = BIT(warningId);
    if (log->enabledWarnings.num & bit)
        printCodedMessage(log,
                          (log->warningsAsErrors & bit) ? dkError : dkWarning,
                          warningId < sizeof(names) / sizeof(names[0]) ? names[warningId]
                                                                       : NULL,
                          fileLoc,
                          format_str,
                          args);
}

void printDiagnosticToConsole(const Diagnostic *diag, void *ctx)
{
    Log *log = ctx;
    FormatState state = newFormatState("", log->ignoreStyles);
    printDiagnosticToState(&state, diag, ctx);
    char *msg = formatStateToString(&state);
    printStatus(log, "");
    fputs(msg, stdout);
    freeFormatState(&state);
    free(msg);
}

void printDiagnosticToMemory(const Diagnostic *diag, void *ctx)
{
    DiagnosticMemoryPrintCtx *memoryPrintCtx = ctx;
    printDiagnosticToState(memoryPrintCtx->state, diag, memoryPrintCtx->L);
}

u64 parseWarningLevels(Log *L, cstring str)
{
    return parseWarningLevelsFrom(L, L->enabledWarnings.num, str);
}

u64 parseWarningLevelsFrom(Log *L, u64 base, cstring str)
{
    WarningId warnings = base;
    if (str == NULL)
        return warnings;

    // Tokens are `Name`, `~Name`, separated by `|`. Only the copy is modified.
    char *copy = strdup(str);
    char *cursor = copy;
    while (cursor != NULL) {
        char *token = cursor;
        char *bar = strchr(cursor, '|');
        if (bar) {
            *bar = '\0';
            cursor = bar + 1;
        }
        else
            cursor = NULL;

        while (isspace((unsigned char)*token))
            token++;
        char *tail = token + strlen(token);
        while (tail > token && isspace((unsigned char)tail[-1]))
            *--tail = '\0';
        if (*token == '\0')
            continue;

        bool flip = *token == '~';
        if (flip) {
            token++;
            while (isspace((unsigned char)*token))
                token++;
        }

        u64 warning = parseNextWarningId(L, token);
        if (warning == wrn_Error) {
            free(copy);
            return wrn_Error;
        }

        if (flip)
            warnings &= ~warning;
        else if (warning == wrnNone) // `None` clears what came before
            warnings = wrnNone;
        else
            warnings |= warning;
    }

    free(copy);
    return warnings;
}

void logNote(Log *log,
             const FileLoc *fileLoc,
             const char *format_str,
             const FormatArg *args)
{
    printMessage(log, dkNote, fileLoc, format_str, args);
}

const FileLoc *builtinLoc(void)
{
    static FileLoc builtin = {
        .fileName = NULL, .begin = {0, 0, 0}, .end = {0, 0, 0}};
    return &builtin;
}

void printStatus_(Log *L, bool always, const char *fmt, va_list args)
{
    if (L->progress || always) {
        static int lastPrinted = 0;
        printf("\r\033[K");
        lastPrinted = vprintf(fmt, args);
        fflush(stdout);
    }
}

void printStatus(Log *L, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printStatus_(L, false, fmt, args);
    va_end(args);
}

void printStatusAlways(Log *L, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printStatus_(L, true, fmt, args);
    va_end(args);
}

void printStatusSticky(Log *L, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    va_end(args);
}
