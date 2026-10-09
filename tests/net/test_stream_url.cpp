#include "net/StreamUrl.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace blc {
namespace {

TEST(StreamUrl, SpotCombinedStreams) {
    const Endpoint ep = make_endpoint(Venue::Spot, {"BTCUSDT"});
    EXPECT_EQ(ep.host, "stream.binance.com");
    EXPECT_EQ(ep.port, "9443");
    EXPECT_EQ(ep.target, "/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade");
    EXPECT_EQ(ep.url(),
              "wss://stream.binance.com:9443/stream?streams="
              "btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade");
}

TEST(StreamUrl, UsdmUsesPublicPathAndMultipleSymbols) {
    const Endpoint ep = make_endpoint(Venue::UsdM, {"BTCUSDT", "ETHUSDT"});
    EXPECT_EQ(ep.host, "fstream.binance.com");
    EXPECT_EQ(ep.port, "443");
    EXPECT_EQ(ep.target,
              "/public/stream?streams=btcusdt@depth@100ms/btcusdt@depth5@100ms/btcusdt@trade/"
              "ethusdt@depth@100ms/ethusdt@depth5@100ms/ethusdt@trade");
}

TEST(StreamUrl, Limits) {
    EXPECT_THROW(make_endpoint(Venue::Spot, {}), std::invalid_argument);
    const std::vector<std::string> too_many(kMaxSymbolsPerShard + 1, "BTCUSDT");
    EXPECT_THROW(make_endpoint(Venue::Spot, too_many), std::invalid_argument);
}

TEST(StreamUrl, Sharding) {
    const std::vector<std::string> syms = {"A", "B", "C", "D", "E"};
    const auto shards = shard_symbols(syms, 2);
    ASSERT_EQ(shards.size(), 3u);
    EXPECT_EQ(shards[0], (std::vector<std::string>{"A", "B"}));
    EXPECT_EQ(shards[2], (std::vector<std::string>{"E"}));
    EXPECT_EQ(shard_symbols(syms, 20).size(), 1u);
    EXPECT_THROW(shard_symbols(syms, 0), std::invalid_argument);
    EXPECT_THROW(shard_symbols(syms, kMaxSymbolsPerShard + 1), std::invalid_argument);
}

}  // namespace
}  // namespace blc
