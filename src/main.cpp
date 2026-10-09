#include <exception>
#include <iostream>

#include "app/LiveCapture.hpp"
#include "cli/Options.hpp"
#include "common/Metrics.hpp"
#include "replay/Replay.hpp"

int main(int argc, char** argv) {
    try {
        const auto opts = blc::parse_options(argc, argv);
        if (!opts) return 0;  // --help

        blc::Metrics metrics;
        if (opts->replay()) {
            const auto result = blc::run_replay(opts->replay_file, opts->output_dir, metrics);
            std::cerr << "replay: " << result.rows_read << " market rows -> "
                      << result.orderbook_path << '\n';
            metrics.report(std::cerr);
            return 0;
        }
        return blc::run_live_capture(*opts, metrics);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
