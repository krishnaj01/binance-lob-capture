#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "common/Types.hpp"

namespace blc {

// <dir>/market_data_<venue>_<SYMBOL>_<YYYY-MM-DD>.csv  (date = UTC date at process start)
inline std::string market_data_path(const std::string& dir, Venue venue, std::string_view symbol,
                                    const std::string& utc_date) {
    std::string name = "market_data_";
    name.append(to_string(venue)).append("_").append(symbol).append("_").append(utc_date);
    name.append(".csv");
    return (std::filesystem::path(dir) / name).string();
}

// Order book file paired with a market-data file: "<stem>_orderbook.csv" in `dir`.
// Live capture and replay both use it, so their outputs have the same file name.
inline std::string orderbook_path_for(const std::string& dir, const std::string& market_csv) {
    const std::filesystem::path p(market_csv);
    return (std::filesystem::path(dir) / (p.stem().string() + "_orderbook.csv")).string();
}

}  // namespace blc
