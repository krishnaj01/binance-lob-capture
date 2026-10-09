#include "common/Decimal.hpp"

#include <gtest/gtest.h>

namespace blc {
namespace {

// Compile-time evaluation: the parser is constexpr.
static_assert(parse_decimal("1.5").value() == 150'000'000);
static_assert(!parse_decimal("-1").has_value());

TEST(Decimal, TypicalBinancePrices) {
    EXPECT_EQ(parse_decimal("67012.34000000"), 6'701'234'000'000);
    EXPECT_EQ(parse_decimal("67012.34"), 6'701'234'000'000);
    EXPECT_EQ(parse_decimal("0.00012000"), 12'000);
}

TEST(Decimal, ZeroForms) {
    EXPECT_EQ(parse_decimal("0"), 0);
    EXPECT_EQ(parse_decimal("0.0"), 0);
    EXPECT_EQ(parse_decimal("0.00000000"), 0);  // Binance "remove level" quantity
    EXPECT_EQ(parse_decimal("000.5"), 50'000'000);
}

TEST(Decimal, SmallestUnit) {
    EXPECT_EQ(parse_decimal("0.00000001"), 1);
}

TEST(Decimal, IntegersWithoutFraction) {
    EXPECT_EQ(parse_decimal("1"), kScale);
    EXPECT_EQ(parse_decimal("123456"), 123456 * kScale);
}

TEST(Decimal, ExtraDecimalsOnlyIfZero) {
    EXPECT_EQ(parse_decimal("1.123456780000"), 112'345'678);
    EXPECT_FALSE(parse_decimal("1.123456789").has_value());
    EXPECT_FALSE(parse_decimal("0.000000001").has_value());
}

TEST(Decimal, LargestValues) {
    // INT64_MAX = 9223372036854775807 -> 92233720368.54775807 is the exact maximum.
    EXPECT_EQ(parse_decimal("92233720368.54775807"), std::numeric_limits<std::int64_t>::max());
    EXPECT_FALSE(parse_decimal("92233720368.54775808").has_value());
    EXPECT_FALSE(parse_decimal("92233720369").has_value());
    EXPECT_FALSE(parse_decimal("99999999999999999999999").has_value());
}

TEST(Decimal, RejectsMalformedInput) {
    for (const char* bad : {"", ".", ".5", "5.", "-1", "+1", " 1", "1 ", "1.2.3", "1e5", "abc",
                            "1,5", "0x10", "NaN"}) {
        EXPECT_FALSE(parse_decimal(bad).has_value()) << "input: '" << bad << "'";
    }
}

}  // namespace
}  // namespace blc
