/*
 * Unit tests for the streaming JSON writer of the trace library
 * (stdlib/trace/trace_json.h). Every output is also parsed with cJSON, a writer
 * that emits something that does not parse is wrong whatever the text looks like.
 */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "3rdParty/cJSON.h"
#include "stdlib/trace/trace_json.h"

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_TEXT(w, expected)                                                \
    do {                                                                       \
        if (strcmp(out, (expected)) != 0) {                                    \
            printf("FAIL %s:%d: got  %s\n                      want %s\n",     \
                   __FILE__, __LINE__, out, (expected));                       \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------- sinks */

static char out[1 << 16];
static size_t outLen;
static int sinkCalls;
static size_t failAfter; // 0: never fail, otherwise fail once this many bytes were taken

static bool memorySink(void *ctx, const char *data, size_t len)
{
    (void)ctx;
    sinkCalls++;
    if (failAfter != 0 && outLen + len > failAfter)
        return false;
    if (outLen + len >= sizeof(out))
        return false;
    memcpy(out + outLen, data, len);
    outLen += len;
    out[outLen] = '\0';
    return true;
}

static void reset(JsonWriter *w)
{
    outLen = 0;
    out[0] = '\0';
    sinkCalls = 0;
    failAfter = 0;
    jsonInit(w, memorySink, NULL);
}

static cJSON *parseOut(void)
{
    return cJSON_Parse(out);
}

/* ------------------------------------------------------------------- tests */

static void testEmpty(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginObject(&w);
    jsonEndObject(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w, "{}");

    reset(&w);
    jsonBeginArray(&w);
    jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w, "[]");
}

static void testNesting(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginObject(&w);
    jsonKey(&w, "a");
    jsonInt(&w, 1);
    jsonKey(&w, "b");
    jsonBeginArray(&w);
    jsonInt(&w, 1);
    jsonInt(&w, 2);
    jsonBeginObject(&w);
    jsonEndObject(&w);
    jsonBeginArray(&w);
    jsonEndArray(&w);
    jsonEndArray(&w);
    jsonKey(&w, "c");
    jsonBeginObject(&w);
    jsonKey(&w, "d");
    jsonBool(&w, true);
    jsonKey(&w, "e");
    jsonNull(&w);
    jsonKey(&w, "f");
    jsonBool(&w, false);
    jsonEndObject(&w);
    jsonEndObject(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w, "{\"a\":1,\"b\":[1,2,{},[]],\"c\":{\"d\":true,\"e\":null,\"f\":false}}");

    cJSON *root = parseOut();
    CHECK(root != NULL);
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(root, "b")) == 4);
    cJSON_Delete(root);
}

static void testFieldHelpers(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginObject(&w);
    jsonFieldString(&w, "name", "Node");
    jsonFieldInt(&w, "delta", -5);
    jsonFieldUint(&w, "bytes", 1264);
    jsonFieldDouble(&w, "ratio", 0.5);
    jsonFieldBool(&w, "ok", true);
    jsonFieldPtr(&w, "ptr", (void *)(uintptr_t)0x7bc6c04d10);
    jsonEndObject(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w,
               "{\"name\":\"Node\",\"delta\":-5,\"bytes\":1264,\"ratio\":0.5,\"ok\":true,"
               "\"ptr\":\"0x7bc6c04d10\"}");
}

static void escaped(const char *in, size_t len, const char *expected)
{
    JsonWriter w;
    reset(&w);
    jsonStringN(&w, in, len);
    CHECK(jsonFinish(&w));
    if (strcmp(out, expected) != 0) {
        printf("FAIL escape: got %s want %s\n", out, expected);
        failures++;
    }
    // cJSON keeps strings as C strings and refuses an escaped NUL, that is its
    // limit and not a defect of the document
    if (strstr(out, "\\u0000") == NULL) {
        cJSON *root = parseOut();
        CHECK(root != NULL);
        cJSON_Delete(root);
    }
}

static void testEscaping(void)
{
    escaped("plain", 5, "\"plain\"");
    escaped("a\"b", 3, "\"a\\\"b\"");
    escaped("a\\b", 3, "\"a\\\\b\"");
    escaped("a\nb\tc\rd", 7, "\"a\\nb\\tc\\rd\"");
    escaped("\b\f", 2, "\"\\b\\f\"");
    escaped("\x01\x1f", 2, "\"\\u0001\\u001f\"");
    // a NUL inside a string given with its length
    escaped("a\0b", 3, "\"a\\u0000b\"");
    // DEL and the slash pass through
    escaped("\x7f/", 2, "\"\x7f/\"");
    // a path as the trace reports them
    const char *path = "/Users/x/my \"app\"/a.cxy";
    escaped(path, strlen(path), "\"/Users/x/my \\\"app\\\"/a.cxy\"");
}

