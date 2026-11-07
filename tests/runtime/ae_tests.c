/*
 * Tests for the event loop of the coroutine scheduler (stdlib/native/evloop/ae.c).
 * An idle loop must block until its timeout. A wakeup from another thread has to be
 * drained, or the wakeup fd stays readable and every later wait returns at once,
 * which is a scheduler thread that spins at 100% CPU.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "stdlib/native/evloop/ae.h"

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static long long nowMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

// How long one wait of at most `timeout` ms really took
static long long waitFor(aeEventLoop *loop, int64_t timeout)
{
    long long start = nowMs();
    aeProcessEvents(loop, AE_FILE_EVENTS | AE_TIME_EVENTS, timeout);
    return nowMs() - start;
}

// A wait that blocks for its timeout takes about that long, one that spins takes
// nothing. The margin is wide so a slow machine does not fail it.
#define BLOCKED(ms) ((ms) >= 80)

static void testIdleWaitBlocks(void)
{
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    CHECK(loop != NULL);
    CHECK(BLOCKED(waitFor(loop, 100)));
    aeDeleteEventLoop(loop);
}

static void testWakeupIsDrained(void)
{
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeWakeup(loop);
    waitFor(loop, 100); // takes the wakeup
    // it was drained: the next waits block, they do not return at once
    CHECK(BLOCKED(waitFor(loop, 100)));
    CHECK(BLOCKED(waitFor(loop, 100)));
    aeDeleteEventLoop(loop);
}

static void testManyWakeupsAreDrained(void)
{
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    for (int i = 0; i < 5; i++)
        aeWakeup(loop);
    waitFor(loop, 100);
    waitFor(loop, 100); // a pipe holds each write, a few waits may be needed
    CHECK(BLOCKED(waitFor(loop, 100)));
    aeDeleteEventLoop(loop);
}

static void *wakeLater(void *arg)
{
    usleep(50 * 1000);
    aeWakeup((aeEventLoop *)arg);
    return NULL;
}

static void testWakeupFromAnotherThread(void)
{
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, wakeLater, loop) == 0);

    // the wait is cut short by the other thread, well before its timeout
    long long woken = waitFor(loop, 5000);
    CHECK(woken < 2000);
    pthread_join(thread, NULL);

    // and afterwards the loop blocks again
    CHECK(BLOCKED(waitFor(loop, 100)));
    aeDeleteEventLoop(loop);
}

/* ------------------------------------------- one fd, a reader and a writer */

#define MAX_CALLS 16
static struct {
    const char *who;
    int fd;
    int mask;
} calls[MAX_CALLS];
static int callCount = 0;

static void record(aeEventLoop *loop, int fd, void *data, int mask)
{
    (void)loop;
    if (callCount < MAX_CALLS)
        calls[callCount++] = (typeof(calls[0])){.who = data, .fd = fd, .mask = mask};
}

static int calledBy(const char *who)
{
    int n = 0;
    for (int i = 0; i < callCount; i++)
        n += strcmp(calls[i].who, who) == 0;
    return n;
}

static void step(aeEventLoop *loop, int64_t timeout)
{
    callCount = 0;
    aeProcessEvents(loop, AE_FILE_EVENTS | AE_TIME_EVENTS, timeout);
}

// A coroutine parked reading must be the one woken when data arrives, even when
// another coroutine is waiting to write on the same socket
static void testReaderAndWriterEachGetTheirOwn(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeCreateFileEvent(loop, sv[0], AE_READABLE, record, "reader", 0);
    aeCreateFileEvent(loop, sv[0], AE_WRITABLE, record, "writer", 0);
    CHECK(write(sv[1], "x", 1) == 1); // readable, and writable already

    step(loop, 100);
    CHECK(calledBy("reader") == 1);
    CHECK(calledBy("writer") == 1);
    // each one is told about its own direction
    for (int i = 0; i < callCount; i++) {
        if (strcmp(calls[i].who, "reader") == 0)
            CHECK(calls[i].mask == AE_READABLE);
        else
            CHECK(calls[i].mask == AE_WRITABLE);
    }
    aeDeleteEventLoop(loop);
    close(sv[0]);
    close(sv[1]);
}

