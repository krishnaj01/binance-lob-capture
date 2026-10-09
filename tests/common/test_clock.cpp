#include "common/Clock.hpp"

#include <gtest/gtest.h>

namespace blc {
namespace {

TEST(Clock, UtcDate) {
    EXPECT_EQ(utc_date(0), "1970-01-01");
    EXPECT_EQ(utc_date(1'736'899'200), "2025-01-15");       // 2025-01-15T00:00:00Z
    EXPECT_EQ(utc_date(1'736'985'599), "2025-01-15");       // 23:59:59 same day
    EXPECT_EQ(utc_date(1'736'985'600), "2025-01-16");
}

TEST(Clock, WallClockIsSaneAndNormalised) {
    const Timestamp t = wall_clock_now();
    EXPECT_GT(t.sec, 1'700'000'000);  // after Nov 2023
    EXPECT_GE(t.nsec, 0);
    EXPECT_LE(t.nsec, 999'999'999);
}

TEST(Clock, TotalNs) {
    EXPECT_EQ((Timestamp{2, 5}).total_ns(), 2'000'000'005);
}

}  // namespace
}  // namespace blc
