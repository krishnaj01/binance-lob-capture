#include "pipeline/Processor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "TestUtil.hpp"
#include "pipeline/OutputFiles.hpp"
#include "replay/Replay.hpp"

namespace blc {
namespace {

using test::slurp;

constexpr const char* kSeed =
    R"({ "lastUpdateId" : 100, "bids" : [["100","1"],["99","1"],["98","1"],["97","1"],["96","1"]],)"
    R"( "asks" : [["101","1"],["102","1"],["103","1"],["104","1"],["105","1"]] })";

RawFrame frame(std::uint64_t epoch, std::uint64_t seq, std::string_view stream,
               std::string_view data) {
    RawFrame f;
    f.recv = Timestamp{1'736'899'200, static_cast<std::int32_t>(seq + 1)};
    f.conn_epoch = epoch;
    f.conn_seq = seq;
    f.payload = std::string(R"({"stream":")") + std::string(stream) + R"(","data":)" +
                std::string(data) + "}";
    return f;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t nl; (nl = text.find('\n', start)) != std::string::npos; start = nl + 1) {
        out.push_back(text.substr(start, nl - start));
    }
    return out;
}

class ProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = test::fresh_dir("processor");
        market_path_ = market_data_path(dir_.string(), Venue::Spot, "BTCUSDT", "2025-01-15");
        book_path_ = orderbook_path_for(dir_.string(), market_path_);
    }

    std::filesystem::path dir_;
    std::string market_path_;
    std::string book_path_;
};

TEST_F(ProcessorTest, LiveCaptureWritesBothFilesAndReplayReproducesBook) {
    Metrics metrics;
    {
        Processor p(metrics);
        p.add_symbol(Venue::Spot, "BTCUSDT", std::make_unique<MarketDataSink>(market_path_),
                     std::make_unique<OrderBookSink>(book_path_));
        const std::vector<RawFrame> frames = {
            frame(0, 0, "btcusdt@depth5@100ms", kSeed),  // seed -> S
            frame(0, 1, "btcusdt@depth@100ms",           // apply -> D/B
                  R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":101,"u":102,"b":[["100","0"]],"a":[]})"),
            frame(0, 2, "btcusdt@trade", R"({"e":"trade","s":"BTCUSDT","t":1,"m":true})"),  // T/S
            frame(0, 3, "btcusdt@depth@100ms",  // gap (expected U <= 103) -> R
                  R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":105,"u":106,"b":[],"a":[]})"),
            frame(0, 4, "ethusdt@trade", R"({"e":"trade","m":true})"),  // unknown symbol
            frame(0, 5, "btcusdt@depth5@100ms", R"(not json)"),        // malformed
            frame(0, 6, "btcusdt@depth5@100ms", kSeed),                // re-seed -> S
            frame(1, 0, "btcusdt@depth@100ms",  // new conn_epoch -> R, then unsynced
                  R"({"e":"depthUpdate","E":1,"s":"BTCUSDT","U":111,"u":112,"b":[],"a":[]})"),
        };
        for (const auto& f : frames) p.on_frame(Venue::Spot, f);
    }  // destructors flush

    const auto market = lines(slurp(market_path_));
    ASSERT_EQ(market.size(), 7u);  // header + 6 (unknown symbol and bad JSON are not rows)
    EXPECT_EQ(market[0],
              "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,"
              "payload_json");
    // payload is the minified inner `data`, CSV-quoted.
    EXPECT_EQ(market[1],
              R"(1736899200,1,spot,depth5,0,0,0,BTCUSDT,"{""lastUpdateId"":100,""bids"":)"
              R"([[""100"",""1""],[""99"",""1""],[""98"",""1""],[""97"",""1""],[""96"",""1""]],)"
              R"(""asks"":[[""101"",""1""],[""102"",""1""],[""103"",""1""],[""104"",""1""],)"
              R"([""105"",""1""]]}")");
    EXPECT_TRUE(starts_with(market[6], "1736899200,1,spot,depth_diff,0,1,0,BTCUSDT,\""));

    const auto book = lines(slurp(book_path_));
    ASSERT_EQ(book.size(), 7u);  // header + S, D, T, R, S, R
    EXPECT_EQ(book[0], std::string(OrderBookSink::kHeader));
    const std::string id = std::to_string(instrument_id(Venue::Spot, "BTCUSDT"));
    const std::string empty_book = "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
    EXPECT_TRUE(starts_with(book[1], "1736899200,1,1," + id + ",S,N,10000000000,"));
    // Level 100 removed by qty 0: remaining bids shift up, slot 4 is empty.
    EXPECT_EQ(book[2], "1736899200,2,2," + id +
                           ",D,B,9900000000,9800000000,9700000000,9600000000,0,"
                           "100000000,100000000,100000000,100000000,0,"
                           "10100000000,10200000000,10300000000,10400000000,10500000000,"
                           "100000000,100000000,100000000,100000000,100000000");
    EXPECT_TRUE(starts_with(book[3], "1736899200,3,3," + id + ",T,S,9900000000,"));
    EXPECT_EQ(book[4], "1736899200,4,4," + id + ",R,N," + empty_book);
    EXPECT_TRUE(starts_with(book[5], "1736899200,7,5," + id + ",S,N,10000000000,"));
    EXPECT_EQ(book[6], "1736899200,1,6," + id + ",R,N," + empty_book);
    for (const auto& row : book) {
        EXPECT_EQ(std::count(row.begin(), row.end(), ','), 25) << row;
    }

    EXPECT_EQ(metrics.parse_errors.load(), 2u);
    EXPECT_EQ(metrics.sequence_gaps.load(), 1u);
    EXPECT_EQ(metrics.depth_diffs_unsynced.load(), 1u);

    // Replay of the captured market data must reproduce the order book byte for byte.
    const auto replay_dir = test::fresh_dir("processor_replay");
    Metrics replay_metrics;
    const auto result = run_replay(market_path_, replay_dir.string(), replay_metrics);
    EXPECT_EQ(result.rows_read, 6u);
    EXPECT_EQ(std::filesystem::path(result.orderbook_path).filename(),
              std::filesystem::path(book_path_).filename());
    EXPECT_EQ(slurp(result.orderbook_path), slurp(book_path_));
}

}  // namespace
}  // namespace blc
