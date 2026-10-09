#include "replay/Replay.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "TestUtil.hpp"
#include "book/OrderBookSink.hpp"
#include "io/CsvReader.hpp"

namespace blc {
namespace {

namespace fs = std::filesystem;

const fs::path kFixtures = BLC_TEST_FIXTURES_DIR;

// Hand-written scenario covering: diff before snapshot, seed, straddling diff, qty-0
// removal, stale diff, trade annotation, gap -> reset, unsynced diff, thin re-seed,
// stale snapshot, new conn_epoch -> reset, bad decimal -> reset.
// The expected file was derived independently (not produced by this program).
TEST(Replay, GoldenSpotScenario) {
    const auto out_dir = test::fresh_dir("replay_golden");
    Metrics metrics;
    const auto result =
        run_replay((kFixtures / "market_data_spot_BTCUSDT_2025-01-15.csv").string(),
                   out_dir.string(), metrics);

    EXPECT_EQ(result.rows_read, 13u);
    EXPECT_EQ(fs::path(result.orderbook_path).filename(),
              "market_data_spot_BTCUSDT_2025-01-15_orderbook.csv");
    EXPECT_EQ(test::slurp(result.orderbook_path),
              test::slurp(kFixtures / "market_data_spot_BTCUSDT_2025-01-15_orderbook.expected.csv"));

    EXPECT_EQ(metrics.messages_processed.load(), 13u);
    EXPECT_EQ(metrics.depth_diffs_applied.load(), 2u);
    EXPECT_EQ(metrics.depth_diffs_stale.load(), 1u);
    EXPECT_EQ(metrics.depth_diffs_unsynced.load(), 3u);
    EXPECT_EQ(metrics.depth5_applied.load(), 3u);
    EXPECT_EQ(metrics.depth5_stale.load(), 1u);
    EXPECT_EQ(metrics.trades.load(), 1u);
    EXPECT_EQ(metrics.sequence_gaps.load(), 1u);
    EXPECT_EQ(metrics.decimal_errors.load(), 1u);
    EXPECT_EQ(metrics.book_rows_written.load(), 9u);
}

TEST(Replay, RejectsBadInput) {
    const auto dir = test::fresh_dir("replay_bad");
    Metrics metrics;
    const auto write = [&](const std::string& name, const std::string& content) {
        const auto p = (dir / name).string();
        std::ofstream(p, std::ios::binary) << content;
        return p;
    };
    const std::string header =
        "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,payload_json\n";

    EXPECT_THROW(run_replay(write("a.csv", "wrong,header\n"), dir.string(), metrics),
                 CsvParseError);
    EXPECT_THROW(run_replay(write("b.csv", header + "1,2,spot,depth5,0,0,0,BTCUSDT\n"),
                            dir.string(), metrics),
                 CsvParseError);
    EXPECT_THROW(run_replay(write("c.csv", header + "1,2,mars,depth5,0,0,0,BTCUSDT,{}\n"),
                            dir.string(), metrics),
                 CsvParseError);
    EXPECT_THROW(run_replay(write("d.csv", header + "1,2000000000,spot,depth5,0,0,0,X,{}\n"),
                            dir.string(), metrics),
                 CsvParseError);
    EXPECT_THROW(run_replay(write("e.csv", header + "1,2,spot,trade,0,0,0,AAA,{}\n" +
                                               "1,2,spot,trade,0,0,1,BBB,{}\n"),
                            dir.string(), metrics),
                 CsvParseError);
    EXPECT_THROW(run_replay((dir / "missing.csv").string(), dir.string(), metrics),
                 std::runtime_error);
}

TEST(Replay, HeaderOnlyInputYieldsHeaderOnlyOutput) {
    const auto dir = test::fresh_dir("replay_empty");
    const auto in = (dir / "market_data_spot_X_2025-01-15.csv").string();
    std::ofstream(in, std::ios::binary)
        << "recv_tsec,recv_tnsec,venue,stream_kind,shard_id,conn_epoch,conn_seq,symbol,"
           "payload_json\n";
    Metrics metrics;
    const auto result = run_replay(in, (dir / "out").string(), metrics);
    EXPECT_EQ(result.rows_read, 0u);
    EXPECT_EQ(test::slurp(result.orderbook_path), std::string(OrderBookSink::kHeader) + "\n");
}

}  // namespace
}  // namespace blc
