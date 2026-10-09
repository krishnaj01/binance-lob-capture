#include "net/WsClient.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <simdjson.h>

#include <algorithm>
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include "common/Clock.hpp"
#include "common/Log.hpp"

namespace blc {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace {
constexpr std::chrono::milliseconds kInitialBackoff{500};
constexpr std::chrono::milliseconds kMaxBackoff{30'000};
constexpr std::chrono::seconds kConnectTimeout{10};
constexpr std::size_t kInitialFrameCapacity = 16 * 1024;
}  // namespace

WsClient::WsClient(net::io_context& ioc, ssl::context& ssl_ctx, Endpoint endpoint,
                   std::uint32_t shard_id, SpscQueue<RawFrame>& queue, Metrics& metrics,
                   std::function<void()> on_stopped)
    : ioc_(ioc),
      ssl_ctx_(ssl_ctx),
      endpoint_(std::move(endpoint)),
      shard_id_(shard_id),
      queue_(queue),
      metrics_(metrics),
      on_stopped_(std::move(on_stopped)),
      resolver_(ioc),
      reconnect_timer_(ioc),
      backoff_(kInitialBackoff) {
    frame_.payload.reserve(kInitialFrameCapacity);
}

void WsClient::start() {
    Log::info("shard ", shard_id_, ": connecting to ", endpoint_.url());
    connect();
}

// Every pending operation completes with an error after this, and each completion path
// ends in notify_stopped(): read/handshake/connect/resolve via fail(), a pending reconnect
// via the timer handler, an open connection via the close handler.
void WsClient::stop() {
    if (stopping_) return;
    stopping_ = true;
    reconnect_timer_.cancel();
    resolver_.cancel();
    if (ws_ && connected_) {
        ws_->async_close(websocket::close_code::normal,
                         [self = shared_from_this()](beast::error_code) {
                             self->notify_stopped();
                         });
    } else if (ws_) {
        beast::get_lowest_layer(*ws_).cancel();
    }
}

void WsClient::force_disconnect() {
    if (stopping_ || !ws_ || !connected_) return;
    Log::warn("shard ", shard_id_, ": debug: forcing disconnect");
    beast::error_code ignored;
    beast::get_lowest_layer(*ws_).socket().close(ignored);
}

void WsClient::notify_stopped() {
    if (stopped_notified_) return;
    stopped_notified_ = true;
    Log::info("shard ", shard_id_, ": closed");
    if (on_stopped_) on_stopped_();
}

void WsClient::connect() {
    connected_ = false;
    ws_.emplace(ioc_, ssl_ctx_);  // single-threaded io_context: no strand needed
    buffer_.clear();

    // TLS: SNI (required by Binance's front end) + certificate host name verification.
    auto& tls = ws_->next_layer();
    // Equivalent to the SSL_set_tlsext_host_name() macro, minus its C-style cast.
    if (SSL_ctrl(tls.native_handle(), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name,
                 const_cast<char*>(endpoint_.host.c_str())) != 1) {
        fail(beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
             "sni");
        return;
    }
    tls.set_verify_callback(ssl::host_name_verification(endpoint_.host));

    resolver_.async_resolve(endpoint_.host, endpoint_.port,
                            [self = shared_from_this()](beast::error_code ec,
                                                        tcp::resolver::results_type results) {
                                self->on_resolve(ec, std::move(results));
                            });
}

void WsClient::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) return fail(ec, "resolve");
    auto& tcp_layer = beast::get_lowest_layer(*ws_);
    tcp_layer.expires_after(kConnectTimeout);
    tcp_layer.async_connect(results, [self = shared_from_this()](
                                         beast::error_code e, const tcp::endpoint&) {
        self->on_connect(e);
    });
}

void WsClient::on_connect(beast::error_code ec) {
    if (ec) return fail(ec, "connect");
    beast::get_lowest_layer(*ws_).expires_after(kConnectTimeout);
    ws_->next_layer().async_handshake(
        ssl::stream_base::client,
        [self = shared_from_this()](beast::error_code e) { self->on_tls_handshake(e); });
}

