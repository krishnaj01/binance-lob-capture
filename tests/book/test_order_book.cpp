#include "book/OrderBook.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "TestUtil.hpp"

namespace blc {
namespace {

using test::lvl;
using test::px;

std::vector<Level> top(const OrderBook& book, Side side, std::size_t n = 5) {
    std::vector<Level> out(n);
    out.resize(book.top(side, out.data(), n));
    return out;
}

TEST(OrderBook, KeepsBothSidesSortedBestFirst) {
    OrderBook book;
    book.set_level(Side::Bid, px("99"), px("1"));
    book.set_level(Side::Bid, px("101"), px("2"));
    book.set_level(Side::Bid, px("100"), px("3"));
    book.set_level(Side::Ask, px("103"), px("1"));
    book.set_level(Side::Ask, px("102"), px("2"));
    book.set_level(Side::Ask, px("104"), px("3"));

    EXPECT_EQ(top(book, Side::Bid),
              (std::vector<Level>{lvl("101", "2"), lvl("100", "3"), lvl("99", "1")}));
    EXPECT_EQ(top(book, Side::Ask),
              (std::vector<Level>{lvl("102", "2"), lvl("103", "1"), lvl("104", "3")}));
    EXPECT_EQ(book.best(Side::Bid), lvl("101", "2"));
    EXPECT_EQ(book.best(Side::Ask), lvl("102", "2"));
}

TEST(OrderBook, ZeroQuantityRemovesLevel) {
    OrderBook book;
    book.set_level(Side::Bid, px("100"), px("1"));
    book.set_level(Side::Bid, px("99"), px("1"));
    book.set_level(Side::Bid, px("100"), 0);
    EXPECT_EQ(top(book, Side::Bid), (std::vector<Level>{lvl("99", "1")}));

    book.set_level(Side::Bid, px("42"), 0);  // removing an absent level is a no-op
    EXPECT_EQ(book.depth(Side::Bid), 1u);
}

TEST(OrderBook, UpdateReplacesQuantity) {
    OrderBook book;
    book.set_level(Side::Ask, px("10"), px("1"));
    book.set_level(Side::Ask, px("10"), px("7.5"));
    EXPECT_EQ(top(book, Side::Ask), (std::vector<Level>{lvl("10", "7.5")}));
}

TEST(OrderBook, TopReturnsAtMostAvailableLevels) {
    OrderBook book;
    for (int p = 1; p <= 8; ++p) book.set_level(Side::Bid, p * kScale, kScale);
    const auto t = top(book, Side::Bid);
    ASSERT_EQ(t.size(), 5u);
    EXPECT_EQ(t.front().price, 8 * kScale);
    EXPECT_EQ(t.back().price, 4 * kScale);
    EXPECT_FALSE(OrderBook().best(Side::Ask).has_value());
}

TEST(OrderBook, ReplaceTopKeepsDeeperLevelsAndDropsStaleOnes) {
    OrderBook book;
    // Stale level 99.5 (better than the snapshot's best) and deep levels 94, 93.
    for (const char* p : {"99.5", "99", "98", "97", "96", "95", "94", "93"}) {
        book.set_level(Side::Bid, px(p), px("1"));
    }
    book.replace_top(Side::Bid,
                     {lvl("99", "2"), lvl("98", "2"), lvl("97", "2"), lvl("96", "2"),
                      lvl("95", "2")},
                     5);
    EXPECT_EQ(top(book, Side::Bid, 10),
              (std::vector<Level>{lvl("99", "2"), lvl("98", "2"), lvl("97", "2"), lvl("96", "2"),
                                  lvl("95", "2"), lvl("94", "1"), lvl("93", "1")}));
}

TEST(OrderBook, ReplaceTopOnAskSide) {
    OrderBook book;
    for (const char* p : {"100.5", "101", "105", "106"}) book.set_level(Side::Ask, px(p), px("1"));
    book.replace_top(Side::Ask,
                     {lvl("101", "3"), lvl("102", "3"), lvl("103", "3"), lvl("104", "3"),
                      lvl("104.5", "3")},
                     5);
    EXPECT_EQ(top(book, Side::Ask, 10),
              (std::vector<Level>{lvl("101", "3"), lvl("102", "3"), lvl("103", "3"),
                                  lvl("104", "3"), lvl("104.5", "3"), lvl("105", "1"),
                                  lvl("106", "1")}));
}

TEST(OrderBook, ShortSnapshotReplacesWholeSide) {
    OrderBook book;
    for (const char* p : {"10", "9", "8", "7", "6", "5"}) book.set_level(Side::Bid, px(p), px("1"));
    book.replace_top(Side::Bid, {lvl("9", "4"), lvl("8", "4")}, 5);
    EXPECT_EQ(top(book, Side::Bid, 10), (std::vector<Level>{lvl("9", "4"), lvl("8", "4")}));

    book.replace_top(Side::Bid, {}, 5);  // empty side
    EXPECT_EQ(book.depth(Side::Bid), 0u);
}

TEST(OrderBook, LevelCapDropsWorstLevels) {
    OrderBook book(3);
    for (int p = 1; p <= 5; ++p) book.set_level(Side::Ask, p * kScale, kScale);
    EXPECT_EQ(book.depth(Side::Ask), 3u);
    EXPECT_EQ(top(book, Side::Ask).back().price, 3 * kScale);  // 4 and 5 dropped
}

TEST(OrderBook, SnapshotValidation) {
    EXPECT_TRUE(OrderBook::is_valid_snapshot(Side::Bid, {lvl("2", "1"), lvl("1", "1")}));
    EXPECT_FALSE(OrderBook::is_valid_snapshot(Side::Bid, {lvl("1", "1"), lvl("2", "1")}));
    EXPECT_TRUE(OrderBook::is_valid_snapshot(Side::Ask, {lvl("1", "1"), lvl("2", "1")}));
    EXPECT_FALSE(OrderBook::is_valid_snapshot(Side::Ask, {lvl("1", "1"), lvl("1", "1")}));
    EXPECT_FALSE(OrderBook::is_valid_snapshot(Side::Ask, {lvl("1", "0")}));
    EXPECT_TRUE(OrderBook::is_valid_snapshot(Side::Ask, {}));
}

}  // namespace
}  // namespace blc
