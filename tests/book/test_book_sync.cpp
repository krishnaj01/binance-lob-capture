#include "book/BookSync.hpp"

#include <gtest/gtest.h>

#include "TestUtil.hpp"

namespace blc {
namespace {

using test::lvl;
using test::px;

const std::vector<Level> kBids = {lvl("100", "1"), lvl("99", "1"), lvl("98", "1"),
                                  lvl("97", "1"),  lvl("96", "1")};
const std::vector<Level> kAsks = {lvl("101", "1"), lvl("102", "1"), lvl("103", "1"),
                                  lvl("104", "1"), lvl("105", "1")};

DepthIds spot(std::uint64_t U, std::uint64_t u) { return {U, u, 0}; }
DepthIds usdm(std::uint64_t U, std::uint64_t u, std::uint64_t pu) { return {U, u, pu}; }

TEST(BookSyncSpot, DiffsBeforeSnapshotAreUnsynced) {
    BookSync sync(Venue::Spot);
    EXPECT_FALSE(sync.synced());
    EXPECT_EQ(sync.check_diff(spot(1, 5)), DiffAction::Unsynced);
}

TEST(BookSyncSpot, SnapshotThenContiguousDiffs) {
    BookSync sync(Venue::Spot);
    ASSERT_EQ(sync.apply_snapshot(100, kBids, kAsks), SnapshotAction::Apply);
    EXPECT_TRUE(sync.synced());
    EXPECT_EQ(sync.last_update_id(), 100u);

    EXPECT_EQ(sync.check_diff(spot(90, 99)), DiffAction::Stale);   // older than snapshot
    EXPECT_EQ(sync.check_diff(spot(95, 100)), DiffAction::Apply);  // same tick as snapshot
    EXPECT_EQ(sync.check_diff(spot(99, 103)), DiffAction::Apply);  // straddles snapshot
    EXPECT_EQ(sync.check_diff(spot(101, 103)), DiffAction::Apply); // exact successor
    EXPECT_EQ(sync.check_diff(spot(102, 103)), DiffAction::Gap);   // 101 missing

    sync.apply_diff(spot(101, 103), {lvl("100", "0"), lvl("100.5", "2")}, {});
    EXPECT_EQ(sync.last_update_id(), 103u);
    EXPECT_EQ(sync.book().best(Side::Bid), lvl("100.5", "2"));
    EXPECT_EQ(sync.check_diff(spot(103, 103)), DiffAction::Stale);  // u == last, after a diff
    EXPECT_EQ(sync.check_diff(spot(104, 110)), DiffAction::Apply);
    EXPECT_EQ(sync.check_diff(spot(105, 110)), DiffAction::Gap);
}

TEST(BookSyncSpot, InvalidateClearsBookUntilNextSnapshot) {
    BookSync sync(Venue::Spot);
    sync.apply_snapshot(100, kBids, kAsks);
    sync.invalidate();
    EXPECT_FALSE(sync.synced());
    EXPECT_EQ(sync.book().depth(Side::Bid), 0u);
    EXPECT_EQ(sync.check_diff(spot(101, 102)), DiffAction::Unsynced);

    // Re-seeding after a gap must not keep levels from before the gap.
    ASSERT_EQ(sync.apply_snapshot(500, {lvl("50", "1")}, {lvl("51", "1")}),
              SnapshotAction::Apply);
    EXPECT_EQ(sync.book().depth(Side::Bid), 1u);
    EXPECT_EQ(sync.book().depth(Side::Ask), 1u);
}

TEST(BookSyncSpot, SnapshotOrderingRelativeToBook) {
    BookSync sync(Venue::Spot);
    sync.apply_snapshot(100, kBids, kAsks);
    sync.apply_diff(spot(101, 110), {lvl("100", "9")}, {});

    // Older than the book: ignored, book unchanged.
    EXPECT_EQ(sync.apply_snapshot(105, kBids, kAsks), SnapshotAction::Stale);
    EXPECT_EQ(sync.book().best(Side::Bid), lvl("100", "9"));
    EXPECT_EQ(sync.last_update_id(), 110u);

    // Same id: refresh (heals drift). Newer id: jump forward.
    EXPECT_EQ(sync.apply_snapshot(110, kBids, kAsks), SnapshotAction::Apply);
    EXPECT_EQ(sync.book().best(Side::Bid), lvl("100", "1"));
    EXPECT_EQ(sync.apply_snapshot(120, kBids, kAsks), SnapshotAction::Apply);
    EXPECT_EQ(sync.last_update_id(), 120u);
    EXPECT_EQ(sync.check_diff(spot(111, 119)), DiffAction::Stale);
    EXPECT_EQ(sync.check_diff(spot(118, 125)), DiffAction::Apply);
}

// Observed live ordering on spot: depth5(L) arrives just before the diff with u == L.
// That diff must be applied so levels deeper than the top 5 keep tracking the market.
TEST(BookSyncSpot, SameTickDiffAfterSnapshotUpdatesDeeperLevels) {
    BookSync sync(Venue::Spot);
    sync.apply_snapshot(100, kBids, kAsks);
    sync.apply_diff(spot(101, 110), {lvl("95", "7")}, {});  // deep level, below the top 5

    ASSERT_EQ(sync.apply_snapshot(120, kBids, kAsks), SnapshotAction::Apply);  // tick 120
    const DepthIds same_tick = spot(111, 120);
    ASSERT_EQ(sync.check_diff(same_tick), DiffAction::Apply);
    sync.apply_diff(same_tick, {lvl("100", "1"), lvl("95", "0"), lvl("94", "3")}, {});

    Level levels[7];
    ASSERT_EQ(sync.book().top(Side::Bid, levels, 7), 6u);
    EXPECT_EQ(levels[0], lvl("100", "1"));  // agrees with the snapshot
    EXPECT_EQ(levels[4], lvl("96", "1"));
    EXPECT_EQ(levels[5], lvl("94", "3"));   // 95 removed, 94 added beyond the top 5
    EXPECT_EQ(sync.check_diff(spot(121, 125)), DiffAction::Apply);
}

TEST(BookSyncSpot, InvalidSnapshotIsIgnored) {
    BookSync sync(Venue::Spot);
    const std::vector<Level> bad_bids = {lvl("99", "1"), lvl("100", "1")};   // worst first
    const std::vector<Level> bad_asks = {lvl("102", "1"), lvl("101", "1")};  // worst first
    EXPECT_EQ(sync.apply_snapshot(100, bad_bids, kAsks), SnapshotAction::Invalid);
    EXPECT_FALSE(sync.synced());

    sync.apply_snapshot(100, kBids, kAsks);
    EXPECT_EQ(sync.apply_snapshot(101, kBids, bad_asks), SnapshotAction::Invalid);
    EXPECT_EQ(sync.last_update_id(), 100u);
    EXPECT_EQ(sync.book().best(Side::Ask), lvl("101", "1"));  // untouched
}

TEST(BookSyncUsdm, ChainsOnPreviousFinalId) {
    BookSync sync(Venue::UsdM);
    ASSERT_EQ(sync.apply_snapshot(1000, kBids, kAsks), SnapshotAction::Apply);

    EXPECT_EQ(sync.check_diff(usdm(980, 990, 970)), DiffAction::Stale);
    EXPECT_EQ(sync.check_diff(usdm(990, 1000, 980)), DiffAction::Apply);  // same tick
    EXPECT_EQ(sync.check_diff(usdm(995, 1010, 990)), DiffAction::Apply);   // straddles
    EXPECT_EQ(sync.check_diff(usdm(1005, 1010, 1000)), DiffAction::Apply); // pu == last
    EXPECT_EQ(sync.check_diff(usdm(1005, 1010, 1003)), DiffAction::Gap);   // pu mismatch

    // USD-M ids are not dense: U need not be last+1, only pu must match.
    sync.apply_diff(usdm(1005, 1010, 1000), {}, {});
    EXPECT_EQ(sync.check_diff(usdm(1050, 1060, 1010)), DiffAction::Apply);
    EXPECT_EQ(sync.check_diff(usdm(1050, 1060, 1020)), DiffAction::Gap);
}

}  // namespace
}  // namespace blc
