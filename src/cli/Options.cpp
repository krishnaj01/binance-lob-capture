#include "cli/Options.hpp"

#include <algorithm>
#include <boost/program_options.hpp>
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace blc {

namespace po = boost::program_options;

namespace {

std::vector<std::string> parse_symbol_list(const std::string& csv) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= csv.size()) {
        const std::size_t comma = std::min(csv.find(',', start), csv.size());
        std::string sym = csv.substr(start, comma - start);
        start = comma + 1;
        if (sym.empty()) continue;
        if (sym.size() > Options::kMaxSymbolLength) {
            throw std::invalid_argument("symbol too long: '" + sym + "'");
        }
        for (char& c : sym) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                throw std::invalid_argument("symbol must be alphanumeric: '" + sym + "'");
            }
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (std::find(out.begin(), out.end(), sym) == out.end()) out.push_back(std::move(sym));
    }
    if (out.size() > Options::kMaxSymbols) {
        throw std::invalid_argument("too many symbols (max " +
                                    std::to_string(Options::kMaxSymbols) + ")");
    }
    return out;
}

}  // namespace

std::optional<Options> parse_options(int argc, char** argv) {
    Options opts;
    std::string venue = "spot";
    std::string symbols;

    po::options_description desc(
        "binance_capture: Binance WebSocket capture + local order book\n\n"
        "Live:   binance_capture --venue spot --symbols BTCUSDT[,ETHUSDT] [--duration 120]\n"
        "Replay: binance_capture --replay market_data_spot_BTCUSDT_<date>.csv\n\nOptions");
    desc.add_options()
        ("help,h", "show this help")
        ("venue", po::value(&venue)->default_value(venue), "spot | usdm")
        ("symbols", po::value(&symbols), "comma-separated symbols, e.g. BTCUSDT,ETHUSDT")
        ("symbols-per-shard", po::value(&opts.symbols_per_shard)->default_value(20),
         "symbols per WebSocket connection (3 streams each; max 66)")
        ("output-dir", po::value(&opts.output_dir)->default_value(opts.output_dir),
         "directory for CSV outputs (created if missing)")
        ("duration", po::value(&opts.duration_sec)->default_value(0),
         "live capture length in seconds (0 = until Ctrl+C)")
        ("metrics-interval", po::value(&opts.metrics_interval_sec)->default_value(10),
         "seconds between metrics reports on stderr (0 = only at exit)")
        ("busy-poll", po::bool_switch(&opts.busy_poll),
         "processor thread spins when idle (lowest latency, burns one core)")
        ("replay", po::value(&opts.replay_file),
         "regenerate the order book CSV from this market-data CSV (no network)")
        ("debug-disconnect-every", po::value(&opts.debug_disconnect_sec)->default_value(0),
         "testing: drop every connection every N seconds to exercise reconnects (0 = off)");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        throw std::invalid_argument(e.what());
    }

    if (vm.count("help")) {
        std::cout << desc << '\n';
        return std::nullopt;
    }

    const auto v = parse_venue(venue);
    if (!v) throw std::invalid_argument("--venue must be 'spot' or 'usdm'");
    opts.venue = *v;

    if (opts.replay()) {
        if (vm.count("symbols")) {
            throw std::invalid_argument("--symbols is not used with --replay (read from the file)");
        }
        return opts;
    }

    opts.symbols = parse_symbol_list(symbols);
    if (opts.symbols.empty()) throw std::invalid_argument("--symbols is required for live capture");
    if (opts.symbols_per_shard == 0 || opts.symbols_per_shard > 66) {
        throw std::invalid_argument("--symbols-per-shard must be in [1, 66]");
    }
    return opts;
}

}  // namespace blc
