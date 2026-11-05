/*
 * A small streaming JSON writer for the trace library.
 *
 * The query server writes its answers with it: straight to the socket, nothing is
 * built in memory first, and it never allocates, so it is safe in the query thread
 * and in the forked child that builds the ownership graph. It adds no symbols the
 * program could clash with: everything here is `static inline`.
 *
 * The writer cannot produce malformed JSON. A call that would (a value in an object
 * without a key, a key in an array, a mismatched closer, a second top level value,
 * nesting deeper than JSON_MAX_DEPTH) marks the writer failed and nothing more is
 * written. The same happens when the sink fails, for instance because the client
 * went away. `jsonFinish` says whether the document is complete and valid.
 *
 *     JsonWriter w;
 *     jsonInitFd(&w, fd);
 *     jsonBeginObject(&w);
 *     jsonFieldString(&w, "type", "Node");
 *     jsonKey(&w, "sites");
 *     jsonBeginArray(&w);
 *     ...
 *     jsonEndArray(&w);
 *     jsonEndObject(&w);
 *     jsonFinish(&w);
 *
 * Strings are escaped. Bytes that are not valid UTF-8 become U+FFFD, one per byte,
 * so a file name or type name with junk in it cannot break the document. Numbers
 * that JSON cannot express (NaN, infinity) are written as null. Output is compact,
 * with no spaces.
 */
#pragma once

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define JSON_MAX_DEPTH 16
#define JSON_BUFFER_SIZE 1024

// Takes `len` bytes, false when they could not be taken
typedef bool (*JsonSink)(void *ctx, const char *data, size_t len);

typedef struct JsonWriter {
    JsonSink sink;
    void *ctx;
    size_t len;
    char buf[JSON_BUFFER_SIZE];
    bool failed;
    bool done;         // a complete top level value was written
    bool expectValue;  // a key was written, its value comes next
    int depth;
    char kind[JSON_MAX_DEPTH];      // 'o' or 'a' for each open container
    bool hasItems[JSON_MAX_DEPTH];  // a comma is needed before the next item
} JsonWriter;