static void testUtf8(void)
{
    // valid sequences are kept as they are: e acute, euro sign, a 4 byte emoji
    escaped("\xc3\xa9", 2, "\"\xc3\xa9\"");
    escaped("\xe2\x82\xac", 3, "\"\xe2\x82\xac\"");
    escaped("\xf0\x9f\x98\x80", 4, "\"\xf0\x9f\x98\x80\"");

    // anything else would make the document invalid: one U+FFFD per bad byte
    escaped("\xff", 1, "\"\\ufffd\"");
    escaped("a\x80z", 3, "\"a\\ufffdz\"");                 // stray continuation byte
    escaped("\xc3", 1, "\"\\ufffd\"");                      // truncated at the end
    escaped("\xc3(", 2, "\"\\ufffd(\"");                    // lead byte, no continuation
    escaped("\xc0\xaf", 2, "\"\\ufffd\\ufffd\"");           // overlong '/'
    escaped("\xed\xa0\x80", 3, "\"\\ufffd\\ufffd\\ufffd\""); // a surrogate half
    escaped("\xf4\x90\x80\x80", 4, "\"\\ufffd\\ufffd\\ufffd\\ufffd\""); // above U+10FFFF
}

static void testNullString(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginArray(&w);
    jsonString(&w, NULL);
    jsonString(&w, "");
    jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w, "[null,\"\"]");
}

static void testNumbers(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginArray(&w);
    jsonInt(&w, 0);
    jsonInt(&w, INT64_MAX);
    jsonInt(&w, INT64_MIN);
    jsonUint(&w, UINT64_MAX);
    jsonDouble(&w, 0.5);
    jsonDouble(&w, 0.1); // the shortest text that reads back the same
    jsonDouble(&w, 3.0);
    jsonDouble(&w, -2.25);
    jsonDouble(&w, 1e21);
    jsonDouble(&w, 1.0 / 3.0);
    jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    cJSON *root = parseOut();
    CHECK(root != NULL);
    CHECK(strstr(out, "9223372036854775807") != NULL);
    CHECK(strstr(out, "-9223372036854775808") != NULL);
    CHECK(strstr(out, "18446744073709551615") != NULL);
    CHECK(strstr(out, "[0,") == out);
    CHECK(strstr(out, ",0.5,0.1,3,-2.25,1e+21,") != NULL);
    cJSON_Delete(root);
    // 1/3 reads back as exactly 1/3 (strtod, the same rule the writer uses)
    const char *lastComma = strrchr(out, ',');
    CHECK(lastComma != NULL && strtod(lastComma + 1, NULL) == 1.0 / 3.0);
}

static void testNonFiniteNumbers(void)
{
    // JSON has no NaN or Infinity
    JsonWriter w;
    reset(&w);
    jsonBeginArray(&w);
    jsonDouble(&w, NAN);
    jsonDouble(&w, INFINITY);
    jsonDouble(&w, -INFINITY);
    jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w, "[null,null,null]");
}

static void testPointers(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginArray(&w);
    jsonPtr(&w, (void *)(uintptr_t)0x10);
    jsonPtr(&w, NULL);
    jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    CHECK_TEXT(&w, "[\"0x10\",null]");
}