// The writer finishing and removing its event leaves the reader parked, and it
// still gets its own data
static void testReaderSurvivesTheWriterLeaving(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeCreateFileEvent(loop, sv[0], AE_READABLE, record, "reader", 0);
    aeCreateFileEvent(loop, sv[0], AE_WRITABLE, record, "writer", 0);
    aeDeleteFileEvent(loop, sv[0], AE_WRITABLE);
    CHECK(write(sv[1], "x", 1) == 1);

    step(loop, 100);
    CHECK(calledBy("reader") == 1);
    CHECK(calledBy("writer") == 0);
    aeDeleteEventLoop(loop);
    close(sv[0]);
    close(sv[1]);
}

// A registration for both directions with one handler reaches that handler. kqueue
// reports each direction as its own event, epoll reports both at once: one or two calls
static void testOneHandlerForBothDirections(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeCreateFileEvent(loop, sv[0], AE_READABLE | AE_WRITABLE, record, "both", 0);
    CHECK(write(sv[1], "x", 1) == 1);

    step(loop, 100);
    CHECK(calledBy("both") >= 1 && calledBy("both") <= 2);
    aeDeleteEventLoop(loop);
    close(sv[0]);
    close(sv[1]);
}

// The timeout of a read wait is not touched by a write registration, with or
// without a timeout of its own
static void testReadTimeoutSurvivesAWriteRegistration(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeCreateFileEvent(loop, sv[0], AE_READABLE, record, "reader", 300);
    aeCreateFileEvent(loop, sv[0], AE_WRITABLE, record, "writer", 0);

    // only the writable event is ready, nothing may time out yet
    step(loop, 50);
    CHECK(calledBy("writer") == 1);
    CHECK(calledBy("reader") == 0);
    aeDeleteFileEvent(loop, sv[0], AE_WRITABLE);

    // the reader's own timeout fires on the right fd, to the right handler
    long long start = nowMs();
    while (calledBy("reader") == 0 && nowMs() - start < 2000)
        step(loop, 100);
    CHECK(calledBy("reader") == 1);
    CHECK(calls[0].fd == sv[0]);
    CHECK(calls[0].mask == AE_TIMEOUT);
    CHECK(nowMs() - start >= 150);
    aeDeleteEventLoop(loop);
    close(sv[0]);
    close(sv[1]);
}

// Two timeouts on one fd, one per direction, each fires for its own handler. A
// timer registered twice used to point at itself and hang the loop.
static void testTimeoutPerDirection(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeCreateFileEvent(loop, sv[0], AE_READABLE, record, "reader", 100);
    aeCreateFileEvent(loop, sv[0], AE_WRITABLE, record, "writer", 300);
    aeDeleteFileEvent(loop, sv[0], AE_WRITABLE); // not interested in it being ready
    aeCreateFileEvent(loop, sv[0], AE_WRITABLE, record, "writer", 300);
    aeDeleteFileEvent(loop, sv[0], AE_WRITABLE);

    long long start = nowMs();
    while (calledBy("reader") == 0 && nowMs() - start < 2000)
        step(loop, 400);
    CHECK(calledBy("reader") == 1);
    CHECK(calls[0].mask == AE_TIMEOUT);
    CHECK(nowMs() - start < 1000);
    aeDeleteEventLoop(loop);
    close(sv[0]);
    close(sv[1]);
}

// Registering again for the same direction replaces the handler and the timeout
static void testReregisterReplaces(void)
{
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    aeEventLoop *loop = aeCreateEventLoop(NULL, 64);
    aeCreateFileEvent(loop, sv[0], AE_READABLE, record, "old", 100);
    aeCreateFileEvent(loop, sv[0], AE_READABLE, record, "new", 0);
    CHECK(write(sv[1], "x", 1) == 1);

    step(loop, 100);
    CHECK(calledBy("new") == 1);
    CHECK(calledBy("old") == 0);
    aeDeleteFileEvent(loop, sv[0], AE_READABLE);
    // the old 100 ms timeout is gone
    step(loop, 250);
    CHECK(calledBy("old") == 0 && calledBy("new") == 0);
    aeDeleteEventLoop(loop);
    close(sv[0]);
    close(sv[1]);
}

int main(void)
{
    testReaderAndWriterEachGetTheirOwn();
    testReaderSurvivesTheWriterLeaving();
    testOneHandlerForBothDirections();
    testReadTimeoutSurvivesAWriteRegistration();
    testTimeoutPerDirection();
    testReregisterReplaces();
    testIdleWaitBlocks();
    testWakeupIsDrained();
    testManyWakeupsAreDrained();
    testWakeupFromAnotherThread();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("ae tests: ok\n");
    return failures != 0;
}