static inline bool jsonFdSink(void *ctx, const char *data, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

static inline void jsonInit(JsonWriter *w, JsonSink sink, void *ctx)
{
    memset(w, 0, sizeof(*w));
    w->sink = sink;
    w->ctx = ctx;
}

static inline void jsonInitFd(JsonWriter *w, int fd)
{
    jsonInit(w, jsonFdSink, (void *)(intptr_t)fd);
}

/* ------------------------------------------------------------------ output */

static inline void jsonFail(JsonWriter *w)
{
    w->failed = true;
}

static inline void jsonFlush(JsonWriter *w)
{
    if (w->failed || w->len == 0)
        return;
    if (!w->sink(w->ctx, w->buf, w->len))
        w->failed = true;
    w->len = 0;
}

static inline void jsonPut(JsonWriter *w, char c)
{
    if (w->failed)
        return;
    if (w->len == JSON_BUFFER_SIZE)
        jsonFlush(w);
    if (!w->failed)
        w->buf[w->len++] = c;
}

static inline void jsonPutN(JsonWriter *w, const char *data, size_t len)
{
    if (w->failed)
        return;
    if (len > JSON_BUFFER_SIZE - w->len) {
        jsonFlush(w);
        if (w->failed)
            return;
        // bigger than the buffer: straight to the sink
        if (len >= JSON_BUFFER_SIZE) {
            if (!w->sink(w->ctx, data, len))
                w->failed = true;
            return;
        }
    }
    memcpy(w->buf + w->len, data, len);
    w->len += len;
}

static inline void jsonPutStr(JsonWriter *w, const char *s)
{
    jsonPutN(w, s, strlen(s));
}

/* ------------------------------------------------------------------- state */

// Called before every value (a scalar or the start of a container): checks that a
// value is allowed here and writes the comma
static inline bool jsonBeforeValue(JsonWriter *w)
{
    if (w->failed)
        return false;
    if (w->depth == 0) {
        if (w->done) {
            jsonFail(w);
            return false;
        }
        return true;
    }
    int top = w->depth - 1;
    if (w->kind[top] == 'o') {
        if (!w->expectValue) {
            jsonFail(w);
            return false;
        }
        w->expectValue = false;
        return true;
    }
    if (w->hasItems[top])
        jsonPut(w, ',');
    w->hasItems[top] = true;
    return !w->failed;
}

// Called after a scalar: at the top level it completes the document
static inline void jsonAfterScalar(JsonWriter *w)
{
    if (w->depth == 0)
        w->done = true;
}

static inline void jsonBegin(JsonWriter *w, char kind, char open)
{
    if (!jsonBeforeValue(w))
        return;
    if (w->depth == JSON_MAX_DEPTH) {
        jsonFail(w);
        return;
    }
    w->kind[w->depth] = kind;
    w->hasItems[w->depth] = false;
    w->depth++;
    jsonPut(w, open);
}

static inline void jsonEnd(JsonWriter *w, char kind, char close)
{
    if (w->failed)
        return;
    if (w->depth == 0 || w->kind[w->depth - 1] != kind || w->expectValue) {
        jsonFail(w);
        return;
    }
    w->depth--;
    jsonPut(w, close);
    jsonAfterScalar(w);
}

static inline void jsonBeginObject(JsonWriter *w) { jsonBegin(w, 'o', '{'); }
static inline void jsonEndObject(JsonWriter *w) { jsonEnd(w, 'o', '}'); }
static inline void jsonBeginArray(JsonWriter *w) { jsonBegin(w, 'a', '['); }
static inline void jsonEndArray(JsonWriter *w) { jsonEnd(w, 'a', ']'); }

/* ----------------------------------------------------------------- strings */

// Length of the valid UTF-8 sequence that starts at `s[i]`, 0 if there is none
static inline size_t jsonUtf8Length(const unsigned char *s, size_t i, size_t n)
{
    unsigned char b = s[i];
    size_t need;
    unsigned char lo = 0x80, hi = 0xBF;
    if (b >= 0xC2 && b <= 0xDF) {
        need = 1;
    }
    else if (b >= 0xE0 && b <= 0xEF) {
        need = 2;
        if (b == 0xE0)
            lo = 0xA0;
        else if (b == 0xED)
            hi = 0x9F;
    }
    else if (b >= 0xF0 && b <= 0xF4) {
        need = 3;
        if (b == 0xF0)
            lo = 0x90;
        else if (b == 0xF4)
            hi = 0x8F;
    }
    else {
        return 0;
    }
    // the continuation bytes must be inside the string
    if (i + need >= n)
        return 0;
    if (s[i + 1] < lo || s[i + 1] > hi)
        return 0;
    for (size_t k = 2; k <= need; k++) {
        if (s[i + k] < 0x80 || s[i + k] > 0xBF)
            return 0;
    }
    return need + 1;
}

static inline void jsonEscaped(JsonWriter *w, const char *str, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *s = (const unsigned char *)str;
    jsonPut(w, '"');
    for (size_t i = 0; i < n && !w->failed; i++) {
        unsigned char c = s[i];
        switch (c) {
        case '"': jsonPutStr(w, "\\\""); break;
        case '\\': jsonPutStr(w, "\\\\"); break;
        case '\n': jsonPutStr(w, "\\n"); break;
        case '\r': jsonPutStr(w, "\\r"); break;
        case '\t': jsonPutStr(w, "\\t"); break;
        case '\b': jsonPutStr(w, "\\b"); break;
        case '\f': jsonPutStr(w, "\\f"); break;
        default:
            if (c < 0x20) {
                char esc[7] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], 0};
                jsonPutN(w, esc, 6);
            }
            else if (c < 0x80) {
                jsonPut(w, (char)c);
            }
            else {
                size_t len = jsonUtf8Length(s, i, n);
                if (len == 0) {
                    jsonPutStr(w, "\\ufffd");
                }
                else {
                    jsonPutN(w, str + i, len);
                    i += len - 1;
                }
            }
        }
    }
    jsonPut(w, '"');
}

// A string value, NULL gives null
static inline void jsonString(JsonWriter *w, const char *s)
{
    if (!jsonBeforeValue(w))
        return;
    if (s == NULL)
        jsonPutStr(w, "null");
    else
        jsonEscaped(w, s, strlen(s));
    jsonAfterScalar(w);
}

// A string value of `len` bytes, which may contain NUL
static inline void jsonStringN(JsonWriter *w, const char *s, size_t len)
{
    if (!jsonBeforeValue(w))
        return;
    if (s == NULL)
        jsonPutStr(w, "null");
    else
        jsonEscaped(w, s, len);
    jsonAfterScalar(w);
}

