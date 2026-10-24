/** @file
 * Bounded future-based client scheduling and injected channel contracts.
 */
#ifndef PIPEFORGE_CLIENT_HPP
#define PIPEFORGE_CLIENT_HPP

#include "pipeforge/transport.hpp"

#include <functional>
#include <future>

namespace pipeforge {
class diagnostic_session;

namespace client {
/** Admission ceiling for queued and in-flight calls per connection. */
inline constexpr std::size_t max_pending_calls = 16;

/** Expected local failure category; native details never enter remote replies. */
enum class failure_kind : std::uint8_t {
    invalid_request,    ///< Local operation or connection configuration is invalid.
    not_connected,      ///< No usable established client session.
    quota_exceeded,     ///< Client pending-call admission ceiling reached.
    timeout,            ///< Absolute deadline expired; sent mutations may have executed.
    cancelled,          ///< Caller or controller requested cancellation.
    disconnected,       ///< Peer exited or the session became unusable.
    access_denied,      ///< Native access policy denied the operation.
    protocol_violation, ///< Peer framing, correlation or response contract is invalid.
    transport_failure,  ///< Other typed failure reported by the transport.
    internal_failure,   ///< Local worker resources or adapter contract failed.
};

/** Local failures never masquerade as a server response; native codes stay local. */
struct failure {
    failure_kind kind = failure_kind::transport_failure; ///< Stable local failure category.
    std::uint32_t native_error = 0; ///< Retained native code, zero if no native call failed.
};

struct connected {};

/** Connection marker or typed local failure. */
using connect_result = std::variant<connected, failure>;
/** Correlated protocol response or typed local failure. */
using call_result = std::variant<protocol::response, failure>;

/**
 * Framed-payload boundary used by the client and controlled completion tests.
 * Implementations allow one sender and one receiver concurrently, honor stop and
 * absolute deadlines, and drain pending I/O before returning. close() follows joins.
 */
class channel {
  public:
    channel() = default;
    channel(const channel&) = delete;
    channel& operator=(const channel&) = delete;
    channel(channel&&) = delete;
    channel& operator=(channel&&) = delete;
    virtual ~channel() = default;
    /** Send exactly one complete payload, adding transport framing only once. */
    [[nodiscard]] virtual transport::result<std::size_t> send(std::span<const std::byte> payload,
                                                              transport::deadline until,
                                                              const std::stop_token& stop) = 0;
    /** Return exactly one owned payload or a typed terminal failure; honor stop/deadline. */
    [[nodiscard]] virtual transport::result<protocol::bytes>
    receive(transport::deadline until, const std::stop_token& stop) = 0;
    /** Idempotent native cleanup, called only after both I/O directions have returned. */
    virtual void close() noexcept = 0;
};

/** Injected opener; captures must outlive the connection and obey channel cancellation. */
using connector = std::function<transport::result<std::unique_ptr<channel>>(
    std::wstring_view, transport::deadline, const std::stop_token&)>;

/** Windows-only adapter: opens the local named-pipe stream, with no socket fallback. */
[[nodiscard]] connector named_pipe_connector();

class connection_state;

/**
 * Single-session client with a bounded writer queue, independent reader and
 * deadline/cancellation monitor. Submission never waits for a peer response.
 * The blocking methods wait on the same futures as the asynchronous API.
 * Calls may be submitted concurrently; one controller owns connect/close/destruction.
 * IDs are generated internally and never reused within this connection.
 *
 * Cancelling/expiring an unsent call affects only that call. Once writing starts,
 * timeout/cancellation closes the session and fails other calls as disconnected;
 * execution may be indeterminate. No mutation replay or implicit reconnect.
 * close() resolves every promise once, cancels/drains I/O, joins workers and closes
 * the channel. Create another connection to reconnect. Diagnostics and borrowed
 * connector captures must outlive the connection. Allocation/unknown exceptions
 * are not converted to I/O errors; injected adapters must follow the typed contract.
 */
class connection {
  public:
    /** Retain the opener and optional borrowed diagnostics; no connection is started. */
    explicit connection(connector open, diagnostic_session* diagnostics = nullptr);
    ~connection();
    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;
    connection(connection&&) = delete;
    connection& operator=(connection&&) = delete;

    /** Start one bounded connection attempt; repeated attempts on this object are rejected. */
    [[nodiscard]] std::future<connect_result> connect_async(std::wstring endpoint,
                                                            transport::deadline until,
                                                            const std::stop_token& stop = {});
    /** Blocking wrapper around connect_async with identical deadline/error semantics. */
    [[nodiscard]] connect_result
    connect(std::wstring endpoint, transport::deadline until, const std::stop_token& stop = {});
    /** Admit up to sixteen validated calls; queue time is included in the absolute deadline. */
    [[nodiscard]] std::future<call_result> call_async(protocol::operation operation,
                                                      transport::deadline until,
                                                      const std::stop_token& stop = {});
    /** Blocking wrapper around call_async; never retries an indeterminate mutation. */
    [[nodiscard]] call_result call(protocol::operation operation,
                                   transport::deadline until,
                                   const std::stop_token& stop = {});
    /** Controller-only, idempotent; never call from an adapter/worker. */
    void close() noexcept;

  private:
    std::unique_ptr<connection_state> state_;
};
} // namespace client
} // namespace pipeforge
#endif // PIPEFORGE_CLIENT_HPP
