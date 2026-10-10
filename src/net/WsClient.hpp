#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "common/Metrics.hpp"
#include "md/FrameChannel.hpp"
#include "md/Message.hpp"
#include "net/StreamUrl.hpp"

namespace blc {

// One TLS WebSocket connection ("shard") to a Binance combined-stream endpoint.
//
// Runs entirely on the io_context's thread (all handlers execute there; no locking).
// Every complete text frame is timestamped, copied into a recycled RawFrame buffer and
// pushed to the processing thread through the FrameChannel. The network thread never
// blocks on the processor: if the queue is full the frame is dropped and counted (the
// processor will then see an update-id gap and resync the book).
//
// Lifecycle: start() -> resolve -> TCP connect -> TLS handshake (peer certificate and
// host name verified) -> WebSocket handshake -> read loop. Any failure closes the
// connection and schedules a reconnect with exponential backoff (0.5 s doubling to 30 s,
// reset once a frame has been received). Each successful handshake starts a new
// conn_epoch (0 for the first connection) and restarts conn_seq at 0.
// stop() closes gracefully and cancels any pending reconnect; `on_stopped` is invoked
// exactly once when the client has no more pending operations.
class WsClient : public std::enable_shared_from_this<WsClient> {
public:
    static constexpr std::size_t kMaxMessageBytes = 1U << 20;  // reject absurd frames

    WsClient(boost::asio::io_context& ioc, boost::asio::ssl::context& ssl_ctx, Endpoint endpoint,
             std::uint32_t shard_id, FrameChannel& channel, Metrics& metrics,
             std::function<void()> on_stopped);

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    void start();
    void stop();

    // Testing aid (--debug-disconnect-every): abruptly closes the TCP socket of an open
    // connection, exercising exactly the path of a real network drop.
    void force_disconnect();

private:
    using Stream = boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    void connect();
    void on_resolve(boost::beast::error_code ec,
                    boost::asio::ip::tcp::resolver::results_type results);
    void on_connect(boost::beast::error_code ec);
    void on_tls_handshake(boost::beast::error_code ec);
    void on_ws_handshake(boost::beast::error_code ec);
    void read_next();
    void on_read(boost::beast::error_code ec, std::size_t bytes);
    void fail(boost::beast::error_code ec, const char* stage);
    void schedule_reconnect();
    void notify_stopped();

    boost::asio::io_context& ioc_;
    boost::asio::ssl::context& ssl_ctx_;
    Endpoint endpoint_;
    std::uint32_t shard_id_;
    FrameChannel& channel_;
    Metrics& metrics_;
    std::function<void()> on_stopped_;

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::steady_timer reconnect_timer_;
    std::optional<Stream> ws_;  // recreated for every connection attempt
    boost::beast::flat_buffer buffer_;
    RawFrame frame_;  // buffer for the next frame, taken from channel_.recycled

    std::uint64_t next_epoch_ = 0;
    std::uint64_t epoch_ = 0;
    std::uint64_t seq_ = 0;
    std::chrono::milliseconds backoff_;
    bool connected_ = false;
    bool stopping_ = false;
    bool stopped_notified_ = false;
};

}  // namespace blc
