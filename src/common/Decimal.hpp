#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace blc {

// Fixed-point scale applied to every Binance price and quantity: value * 10^8.
// Binance publishes at most 8 decimals, so this is lossless. Max representable integer
// part is INT64_MAX / 1e8 ~= 92'233'720'368, far above any real price or size.
inline constexpr int kScaleDigits = 8;
inline constexpr std::int64_t kScale = 100'000'000;

// Parses a non-negative decimal string ("67012.34000000") into a scaled integer
// (6701234000000) without going through floating point.
//
// Accepted grammar: DIGITS [ "." DIGITS ]. Digits beyond the 8th decimal are accepted
// only if they are all '0' (lossless); anything else, a sign, an empty string,
// whitespace or an overflow returns std::nullopt. Callers count failures in metrics.
constexpr std::optional<std::int64_t> parse_decimal(std::string_view s) noexcept {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

    std::size_t i = 0;
    const std::size_t n = s.size();

    std::int64_t int_part = 0;
    const std::size_t int_begin = i;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; ++i) {
        const auto digit = static_cast<std::int64_t>(s[i] - '0');
        if (int_part > (kMax / kScale - digit) / 10) return std::nullopt;
        int_part = int_part * 10 + digit;
    }
    if (i == int_begin) return std::nullopt;  // need at least one integer digit

    std::int64_t frac = 0;
    int frac_digits = 0;
    if (i < n && s[i] == '.') {
        ++i;
        const std::size_t frac_begin = i;
        for (; i < n && s[i] >= '0' && s[i] <= '9'; ++i) {
            if (frac_digits < kScaleDigits) {
                frac = frac * 10 + static_cast<std::int64_t>(s[i] - '0');
                ++frac_digits;
            } else if (s[i] != '0') {
                return std::nullopt;  // would lose precision
            }
        }
        if (i == frac_begin) return std::nullopt;  // "5." is rejected
    }
    if (i != n) return std::nullopt;  // trailing garbage

    for (int d = frac_digits; d < kScaleDigits; ++d) frac *= 10;

    const std::int64_t scaled_int = int_part * kScale;
    if (scaled_int > kMax - frac) return std::nullopt;
    return scaled_int + frac;
}

}  // namespace blc
