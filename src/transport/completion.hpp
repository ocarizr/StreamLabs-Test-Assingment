#ifndef PIPEFORGE_COMPLETION_HPP
#define PIPEFORGE_COMPLETION_HPP

#include "pipeforge/transport.hpp"

namespace pipeforge::transport::detail {
enum class wake_reason : std::uint8_t { completed, expired, stopped, failed };

struct wake_result {
    wake_reason reason = wake_reason::failed;
    std::uint32_t native_error = 0;
};

/** Injectable completion boundary; complete(true) observes terminal completion, even after cancel.
 */
class completion_backend {
  public:
    completion_backend() = default;
    completion_backend(const completion_backend&) = delete;
    completion_backend& operator=(const completion_backend&) = delete;
    completion_backend(completion_backend&&) = delete;
    completion_backend& operator=(completion_backend&&) = delete;
    virtual ~completion_backend() = default;
    [[nodiscard]] virtual wake_result wait(deadline until) = 0;
    [[nodiscard]] virtual std::uint32_t cancel() = 0;
    [[nodiscard]] virtual result<std::size_t> complete(bool wait) = 0;
};

/** Keep operation ownership until cancellation races have reached terminal completion. */
[[nodiscard]] result<std::size_t> resolve_pending(completion_backend& operation, deadline until);
} // namespace pipeforge::transport::detail
#endif // PIPEFORGE_COMPLETION_HPP
