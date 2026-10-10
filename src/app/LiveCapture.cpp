#include "app/LiveCapture.hpp"

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <csignal>
#include <exception>
#include <functional>
#include <filesystem>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "common/Clock.hpp"
#include "common/Log.hpp"
#include "md/FrameChannel.hpp"
#include "net/StreamUrl.hpp"
#include "net/WsClient.hpp"
#include "pipeline/OutputFiles.hpp"
#include "pipeline/Processor.hpp"

namespace blc {

namespace net = boost::asio;

namespace {

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

// ~16k frames of headroom (~25 s of a busy symbol) before the network thread drops.
constexpr std::size_t kQueueCapacity = 16 * 1024;

void report_metrics(const Metrics& metrics, std::size_t queue_depth) {
    std::ostringstream os;
    metrics.report(os);
    std::string line = os.str();
    for (char& c : line) {
        if (c == '\n') c = ' ';
    }
    Log::info("metrics: ", line, "queue_depth=", queue_depth);
}

// Processing thread body. By default it idles with a 50 us sleep when the queue is empty:
// market data arrives in 100 ms bursts, and the added wait does not affect the recorded
// receive timestamps (taken on the network thread). --busy-poll spins instead, trading a
// full core for the lowest hand-off latency.
void processor_loop(Processor& processor, Venue venue, FrameChannel& channel,
                    const std::atomic<bool>& stop, Metrics& metrics, bool busy_poll) {
    RawFrame frame;
    const auto process_and_recycle = [&] {
        processor.on_frame(venue, frame);
        const auto latency = wall_clock_now().total_ns() - frame.recv.total_ns();
        if (latency > 0) {
            const auto ns = static_cast<std::uint64_t>(latency);
            Metrics::inc(metrics.pipeline_latency_ns_total, ns);
            Metrics::set_max(metrics.pipeline_latency_ns_max, ns);
        }
        channel.recycled.try_push(frame);  // hand the buffer back (kept here if pool full)
    };
    for (;;) {
        if (channel.filled.try_pop(frame)) {
            process_and_recycle();
            continue;
        }
        if (stop.load(std::memory_order_acquire)) {
            // Producer has finished: everything it pushed is visible now.
            while (channel.filled.try_pop(frame)) process_and_recycle();
            break;
        }
        if (busy_poll) {
            cpu_relax();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    processor.flush();
}

}  // namespace

int run_live_capture(const Options& opts, Metrics& metrics) {
    std::filesystem::create_directories(opts.output_dir);
    const std::string date = utc_date(wall_clock_now().sec);

    // Files are created up front so a bad output directory fails before connecting.
    Processor processor(metrics);
    for (const auto& symbol : opts.symbols) {
        const auto market_path = market_data_path(opts.output_dir, opts.venue, symbol, date);
        const auto book_path = orderbook_path_for(opts.output_dir, market_path);
        processor.add_symbol(opts.venue, symbol, std::make_unique<MarketDataSink>(market_path),
                             std::make_unique<OrderBookSink>(book_path));
        Log::info("writing ", market_path, " and ", book_path);
    }

    net::io_context ioc(1);
    net::ssl::context ssl_ctx(net::ssl::context::tls_client);
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(net::ssl::verify_peer);
    ssl_ctx.set_options(net::ssl::context::default_workarounds | net::ssl::context::no_sslv2 |
                        net::ssl::context::no_sslv3 | net::ssl::context::no_tlsv1 |
                        net::ssl::context::no_tlsv1_1);

    net::signal_set signals(ioc, SIGINT, SIGTERM);
    net::steady_timer duration_timer(ioc);
    net::steady_timer metrics_timer(ioc);
    net::steady_timer disconnect_timer(ioc);

    FrameChannel channel(kQueueCapacity);
    std::vector<std::shared_ptr<WsClient>> clients;
    std::size_t clients_running = 0;
    // Once every client has closed, nothing but the signal wait keeps io_context busy.
    const auto on_client_stopped = [&] {
        if (--clients_running == 0) signals.cancel();
    };
    std::uint32_t shard_id = 0;
    for (const auto& group : shard_symbols(opts.symbols, opts.symbols_per_shard)) {
        clients.push_back(std::make_shared<WsClient>(ioc, ssl_ctx, make_endpoint(opts.venue, group),
                                                     shard_id++, channel, metrics,
                                                     on_client_stopped));
        ++clients_running;
    }

    std::atomic<bool> processor_stop{false};
    std::atomic<bool> processor_failed{false};
    bool stopping = false;

    const auto shutdown = [&](const char* reason) {
        if (stopping) return;
        stopping = true;
        Log::info("shutting down: ", reason);
        duration_timer.cancel();
        metrics_timer.cancel();
        disconnect_timer.cancel();
        for (auto& c : clients) c->stop();
        // Keep listening so a second signal can force an immediate exit.
        signals.async_wait([&ioc](const boost::system::error_code& ec, int) {
            if (!ec) {
                Log::warn("second signal: forcing exit");
                ioc.stop();
            }
        });
    };

    signals.async_wait([&](const boost::system::error_code& ec, int signo) {
        if (!ec) shutdown(signo == SIGINT ? "SIGINT" : "SIGTERM");
    });
    if (opts.duration_sec > 0) {
        duration_timer.expires_after(std::chrono::seconds(opts.duration_sec));
        duration_timer.async_wait([&](const boost::system::error_code& ec) {
            if (!ec) shutdown("duration elapsed");
        });
    }
    std::function<void()> arm_metrics = [&] {
        if (opts.metrics_interval_sec == 0) return;
        metrics_timer.expires_after(std::chrono::seconds(opts.metrics_interval_sec));
        metrics_timer.async_wait([&](const boost::system::error_code& ec) {
            if (ec || stopping) return;
            report_metrics(metrics, channel.filled.size_approx());
            arm_metrics();
        });
    };
    arm_metrics();
    std::function<void()> arm_disconnect = [&] {
        if (opts.debug_disconnect_sec == 0) return;
        disconnect_timer.expires_after(std::chrono::seconds(opts.debug_disconnect_sec));
        disconnect_timer.async_wait([&](const boost::system::error_code& ec) {
            if (ec || stopping) return;
            for (auto& c : clients) c->force_disconnect();
            arm_disconnect();
        });
    };
    arm_disconnect();

    std::thread processor_thread([&] {
        try {
            processor_loop(processor, opts.venue, channel, processor_stop, metrics, opts.busy_poll);
        } catch (const std::exception& e) {
            // Typically an output write failure: stop capturing rather than lose rows.
            Log::error("processor failed: ", e.what());
            processor_failed.store(true);
            net::post(ioc, [&] { shutdown("processor failure"); });
            // Keep draining so the network thread never stalls on a full queue.
            RawFrame discard;
            while (!processor_stop.load(std::memory_order_acquire)) {
                while (channel.filled.try_pop(discard)) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    for (auto& c : clients) c->start();
    ioc.run();  // returns once all clients are closed and timers cancelled (or forced)

    processor_stop.store(true, std::memory_order_release);
    processor_thread.join();
    clients.clear();

    report_metrics(metrics, channel.filled.size_approx());
    return processor_failed.load() ? 1 : 0;
}

}  // namespace blc
