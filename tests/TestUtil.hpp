#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "common/Decimal.hpp"
#include "common/Types.hpp"

namespace blc::test {

// Scaled integer from a decimal literal, e.g. px("100.25").
inline std::int64_t px(std::string_view s) {
    const auto v = parse_decimal(s);
    if (!v) throw std::invalid_argument("bad test decimal");
    return *v;
}

inline Level lvl(std::string_view price, std::string_view qty) { return {px(price), px(qty)}; }

inline std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Fresh, empty per-test directory under gtest's temp dir.
inline std::filesystem::path fresh_dir(const std::string& name) {
    const auto dir = std::filesystem::path(::testing::TempDir()) / ("blc_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace blc::test
