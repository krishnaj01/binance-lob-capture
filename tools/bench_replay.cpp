// Replay throughput benchmark: runs the full replay pipeline (CSV parse -> JSON parse ->
// book update -> order book CSV) over a captured market-data file N times.
//
//   ./build/bench_replay <market_data_*.csv> [iterations] [output_dir]

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "common/Metrics.hpp"
#include "replay/Replay.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <market_data.csv> [iterations] [output_dir]\n";
        return 2;
    }
    const std::string input = argv[1];
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 50;
    const std::string out_dir =
        argc > 3 ? argv[3] : (std::filesystem::temp_directory_path() / "blc_bench").string();

    try {
        blc::Metrics warmup_metrics;
        blc::run_replay(input, out_dir, warmup_metrics);  // warm page cache

        std::uint64_t rows = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) {
            blc::Metrics metrics;
            rows += blc::run_replay(input, out_dir, metrics).rows_read;
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

        const auto bytes = std::filesystem::file_size(input) * static_cast<std::uintmax_t>(iterations);
        std::cout << "messages:        " << rows << '\n'
                  << "elapsed:         " << elapsed.count() << " s\n"
                  << "throughput:      " << static_cast<double>(rows) / elapsed.count() / 1e6
                  << " M msgs/s\n"
                  << "per message:     " << elapsed.count() * 1e9 / static_cast<double>(rows)
                  << " ns\n"
                  << "input bandwidth: "
                  << static_cast<double>(bytes) / elapsed.count() / (1024.0 * 1024.0) << " MiB/s\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
