/** @file
 * Correlated application dispatch for one serialized session.
 */
#ifndef PIPEFORGE_SERVICES_HPP
#define PIPEFORGE_SERVICES_HPP

#include "pipeforge/domain.hpp"
#include "pipeforge/protocol.hpp"

namespace pipeforge {
class diagnostic_session;

namespace server {
/**
 * Transport-independent application boundary for one serialized session.
 * Store and optional diagnostic observer must outlive this dispatcher. Safe
 * logs contain operation/ID/error categories only, never peer payloads. Does no
 * I/O beyond opt-in diagnostics and does not implement transport concurrency.
 */
class dispatcher {
  public:
    /** Borrow one serialized store and optional diagnostic observer; retain no requests. */
    explicit dispatcher(domain::session_store& store, diagnostic_session* diagnostics = nullptr);
    dispatcher(const dispatcher&) = delete;
    dispatcher& operator=(const dispatcher&) = delete;
    dispatcher(dispatcher&&) = delete;
    dispatcher& operator=(dispatcher&&) = delete;
    ~dispatcher() = default;
    /** Zero IDs are fatal; recoverable errors preserve correlation and current version. */
    [[nodiscard]] protocol::result<protocol::response> dispatch(const protocol::request& request);
    /** Translate a codec failure only when its nonzero correlation ID is trusted. */
    [[nodiscard]] protocol::result<protocol::response>
    reject(const protocol::failure& failure) const;

  private:
    [[nodiscard]] protocol::result<protocol::response> process(const protocol::request& request);
    domain::session_store& store_;
    diagnostic_session* diagnostics_;
};
} // namespace server
} // namespace pipeforge
#endif // PIPEFORGE_SERVICES_HPP
