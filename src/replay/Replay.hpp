#pragma once

#include <cstdint>
#include <string>

#include "common/Metrics.hpp"

namespace blc {

struct ReplayResult {
    std::string orderbook_path;
    std::uint64_t rows_read = 0;
};

// Regenerates the order book CSV from a market-data CSV, with no network access.
//
// The input must be one file produced by live capture (exact header, one venue + symbol).
// Output: <output_dir>/<input stem>_orderbook.csv, byte-identical to the order book file
// written during the live capture. Malformed input throws (with the line number) rather
// than producing a silently different book.
ReplayResult run_replay(const std::string& market_csv, const std::string& output_dir,
                        Metrics& metrics);

}  // namespace blc