static inline void jsonKey(JsonWriter *w, const char *key)
{
    if (w->failed)
        return;
    if (key == NULL || w->depth == 0 || w->kind[w->depth - 1] != 'o' || w->expectValue) {
        jsonFail(w);
        return;
    }
    int top = w->depth - 1;
    if (w->hasItems[top])
        jsonPut(w, ',');
    w->hasItems[top] = true;
    jsonEscaped(w, key, strlen(key));
    jsonPut(w, ':');
    w->expectValue = true;
}

/* ----------------------------------------------------------------- numbers */

static inline void jsonInt(JsonWriter *w, int64_t v)
{
    if (!jsonBeforeValue(w))
        return;
    char num[24];
    snprintf(num, sizeof(num), "%lld", (long long)v);
    jsonPutStr(w, num);
    jsonAfterScalar(w);
}

static inline void jsonUint(JsonWriter *w, uint64_t v)
{
    if (!jsonBeforeValue(w))
        return;
    char num[24];
    snprintf(num, sizeof(num), "%llu", (unsigned long long)v);
    jsonPutStr(w, num);
    jsonAfterScalar(w);
}

// The shortest text that reads back as the same double. NaN and infinity are not
// JSON, they become null.
static inline void jsonDouble(JsonWriter *w, double v)
{
    if (!jsonBeforeValue(w))
        return;
    if (!isfinite(v)) {
        jsonPutStr(w, "null");
    }
    else {
        char num[40];
        snprintf(num, sizeof(num), "%.15g", v);
        if (strtod(num, NULL) != v)
            snprintf(num, sizeof(num), "%.17g", v);
        // a locale with a decimal comma must not leak into the document
        for (char *p = num; *p; p++) {
            if (*p == ',')
                *p = '.';
        }
        jsonPutStr(w, num);
    }
    jsonAfterScalar(w);
}

static inline void jsonBool(JsonWriter *w, bool v)
{
    if (!jsonBeforeValue(w))
        return;
    jsonPutStr(w, v ? "true" : "false");
    jsonAfterScalar(w);
}

static inline void jsonNull(JsonWriter *w)
{
    if (!jsonBeforeValue(w))
        return;
    jsonPutStr(w, "null");
    jsonAfterScalar(w);
}

// An address as the string "0x...", NULL gives null
static inline void jsonPtr(JsonWriter *w, const void *p)
{
    if (!jsonBeforeValue(w))
        return;
    if (p == NULL) {
        jsonPutStr(w, "null");
    }
    else {
        char text[32];
        snprintf(text, sizeof(text), "\"0x%llx\"", (unsigned long long)(uintptr_t)p);
        jsonPutStr(w, text);
    }
    jsonAfterScalar(w);
}

/* ------------------------------------------------------------------ fields */

static inline void jsonFieldString(JsonWriter *w, const char *key, const char *v)
{
    jsonKey(w, key);
    jsonString(w, v);
}

static inline void jsonFieldInt(JsonWriter *w, const char *key, int64_t v)
{
    jsonKey(w, key);
    jsonInt(w, v);
}

static inline void jsonFieldUint(JsonWriter *w, const char *key, uint64_t v)
{
    jsonKey(w, key);
    jsonUint(w, v);
}

static inline void jsonFieldDouble(JsonWriter *w, const char *key, double v)
{
    jsonKey(w, key);
    jsonDouble(w, v);
}

static inline void jsonFieldBool(JsonWriter *w, const char *key, bool v)
{
    jsonKey(w, key);
    jsonBool(w, v);
}

static inline void jsonFieldPtr(JsonWriter *w, const char *key, const void *v)
{
    jsonKey(w, key);
    jsonPtr(w, v);
}

static inline void jsonFieldNull(JsonWriter *w, const char *key)
{
    jsonKey(w, key);
    jsonNull(w);
}

/* ------------------------------------------------------------------ finish */

// Writes what is buffered. True when a complete, valid document was written.
static inline bool jsonFinish(JsonWriter *w)
{
    if (!w->failed && (w->depth != 0 || !w->done || w->expectValue))
        jsonFail(w);
    jsonFlush(w);
    return !w->failed;
}