void WsClient::on_tls_handshake(beast::error_code ec) {
    if (ec) return fail(ec, "tls handshake");

    // From here on the websocket layer manages timeouts: 10 s handshake, and if nothing
    // arrives for 10 s a ping is sent; no pong / data within 20 s closes the connection.
    beast::get_lowest_layer(*ws_).expires_never();
    websocket::stream_base::timeout timeouts{};
    timeouts.handshake_timeout = kConnectTimeout;
    timeouts.idle_timeout = std::chrono::seconds(20);
    timeouts.keep_alive_pings = true;
    ws_->set_option(timeouts);
    ws_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(beast::http::field::user_agent, "binance-lob-capture/1.0");
    }));
    ws_->read_message_max(kMaxMessageBytes);

    const std::string host_header = endpoint_.host + ":" + endpoint_.port;
    ws_->async_handshake(host_header, endpoint_.target,
                         [self = shared_from_this()](beast::error_code e) {
                             self->on_ws_handshake(e);
                         });
}

void WsClient::on_ws_handshake(beast::error_code ec) {
    if (ec) return fail(ec, "websocket handshake");
    connected_ = true;
    epoch_ = next_epoch_++;
    seq_ = 0;
    if (epoch_ > 0) Metrics::inc(metrics_.reconnects);
    Log::info("shard ", shard_id_, ": connected (conn_epoch ", epoch_, ")");
    if (stopping_) {
        // stop() raced with a handshake that had already succeeded: close it now.
        ws_->async_close(websocket::close_code::normal,
                         [self = shared_from_this()](beast::error_code) {
                             self->notify_stopped();
                         });
        return;
    }
    read_next();
}

void WsClient::read_next() {
    ws_->async_read(buffer_, [self = shared_from_this()](beast::error_code ec, std::size_t n) {
        self->on_read(ec, n);
    });
}

void WsClient::on_read(beast::error_code ec, std::size_t bytes) {
    if (ec) return fail(ec, "read");

    frame_.recv = wall_clock_now();  // as soon as the complete message is available
    backoff_ = kInitialBackoff;      // healthy connection

    Metrics::inc(metrics_.frames_received);
    Metrics::inc(metrics_.bytes_received, bytes);

    if (ws_->got_text()) {
        auto& payload = frame_.payload;
        // Leave room for simdjson's padding so the processor can parse in place.
        if (payload.capacity() < bytes + simdjson::SIMDJSON_PADDING) {
            payload.reserve(bytes + simdjson::SIMDJSON_PADDING);
        }
        const auto data = buffer_.cdata();
        payload.assign(static_cast<const char*>(data.data()), data.size());
        frame_.shard_id = shard_id_;
        frame_.conn_epoch = epoch_;
        frame_.conn_seq = seq_++;
        if (!queue_.try_push(frame_)) Metrics::inc(metrics_.queue_full_drops);
    }
    buffer_.consume(buffer_.size());
    read_next();
}

void WsClient::fail(beast::error_code ec, const char* stage) {
    const bool was_connected = connected_;
    connected_ = false;
    if (stopping_) {
        notify_stopped();
        return;
    }
    if (!was_connected) Metrics::inc(metrics_.connect_failures);
    Log::warn("shard ", shard_id_, ": ", stage, " failed: ", ec.message());
    // Destroy the dead stream now: its keep-alive timer would otherwise stay pending
    // (keeping io_context busy, e.g. delaying shutdown) until the next connect replaces it.
    ws_.reset();
    schedule_reconnect();
}

void WsClient::schedule_reconnect() {
    Log::info("shard ", shard_id_, ": reconnecting in ", backoff_.count(), " ms");
    reconnect_timer_.expires_after(backoff_);
    backoff_ = std::min(backoff_ * 2, kMaxBackoff);
    reconnect_timer_.async_wait([self = shared_from_this()](beast::error_code ec) {
        if (self->stopping_) return self->notify_stopped();
        if (ec) return;
        self->connect();
    });
}

}  // namespace blc
