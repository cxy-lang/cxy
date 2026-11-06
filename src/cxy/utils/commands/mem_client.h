/**
 * The client side of the memory trace socket, shared by `cxy utils mem <query>` and
 * `cxy utils mem analyze`.
 */

#pragma once

#include "driver/options.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;

typedef void (*MemSink)(const char *data, size_t length, void *ctx);

/** --socket, $CXY_MEM_SOCKET, --pid, then the only live traced process. */
bool memResolveSocket(const Options *options, char *out, size_t size, Log *log);

/**
 * Sends one request line and hands the reply to `sink` as it arrives. False, with a
 * message in `error`, when the program cannot be reached or does not answer in time.
 */
bool memRequest(const char *socketPath,
                const char *request,
                i64 timeoutMs,
                MemSink sink,
                void *ctx,
                char *error,
                size_t errorSize);

/** `cxy utils mem analyze`: returns the exit code. */
int memAnalyzeCommand(const Options *options, Log *log);

#ifdef __cplusplus
}
#endif