static void testLargeOutput(void)
{
    // longer than the internal buffer: written in pieces, nothing lost
    static char big[5000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    big[100] = '"';
    big[2500] = '\n';

    JsonWriter w;
    reset(&w);
    jsonBeginObject(&w);
    jsonFieldString(&w, "big", big);
    jsonFieldString(&w, "after", "end");
    jsonEndObject(&w);
    CHECK(jsonFinish(&w));
    CHECK(sinkCalls > 1);

    cJSON *root = parseOut();
    CHECK(root != NULL);
    cJSON *value = cJSON_GetObjectItem(root, "big");
    CHECK(value != NULL && strcmp(value->valuestring, big) == 0);
    CHECK(strcmp(cJSON_GetObjectItem(root, "after")->valuestring, "end") == 0);
    cJSON_Delete(root);
}

static void testManyItems(void)
{
    JsonWriter w;
    reset(&w);
    jsonBeginArray(&w);
    for (int i = 0; i < 2000; i++) {
        jsonBeginObject(&w);
        jsonFieldInt(&w, "i", i);
        jsonFieldString(&w, "s", "value");
        jsonEndObject(&w);
    }
    jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    cJSON *root = parseOut();
    CHECK(root != NULL && cJSON_GetArraySize(root) == 2000);
    CHECK(cJSON_GetObjectItem(cJSON_GetArrayItem(root, 1999), "i")->valueint == 1999);
    cJSON_Delete(root);
}

/* The writer refuses what would not be valid JSON, it never emits it */

static void testMisuse(void)
{
    JsonWriter w;

    reset(&w);
    jsonBeginObject(&w);
    jsonInt(&w, 1); // a value in an object needs a key
    CHECK(!jsonFinish(&w));

    reset(&w);
    jsonBeginArray(&w);
    jsonKey(&w, "k"); // a key in an array
    CHECK(!jsonFinish(&w));

    reset(&w);
    jsonBeginObject(&w);
    jsonKey(&w, "a");
    jsonKey(&w, "b"); // two keys in a row
    CHECK(!jsonFinish(&w));

    reset(&w);
    jsonBeginObject(&w);
    jsonKey(&w, "a");
    jsonEndObject(&w); // a key with no value
    CHECK(!jsonFinish(&w));

    reset(&w);
    jsonBeginObject(&w);
    jsonEndArray(&w); // wrong closer
    CHECK(!jsonFinish(&w));

    reset(&w);
    jsonEndObject(&w); // nothing open
    CHECK(!jsonFinish(&w));

    reset(&w);
    jsonBeginObject(&w);
    CHECK(!jsonFinish(&w)); // not closed

    reset(&w);
    jsonInt(&w, 1);
    jsonInt(&w, 2); // two top level values
    CHECK(!jsonFinish(&w));

    reset(&w);
    CHECK(!jsonFinish(&w)); // nothing written
}

static void testMisuseStopsOutput(void)
{
    // after an error nothing more is written, so what was sent never gets worse
    JsonWriter w;
    reset(&w);
    jsonBeginObject(&w);
    jsonKey(&w, "a");
    jsonInt(&w, 1);
    jsonKey(&w, "b");
    jsonKey(&w, "c"); // error here
    size_t before = outLen;
    jsonInt(&w, 2);
    jsonEndObject(&w);
    CHECK(!jsonFinish(&w));
    CHECK(outLen == before);
}

static void testDepth(void)
{
    JsonWriter w;
    reset(&w);
    for (int i = 0; i < JSON_MAX_DEPTH; i++)
        jsonBeginArray(&w);
    for (int i = 0; i < JSON_MAX_DEPTH; i++)
        jsonEndArray(&w);
    CHECK(jsonFinish(&w));
    cJSON *root = parseOut();
    CHECK(root != NULL);
    cJSON_Delete(root);

    reset(&w);
    for (int i = 0; i < JSON_MAX_DEPTH + 1; i++)
        jsonBeginArray(&w);
    CHECK(!jsonFinish(&w));
}

static void testSinkFailure(void)
{
    // the client went away: the writer notices, stops, and finish says so
    JsonWriter w;
    reset(&w);
    failAfter = 50;
    jsonBeginArray(&w);
    for (int i = 0; i < 1000; i++)
        jsonString(&w, "some text that is long enough to fill the buffer");
    jsonEndArray(&w);
    CHECK(!jsonFinish(&w));
    int calls = sinkCalls;
    jsonString(&w, "more");
    jsonFinish(&w);
    CHECK(sinkCalls == calls); // no calls after the failure
}

static void testFdSink(void)
{
    int fds[2];
    CHECK(pipe(fds) == 0);

    JsonWriter w;
    jsonInitFd(&w, fds[1]);
    jsonBeginObject(&w);
    jsonFieldString(&w, "k", "v");
    jsonFieldInt(&w, "n", 7);
    jsonEndObject(&w);
    CHECK(jsonFinish(&w));
    close(fds[1]);

    char got[256];
    ssize_t n = read(fds[0], got, sizeof(got) - 1);
    CHECK(n > 0);
    got[n > 0 ? n : 0] = '\0';
    CHECK(strcmp(got, "{\"k\":\"v\",\"n\":7}") == 0);
    close(fds[0]);
}

static void testFdSinkClosedPeer(void)
{
    // writing to a closed pipe must report the failure, not kill the program
    int fds[2];
    CHECK(pipe(fds) == 0);
    close(fds[0]);

    struct sigaction ignore = {0};
    ignore.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &ignore, NULL);

    JsonWriter w;
    jsonInitFd(&w, fds[1]);
    jsonBeginArray(&w);
    for (int i = 0; i < 5000; i++)
        jsonString(&w, "text text text text text text");
    jsonEndArray(&w);
    CHECK(!jsonFinish(&w));
    close(fds[1]);
}

static void testFdSinkLarge(void)
{
    // more than a pipe holds: a reader drains it while the writer works
    int fds[2];
    CHECK(pipe(fds) == 0);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        JsonWriter w;
        jsonInitFd(&w, fds[1]);
        jsonBeginArray(&w);
        for (int i = 0; i < 20000; i++)
            jsonInt(&w, i);
        jsonEndArray(&w);
        _exit(jsonFinish(&w) ? 0 : 1);
    }
    close(fds[1]);
    static char got[1 << 17];
    size_t len = 0;
    ssize_t n;
    while ((n = read(fds[0], got + len, sizeof(got) - 1 - len)) > 0)
        len += (size_t)n;
    got[len] = '\0';
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    cJSON *root = cJSON_Parse(got);
    CHECK(root != NULL && cJSON_GetArraySize(root) == 20000);
    cJSON_Delete(root);
}

int main(void)
{
    testEmpty();
    testNesting();
    testFieldHelpers();
    testEscaping();
    testUtf8();
    testNullString();
    testNumbers();
    testNonFiniteNumbers();
    testPointers();
    testLargeOutput();
    testManyItems();
    testMisuse();
    testMisuseStopsOutput();
    testDepth();
    testSinkFailure();
    testFdSink();
    testFdSinkClosedPeer();
    testFdSinkLarge();

    if (failures == 0)
        printf("json tests: ok\n");
    else
        printf("json tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
