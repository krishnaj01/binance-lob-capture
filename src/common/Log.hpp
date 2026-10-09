#pragma once

#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>

#include "common/Clock.hpp"

namespace blc {

// Minimal thread-safe stderr logger for operational events (connects, reconnects,
// errors, metrics). Never called per market-data message.
class Log {
public:
    template <typename... Args>
    static void info(const Args&... args) {
        write("INFO", args...);
    }
    template <typename... Args>
    static void warn(const Args&... args) {
        write("WARN", args...);
    }
    template <typename... Args>
    static void error(const Args&... args) {
        write("ERROR", args...);
    }

private:
    template <typename... Args>
    static void write(std::string_view level, const Args&... args) {
        const Timestamp now = wall_clock_now();
        const auto t = static_cast<std::time_t>(now.sec);
        std::tm tm{};
        ::gmtime_r(&t, &tm);
        char ts[32];
        const std::size_t n = std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm);

        std::ostringstream line;
        line.write(ts, static_cast<std::streamsize>(n));
        const int ms = now.nsec / 1'000'000;
        const char frac[] = {'.', static_cast<char>('0' + ms / 100),
                             static_cast<char>('0' + ms / 10 % 10),
                             static_cast<char>('0' + ms % 10), 'Z', ' ', '\0'};
        line << frac << level << ' ';
        (line << ... << args);
        line << '\n';

        static std::mutex mu;
        const std::lock_guard<std::mutex> lock(mu);
        std::cerr << line.str() << std::flush;
    }
};

}  // namespace blc
