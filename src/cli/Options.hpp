#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/Types.hpp"

namespace blc {

struct Options {
    // Sanity limits so a typo cannot create an absurd URL or thousands of files.
    static constexpr std::size_t kMaxSymbols = 100;
    static constexpr std::size_t kMaxSymbolLength = 20;

    Venue venue = Venue::Spot;
    std::vector<std::string> symbols;  // uppercase, de-duplicated, in the given order
    std::size_t symbols_per_shard = 20;  // symbols per WebSocket connection
    std::string output_dir = "./output";
    std::uint32_t duration_sec = 0;          // 0 = run until SIGINT/SIGTERM
    std::uint32_t metrics_interval_sec = 10;  // 0 = only report at exit
    std::string replay_file;                 // non-empty = replay mode
    std::uint32_t debug_disconnect_sec = 0;   // testing: drop connections every N s (0 = off)

    bool replay() const noexcept { return !replay_file.empty(); }
};

// Parses argv. Returns std::nullopt after printing help for --help.
// Throws std::invalid_argument with a user-facing message on invalid input.
std::optional<Options> parse_options(int argc, char** argv);

}  // namespace blc
