#include "md/Parser.hpp"

#include "common/Decimal.hpp"

namespace blc {

namespace ondemand = simdjson::ondemand;

namespace {

constexpr bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.substr(0, prefix.size()) == prefix;
}

// [["price","qty"], ...] -> levels. Each entry must have exactly two decimal strings.
ParseStatus parse_level_array(ondemand::value value, std::vector<Level>& out) noexcept {
    out.clear();
    ondemand::array levels;
    if (value.get_array().get(levels)) return ParseStatus::BadJson;
    for (auto entry : levels) {
        ondemand::array pair;
        if (entry.get_array().get(pair)) return ParseStatus::BadJson;
        std::string_view fields[2];
        std::size_t n = 0;
        for (auto element : pair) {
            if (n == 2) return ParseStatus::BadJson;
            if (element.get_string().get(fields[n])) return ParseStatus::BadJson;
            ++n;
        }
        if (n != 2) return ParseStatus::BadJson;
        const auto price = parse_decimal(fields[0]);
        const auto qty = parse_decimal(fields[1]);
        if (!price || !qty) return ParseStatus::BadDecimal;
        out.push_back(Level{*price, *qty});
    }
    return ParseStatus::Ok;
}

}  // namespace

bool parse_stream_name(std::string_view stream, StreamName& out) noexcept {
    const auto at = stream.find('@');
    if (at == 0 || at == std::string_view::npos) return false;
    const std::string_view suffix = stream.substr(at + 1);
    if (suffix == "depth" || suffix == "depth@100ms") {
        out.kind = StreamKind::DepthDiff;
    } else if (suffix == "depth5" || starts_with(suffix, "depth5@")) {
        out.kind = StreamKind::Depth5;
    } else if (suffix == "trade") {
        out.kind = StreamKind::Trade;
    } else {
        return false;
    }
    out.symbol_lower = stream.substr(0, at);
    return true;
}

ParseStatus parse_depth_ids(ondemand::object& data, Venue venue, StreamKind kind,
                            DepthIds& out) noexcept {
    out = DepthIds{};
    if (venue == Venue::Spot && kind == StreamKind::Depth5) {
        if (data["lastUpdateId"].get_uint64().get(out.last)) return ParseStatus::BadJson;
        out.first = out.last;
        return ParseStatus::Ok;
    }
    if (data["U"].get_uint64().get(out.first)) return ParseStatus::BadJson;
    if (data["u"].get_uint64().get(out.last)) return ParseStatus::BadJson;
    if (venue == Venue::UsdM && data["pu"].get_uint64().get(out.prev_last)) {
        return ParseStatus::BadJson;
    }
    if (out.first > out.last) return ParseStatus::BadJson;
    return ParseStatus::Ok;
}

ParseStatus parse_depth_levels(ondemand::object& data, Venue venue, StreamKind kind,
                               std::vector<Level>& bids, std::vector<Level>& asks) noexcept {
    const bool long_keys = venue == Venue::Spot && kind == StreamKind::Depth5;
    ondemand::value v;
    if (data[long_keys ? "bids" : "b"].get(v)) return ParseStatus::BadJson;
    if (const auto st = parse_level_array(v, bids); st != ParseStatus::Ok) return st;
    if (data[long_keys ? "asks" : "a"].get(v)) return ParseStatus::BadJson;
    return parse_level_array(v, asks);
}

ParseStatus parse_trade(ondemand::object& data, bool& buyer_is_maker) noexcept {
    if (data["m"].get_bool().get(buyer_is_maker)) return ParseStatus::BadJson;
    return ParseStatus::Ok;
}

}  // namespace blc
