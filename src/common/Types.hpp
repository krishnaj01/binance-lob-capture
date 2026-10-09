#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace blc {

enum class Venue : std::uint8_t { Spot, UsdM };

enum class StreamKind : std::uint8_t { DepthDiff, Depth5, Trade };

enum class Side : std::uint8_t { Bid, Ask };

// One price level, both fields scaled by kScale (see common/Decimal.hpp).
struct Level {
    std::int64_t price = 0;
    std::int64_t qty = 0;

    friend constexpr bool operator==(const Level& a, const Level& b) noexcept {
        return a.price == b.price && a.qty == b.qty;
    }
};

// CSV spellings (market data `venue` / `stream_kind` columns, file names).
constexpr std::string_view to_string(Venue v) noexcept {
    return v == Venue::Spot ? "spot" : "usdm";
}

constexpr std::string_view to_string(StreamKind k) noexcept {
    switch (k) {
        case StreamKind::DepthDiff: return "depth_diff";
        case StreamKind::Depth5: return "depth5";
        case StreamKind::Trade: return "trade";
    }
    return "?";
}

constexpr std::optional<Venue> parse_venue(std::string_view s) noexcept {
    if (s == "spot") return Venue::Spot;
    if (s == "usdm") return Venue::UsdM;
    return std::nullopt;
}

constexpr std::optional<StreamKind> parse_stream_kind(std::string_view s) noexcept {
    if (s == "depth_diff") return StreamKind::DepthDiff;
    if (s == "depth5") return StreamKind::Depth5;
    if (s == "trade") return StreamKind::Trade;
    return std::nullopt;
}

}  // namespace blc
