#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace blc {

// A wall-clock instant split the way both CSVs store it: integer seconds plus a
// nanosecond remainder in [0, 999'999'999].
struct Timestamp {
    std::int64_t sec = 0;
    std::int32_t nsec = 0;

    constexpr std::int64_t total_ns() const noexcept {
        return sec * 1'000'000'000LL + nsec;
    }
    friend constexpr bool operator==(const Timestamp& a, const Timestamp& b) noexcept {
        return a.sec == b.sec && a.nsec == b.nsec;
    }
};

// CLOCK_REALTIME (UTC wall clock). Used for recv_tsec/recv_tnsec: taken by the network
// thread immediately after a complete WebSocket frame has been read.
inline Timestamp wall_clock_now() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return {static_cast<std::int64_t>(ts.tv_sec), static_cast<std::int32_t>(ts.tv_nsec)};
}

// "YYYY-MM-DD" for the UTC calendar day containing `epoch_sec` (used in file names).
inline std::string utc_date(std::int64_t epoch_sec) {
    const auto t = static_cast<std::time_t>(epoch_sec);
    std::tm tm{};
    ::gmtime_r(&t, &tm);
    char buf[16];
    const std::size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return std::string(buf, n);
}

}  // namespace blc
