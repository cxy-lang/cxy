/**
 * Unit Tests: the sampling loop of `cxy utils mem analyze --watch` and its duration
 * syntax (src/cxy/utils/mem/watch.c). The loop runs on a fake clock.
 */

#include "doctest.h"

#include "utils/mem/watch.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

u64 parse(const char *text)
{
    u64 ms = 0;
    REQUIRE_MESSAGE(memParseDuration(text, &ms), text);
    return ms;
}

bool refused(const char *text)
{
    u64 ms = 12345;
    return !memParseDuration(text, &ms);
}

// A clock that only moves when the loop waits or when a snapshot takes time
struct Fake {
    u64 clock = 1000; // does not start at zero
    u64 takeCostMs = 0;
    int failAt = -1;
    std::vector<u64> waits;
    std::vector<u64> takenAt;

    static u64 now(void *ctx) { return ((Fake *)ctx)->clock; }
    static void wait(void *ctx, u64 ms)
    {
        Fake *f = (Fake *)ctx;
        f->waits.push_back(ms);
        f->clock += ms;
    }
    static bool take(void *ctx, u32 index, MemSnapshot *out, char *error, size_t errorSize)
    {
        Fake *f = (Fake *)ctx;
        if ((int)index == f->failAt) {
            snprintf(error, errorSize, "sample %u failed", index);
            return false;
        }
        f->takenAt.push_back(f->clock);
        f->clock += f->takeCostMs;
        memset(out, 0, sizeof(*out));
        out->tick = index + 1; // lets the test see which one is which
        return true;
    }
    MemWatchOps ops() { return {now, wait, take}; }
};

} // namespace

TEST_CASE("Mem watch: durations")
{
    CHECK(parse("500ms") == 500);
    CHECK(parse("30s") == 30000);
    CHECK(parse("30") == 30000); // seconds when there is no unit
    CHECK(parse("2m") == 120000);
    CHECK(parse("1h") == 3600000);
    CHECK(parse("1.5s") == 1500);
    CHECK(parse("0.25m") == 15000);
    CHECK(parse("1ms") == 1);
}

TEST_CASE("Mem watch: durations that are refused")
{
    CHECK(refused(nullptr));
    CHECK(refused(""));
    CHECK(refused("s"));
    CHECK(refused("abc"));
    CHECK(refused("0"));
    CHECK(refused("0s"));
    CHECK(refused("-5s"));
    CHECK(refused("10x"));
    CHECK(refused("10 s"));
    CHECK(refused("1d"));
    CHECK(refused("0.0001ms"));
    CHECK(refused("1000000h"));
    // a refused text leaves the value alone
    u64 ms = 12345;
    CHECK_FALSE(memParseDuration("nope", &ms));
    CHECK(ms == 12345);
}

TEST_CASE("Mem watch: snapshots are taken at equal intervals")
{
    Fake fake;
    MemWatchOps ops = fake.ops();
    MemSnapshot out[3];
    char error[128];
    REQUIRE(memWatchRun(&ops, &fake, 3, 10000, out, error, sizeof(error)));
    REQUIRE(fake.takenAt.size() == 3);
    CHECK(fake.takenAt[0] == 1000 + 10000);
    CHECK(fake.takenAt[1] == 1000 + 20000);
    CHECK(fake.takenAt[2] == 1000 + 30000);
    CHECK(out[0].tick == 1);
    CHECK(out[1].tick == 2);
    CHECK(out[2].tick == 3);
    for (auto &s : out)
        memSnapshotFree(&s);
}

TEST_CASE("Mem watch: a slow snapshot does not push the later ones back")
{
    Fake fake;
    fake.takeCostMs = 400;
    MemWatchOps ops = fake.ops();
    MemSnapshot out[3];
    char error[128];
    REQUIRE(memWatchRun(&ops, &fake, 3, 1000, out, error, sizeof(error)));
    CHECK(fake.takenAt[0] == 2000);
    CHECK(fake.takenAt[1] == 3000);
    CHECK(fake.takenAt[2] == 4000);
    // the loop waited for what was left of each interval
    REQUIRE(fake.waits.size() == 3);
    CHECK(fake.waits[0] == 1000);
    CHECK(fake.waits[1] == 600);
    CHECK(fake.waits[2] == 600);
}

TEST_CASE("Mem watch: a snapshot slower than the interval is taken at once")
{
    Fake fake;
    fake.takeCostMs = 2500;
    MemWatchOps ops = fake.ops();
    MemSnapshot out[3];
    char error[128];
    REQUIRE(memWatchRun(&ops, &fake, 3, 1000, out, error, sizeof(error)));
    // sample 2 is due at +2000 but sample 1 ended at +3500: no wait
    CHECK(fake.waits.size() == 1);
    CHECK(fake.takenAt[1] == 1000 + 3500);
    CHECK(fake.takenAt[2] == 1000 + 6000);
}

TEST_CASE("Mem watch: a failure stops the loop and keeps the message")
{
    Fake fake;
    fake.failAt = 1;
    MemWatchOps ops = fake.ops();
    MemSnapshot out[3];
    char error[128] = "";
    CHECK_FALSE(memWatchRun(&ops, &fake, 3, 1000, out, error, sizeof(error)));
    CHECK(std::string(error) == "sample 1 failed");
    CHECK(fake.takenAt.size() == 1); // the third was never asked for
}

TEST_CASE("Mem watch: one sample")
{
    Fake fake;
    MemWatchOps ops = fake.ops();
    MemSnapshot out[1];
    char error[128];
    REQUIRE(memWatchRun(&ops, &fake, 1, 250, out, error, sizeof(error)));
    CHECK(fake.takenAt.size() == 1);
    CHECK(fake.takenAt[0] == 1250);
}
