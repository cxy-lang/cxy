/*
 * Tests for the event loop of the coroutine scheduler (stdlib/native/evloop/ae.c).
 * An idle loop must block until its timeout. A wakeup from another thread has to be
 * drained, or the wakeup fd stays readable and every later wait returns at once,
 * which is a scheduler thread that spins at 100% CPU.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
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

int main(void)
{
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
