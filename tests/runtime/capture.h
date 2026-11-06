/*
 * Helpers for the runtime tests that include the trace library source and check
 * what a report writes: CHECK, and CAPTURE to read a report into `out`. Needs the
 * trace library included first (JsonWriter, stdio, string).
 */
#ifndef CXY_TEST_CAPTURE_H
#define CXY_TEST_CAPTURE_H

static int failures = 0;
static char out[1 << 16];

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CONTAINS(text) CHECK(strstr(out, text) != NULL)
#define MISSING(text) CHECK(strstr(out, text) == NULL)

// Runs `stmt` with `fd` writing to a scratch file and leaves the text in `out`
#define CAPTURE(stmt)                                                          \
    do {                                                                       \
        FILE *capture = tmpfile();                                             \
        int fd = fileno(capture);                                              \
        stmt;                                                                  \
        fseek(capture, 0, SEEK_SET);                                           \
        size_t got = fread(out, 1, sizeof(out) - 1, capture);                  \
        out[got] = '\0';                                                       \
        fclose(capture);                                                       \
    } while (0)

// Like CAPTURE, with `w` a JSON writer on the same file: `out` holds the document
#define CAPTURE_JSON(stmt)                                                     \
    CAPTURE(JsonWriter w; jsonInitFd(&w, fd); stmt; CHECK(jsonFinish(&w)))

#define EXPECT(expected)                                                       \
    do {                                                                       \
        if (strcmp(out, expected) != 0) {                                      \
            printf("FAIL %s:%d\n  expected: %s\n  actual:   %s\n", __FILE__, __LINE__, expected, out); \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#endif
