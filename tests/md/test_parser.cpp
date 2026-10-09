#include "md/Parser.hpp"

#include <gtest/gtest.h>

#include "TestUtil.hpp"

namespace blc {
namespace {

using test::lvl;
namespace ondemand = simdjson::ondemand;

// Owns the padded input + parser so the object stays valid for the test body.
class Doc {
public:
    explicit Doc(std::string_view json) : json_(json) {
        EXPECT_EQ(parser_.iterate(json_).get(doc_), simdjson::SUCCESS);
        EXPECT_EQ(doc_.get_object().get(obj_), simdjson::SUCCESS);
    }
    ondemand::object& obj() { return obj_; }

private:
    simdjson::padded_string json_;
    ondemand::parser parser_;
    ondemand::document doc_;
    ondemand::object obj_;
};

TEST(Parser, StreamNames) {
    StreamName n{};
    ASSERT_TRUE(parse_stream_name("btcusdt@depth@100ms", n));
    EXPECT_EQ(n.kind, StreamKind::DepthDiff);
    EXPECT_EQ(n.symbol_lower, "btcusdt");
    ASSERT_TRUE(parse_stream_name("btcusdt@depth", n));
    EXPECT_EQ(n.kind, StreamKind::DepthDiff);
    ASSERT_TRUE(parse_stream_name("ethusdt@depth5@100ms", n));
    EXPECT_EQ(n.kind, StreamKind::Depth5);
    EXPECT_EQ(n.symbol_lower, "ethusdt");
    ASSERT_TRUE(parse_stream_name("btcusdt@trade", n));
    EXPECT_EQ(n.kind, StreamKind::Trade);

    for (const char* bad : {"", "btcusdt", "@trade", "btcusdt@aggTrade", "btcusdt@depth10@100ms",
                            "btcusdt@depth@1000ms", "btcusdt@trades"}) {
        EXPECT_FALSE(parse_stream_name(bad, n)) << bad;
    }
}

TEST(Parser, SpotDepthDiff) {
    Doc d(R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":157,"u":160,)"
          R"("b":[["67000.10","0.5"],["66999.00","0.00000000"]],"a":[["67001.00","1.25"]]})");
    DepthIds ids;
    ASSERT_EQ(parse_depth_ids(d.obj(), Venue::Spot, StreamKind::DepthDiff, ids), ParseStatus::Ok);
    EXPECT_EQ(ids.first, 157u);
    EXPECT_EQ(ids.last, 160u);
    EXPECT_EQ(ids.prev_last, 0u);

    std::vector<Level> bids, asks;
    ASSERT_EQ(parse_depth_levels(d.obj(), Venue::Spot, StreamKind::DepthDiff, bids, asks),
              ParseStatus::Ok);
    EXPECT_EQ(bids, (std::vector<Level>{lvl("67000.10", "0.5"), lvl("66999", "0")}));
    EXPECT_EQ(asks, (std::vector<Level>{lvl("67001", "1.25")}));
}

TEST(Parser, SpotDepth5) {
    Doc d(R"({"lastUpdateId":160,"bids":[["0.0024","10"]],"asks":[["0.0026","100"]]})");
    DepthIds ids;
    ASSERT_EQ(parse_depth_ids(d.obj(), Venue::Spot, StreamKind::Depth5, ids), ParseStatus::Ok);
    EXPECT_EQ(ids.last, 160u);
    std::vector<Level> bids, asks;
    ASSERT_EQ(parse_depth_levels(d.obj(), Venue::Spot, StreamKind::Depth5, bids, asks),
              ParseStatus::Ok);
    EXPECT_EQ(bids, (std::vector<Level>{lvl("0.0024", "10")}));
    EXPECT_EQ(asks, (std::vector<Level>{lvl("0.0026", "100")}));
}

TEST(Parser, UsdmDepthDiffAndDepth5) {
    for (const StreamKind kind : {StreamKind::DepthDiff, StreamKind::Depth5}) {
        Doc d(R"({"e":"depthUpdate","E":1,"T":1,"s":"BTCUSDT","U":390,"u":400,"pu":389,)"
              R"("b":[["7403.89","0.002"]],"a":[["7405.96","3.340"]]})");
        DepthIds ids;
        ASSERT_EQ(parse_depth_ids(d.obj(), Venue::UsdM, kind, ids), ParseStatus::Ok);
        EXPECT_EQ(ids.first, 390u);
        EXPECT_EQ(ids.last, 400u);
        EXPECT_EQ(ids.prev_last, 389u);
        std::vector<Level> bids, asks;
        ASSERT_EQ(parse_depth_levels(d.obj(), Venue::UsdM, kind, bids, asks), ParseStatus::Ok);
        EXPECT_EQ(bids, (std::vector<Level>{lvl("7403.89", "0.002")}));
        EXPECT_EQ(asks, (std::vector<Level>{lvl("7405.96", "3.34")}));
    }
}

TEST(Parser, Trade) {
    Doc d(R"({"e":"trade","E":1,"s":"BTCUSDT","t":12345,"p":"0.001","q":"100","T":1,)"
          R"("m":true,"M":true})");
    bool buyer_is_maker = false;
    ASSERT_EQ(parse_trade(d.obj(), buyer_is_maker), ParseStatus::Ok);
    EXPECT_TRUE(buyer_is_maker);
}

TEST(Parser, MalformedPayloads) {
    DepthIds ids;
    std::vector<Level> bids, asks;
    {
        Doc d(R"({"U":1,"b":[],"a":[]})");  // missing u
        EXPECT_EQ(parse_depth_ids(d.obj(), Venue::Spot, StreamKind::DepthDiff, ids),
                  ParseStatus::BadJson);
    }
    {
        Doc d(R"({"U":1,"u":2,"b":[]})");  // spot diff missing pu is fine, usdm is not
        EXPECT_EQ(parse_depth_ids(d.obj(), Venue::UsdM, StreamKind::DepthDiff, ids),
                  ParseStatus::BadJson);
    }
    {
        Doc d(R"({"U":5,"u":2,"b":[],"a":[]})");  // U > u
        EXPECT_EQ(parse_depth_ids(d.obj(), Venue::Spot, StreamKind::DepthDiff, ids),
                  ParseStatus::BadJson);
    }
    {
        Doc d(R"({"b":[["1.0","2.0","3.0"]],"a":[]})");  // 3-element level
        EXPECT_EQ(parse_depth_levels(d.obj(), Venue::Spot, StreamKind::DepthDiff, bids, asks),
                  ParseStatus::BadJson);
    }
    {
        Doc d(R"({"b":[[1.0,"2.0"]],"a":[]})");  // number instead of string
        EXPECT_EQ(parse_depth_levels(d.obj(), Venue::Spot, StreamKind::DepthDiff, bids, asks),
                  ParseStatus::BadJson);
    }
    {
        Doc d(R"({"b":[["1e5","2.0"]],"a":[]})");
        EXPECT_EQ(parse_depth_levels(d.obj(), Venue::Spot, StreamKind::DepthDiff, bids, asks),
                  ParseStatus::BadDecimal);
    }
    {
        Doc d(R"({"e":"trade","m":"yes"})");
        bool m = false;
        EXPECT_EQ(parse_trade(d.obj(), m), ParseStatus::BadJson);
    }
}

}  // namespace
}  // namespace blc
