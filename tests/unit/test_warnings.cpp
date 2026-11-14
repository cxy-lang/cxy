/**
 * Unit Tests: compiler warning lists (`--warnings`, `--warning-as-error`)
 */

#include "doctest.h"

#include "core/log.h"

#include <cstring>
#include <initializer_list>

namespace {

void quiet(const Diagnostic *, void *) {}

struct LogFixture {
    Log log = newLog(nullptr, quiet, nullptr);

    u64 parse(const char *list, u64 base)
    {
        return parseWarningLevelsFrom(&log, base, list);
    }
};

} // namespace

TEST_CASE("Warning lists: single names")
{
    LogFixture f;
    CHECK(f.parse("DanglingBorrow", wrnNone) == BIT(wrnDanglingBorrow));
    CHECK(f.parse("", 5) == 5);
    CHECK(f.parse(nullptr, 5) == 5);
}

TEST_CASE("Warning lists: combined with `|`")
{
    LogFixture f;
    // Read-only memory, the parser must not write into it
    const char *list = "UseAfterMove|DoubleDrop";
    CHECK(f.parse(list, wrnNone) == (BIT(wrnUseAfterMove) | BIT(wrnDoubleDrop)));
    CHECK(strcmp(list, "UseAfterMove|DoubleDrop") == 0);
}

TEST_CASE("Warning lists: `~` removes a warning")
{
    LogFixture f;
    CHECK(f.parse("Ownership|~DanglingBorrow", wrnNone) ==
          (wrnOwnership & ~BIT(wrnDanglingBorrow)));
    CHECK(f.parse("~UnusedVariable", BIT(wrnUnusedVariable) | BIT(wrnRedundantStmt)) ==
          BIT(wrnRedundantStmt));
}

TEST_CASE("Warning lists: every ownership rule is in the `Ownership` group, off by default")
{
    LogFixture f;
    for (u64 warning : {(u64)wrnOwnerLeak, (u64)wrnUseAfterMove, (u64)wrnDoubleDrop,
                        (u64)wrnUnclearedMove, (u64)wrnOverwrittenOwner, (u64)wrnDropFlagMismatch,
                        (u64)wrnRedeclaredOwner, (u64)wrnReturnedBorrow, (u64)wrnDanglingBorrow,
                        (u64)wrnMoveOfReference, (u64)wrnTemporaryOwnership, (u64)wrnMovedGlobal}) {
        CHECK((wrnOwnership & BIT(warning)) != 0);
        CHECK((wrnDefault & BIT(warning)) == 0);
    }
    CHECK(f.parse("MovedGlobal", wrnNone) == BIT(wrnMovedGlobal));
    CHECK(f.parse("Ownership|~MovedGlobal", wrnNone) == (wrnOwnership & ~BIT(wrnMovedGlobal)));
}

TEST_CASE("Warning lists: spaces and empty tokens")
{
    LogFixture f;
    CHECK(f.parse(" UseAfterMove | ~DoubleDrop ", BIT(wrnDoubleDrop)) == BIT(wrnUseAfterMove));
    CHECK(f.parse("UseAfterMove||DoubleDrop|", wrnNone) ==
          (BIT(wrnUseAfterMove) | BIT(wrnDoubleDrop)));
}

TEST_CASE("Warning lists: All and None")
{
    LogFixture f;
    CHECK(f.parse("All", wrnNone) == wrnAll);
    CHECK(f.parse("All|~DanglingBorrow", wrnNone) == (wrnAll & ~BIT(wrnDanglingBorrow)));
    CHECK(f.parse("None|UseAfterMove", wrnAll) == BIT(wrnUseAfterMove));
}

TEST_CASE("Warning lists: errors")
{
    LogFixture f;
    CHECK((f.parse("NoSuchWarning", wrnNone) & wrn_Error) != 0);
    CHECK((f.parse("UseAfterMove|NoSuchWarning", wrnNone) & wrn_Error) != 0);
    CHECK((f.parse("~", wrnNone) & wrn_Error) != 0);
}
