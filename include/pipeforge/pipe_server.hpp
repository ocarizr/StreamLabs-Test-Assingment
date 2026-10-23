#ifndef PIPEFORGE_PIPE_SERVER_HPP
#define PIPEFORGE_PIPE_SERVER_HPP

#include "pipeforge/domain.hpp"
#include "pipeforge/transport.hpp"

#include <string>

namespace pipeforge {
class diagnostic_session;

namespace transport {
/** Local endpoint, bounded workers and total per-request deadline, including idle reads. */
struct server_options {
    std::wstring endpoint{default_endpoint};
    std::chrono::milliseconds request_timeout = default_timeout;
    std::size_t sessions = max_sessions;
    domain::session_limits storage{};
};

/** Approximate concurrent snapshot; final counts are stable after stop(). */
struct server_statistics {
    std::uint64_t connections = 0;
    std::uint64_t completed_requests = 0;
    std::uint64_t protocol_failures = 0;
    std::uint64_t io_failures = 0;
    std::size_t active_sessions = 0;
    std::uint32_t last_native_error = 0;
};

/**
 * Windows-only bounded host. Owns a frozen registry, global ID source and fixed
 * worker/pipe slots. One domain gate serializes dispatch and disconnect cleanup.
 * start() publishes readiness only after all secure pipe instances exist.
 * Optional diagnostics must outlive the host. Unexpected allocation/custom-code
 * exceptions are fatal in workers; trusted methods must be bounded and do no I/O.
 */
class pipe_server {
  public:
    /**
     * Move the registry into a ready host. Returns a typed configuration/security/
     * native failure on startup; partial startup joins workers and closes handles.
     * Allocation failures propagate. Borrowed factory captures must outlive the host.
     */
    [[nodiscard]] static result<std::unique_ptr<pipe_server>>
    start(domain::class_registry registry,
          server_options options = {},
          diagnostic_session* diagnostics = nullptr);
    pipe_server(const pipe_server&) = delete;
    pipe_server& operator=(const pipe_server&) = delete;
    pipe_server(pipe_server&&) = delete;
    pipe_server& operator=(pipe_server&&) = delete;
    ~pipe_server();

    /** Thread-safe signal only; it does not join or release operation storage. */
    void request_stop() noexcept;
    /** Controller-only, never from a worker. Cancel, drain/join, then release the name.
     * Idempotent; concurrent stop()/destruction is unsupported.
     */
    void stop() noexcept;
    [[nodiscard]] server_statistics statistics() const noexcept;

  private:
    explicit pipe_server(std::unique_ptr<server_state> state);
    std::unique_ptr<server_state> state_;
};
} // namespace transport
} // namespace pipeforge
#endif // PIPEFORGE_PIPE_SERVER_HPP
