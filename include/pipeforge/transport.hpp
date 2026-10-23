#ifndef PIPEFORGE_TRANSPORT_HPP
#define PIPEFORGE_TRANSPORT_HPP

#include "pipeforge/protocol.hpp"

#include <chrono>
#include <memory>
#include <stop_token>
#include <string_view>

namespace pipeforge::transport {
using clock = std::chrono::steady_clock;
using deadline = clock::time_point;
inline constexpr std::wstring_view default_endpoint = L"\\\\.\\pipe\\pipeforge-v1";
inline constexpr std::chrono::milliseconds default_timeout{5000};
inline constexpr std::chrono::milliseconds minimum_timeout{10};
inline constexpr std::chrono::milliseconds maximum_timeout{60000};
inline constexpr std::size_t max_sessions = 4;

enum class failure_kind : std::uint8_t {
    invalid_configuration,
    timeout,
    cancelled,
    disconnected,
    access_denied,
    io_failure,
    protocol_violation
};

/** Native codes stay local; framing details contain no peer payloads. */
struct failure {
    failure_kind kind = failure_kind::io_failure;
    std::uint32_t native_error = 0;
    std::optional<protocol::failure> framing{};
};

template <typename T> using result = std::variant<T, failure>;

/** Restrict endpoints to local named pipes and a bounded ASCII component. */
[[nodiscard]] bool valid_endpoint(std::wstring_view name);

class stream_state;
class server_state;

/**
 * Windows byte-mode named-pipe stream. Blocking completion wrappers use overlapped
 * I/O and absolute steady-clock deadlines. One reader and one serialized writer
 * may operate concurrently; moving/closing/destruction requires both to finish.
 * After a failure, cancel and finish any concurrent call before closing.
 * A stop token cancels pending I/O; cancellation is drained before returning.
 * Every I/O/framing failure is terminal. No automatic mutation replay.
 */
class pipe_stream {
  public:
    pipe_stream(pipe_stream&&) noexcept;
    pipe_stream& operator=(pipe_stream&&) noexcept;
    pipe_stream(const pipe_stream&) = delete;
    pipe_stream& operator=(const pipe_stream&) = delete;
    ~pipe_stream();

    /**
     * Open a local endpoint; unavailable/busy retries are deadline/cancel bounded.
     * Returns a move-only stream or configuration/native/deadline/cancel failure.
     */
    [[nodiscard]] static result<pipe_stream>
    connect(std::wstring_view endpoint, deadline until, const std::stop_token& stop = {});
    /**
     * Own the framed copy until every write completes, including cancellation cleanup.
     * Returns wire bytes written (header included) or a terminal typed failure.
     * Failure cannot establish whether the peer already dispatched a mutation.
     */
    [[nodiscard]] result<std::size_t>
    send(std::span<const std::byte> payload, deadline until, const std::stop_token& stop = {});
    /**
     * Return one payload, preserving any concatenated frame suffix for the next call.
     * M2 framing limits apply before allocation; errors make this stream terminal.
     */
    [[nodiscard]] result<protocol::bytes> receive(deadline until, const std::stop_token& stop = {});
    /** Close only after outstanding calls have returned; idempotent. */
    void close() noexcept;
    /** Borrowed Windows handle for native interop; never close it or overlap an existing call. */
    [[nodiscard]] void* native_handle() const noexcept;

  private:
    explicit pipe_stream(std::unique_ptr<stream_state> state);
    std::unique_ptr<stream_state> state_;
    friend class server_state;
};
} // namespace pipeforge::transport
#endif // PIPEFORGE_TRANSPORT_HPP
