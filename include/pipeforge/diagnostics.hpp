#ifndef PIPEFORGE_DIAGNOSTICS_HPP
#define PIPEFORGE_DIAGNOSTICS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pipeforge {
inline constexpr bool profiling_enabled = PIPEFORGE_ENABLE_PROFILING != 0;
inline constexpr std::size_t default_trace_event_limit = 100000;

enum class log_level : std::uint8_t { debug, info, warning, error };

/** Optional explicit output paths; no diagnostics files are created by default. */
struct diagnostic_options {
    std::filesystem::path log_file{};
    std::filesystem::path trace_file{};
    std::size_t trace_event_limit = default_trace_event_limit;
};

/** A diagnostic file could not be opened or its paths are incompatible. */
class diagnostic_io_error final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class diagnostic_state;
class profile_scope;

/**
 * Owns synchronous spdlog sinks for one process session. Ordinary logs append
 * and rotate; Chrome Trace output is replaced. Distinct files are required.
 * File setup failures throw diagnostic_io_error; ongoing I/O failures are
 * reported by finish(). No global logger registry or background worker is used.
 */
class diagnostic_session {
  public:
    explicit diagnostic_session(std::string_view application,
                                const diagnostic_options& options = {});

    ~diagnostic_session();

    diagnostic_session(const diagnostic_session&) = delete;

    diagnostic_session& operator=(const diagnostic_session&) = delete;

    diagnostic_session(diagnostic_session&&) = delete;

    diagnostic_session& operator=(diagnostic_session&&) = delete;

    /** Write a trusted diagnostic message, without interpreting it as a format string. */
    void log(log_level level, std::string_view message);

    /**
     * Stop accepting scopes and flush output. Returns false on I/O failure or
     * while scopes remain active. The last active scope finalizes deferred output.
     * Calls are idempotent; logging after finish is ignored.
     */
    [[nodiscard]] bool finish();

    /** Query recorded sink I/O failures; thread-safe. */
    [[nodiscard]] bool failed() const;

  private:
    std::shared_ptr<diagnostic_state> state_;
    friend class profile_scope;
};

/**
 * Records a monotonic-clock complete event at scope exit, including unwinding.
 * Nonmovable: construct and destroy on the same thread. Owns the session state
 * until completion, so an enclosing session may request deferred shutdown.
 * Labels are bounded and invalid UTF-8 is replaced in trace serialization.
 * Fatal allocation failures remain fatal; sink I/O errors never escape cleanup.
 */
class profile_scope {
  public:
    profile_scope(const diagnostic_session& session, std::string_view name);

    ~profile_scope();

    profile_scope(const profile_scope&) = delete;

    profile_scope& operator=(const profile_scope&) = delete;

    profile_scope(profile_scope&&) = delete;

    profile_scope& operator=(profile_scope&&) = delete;

  private:
    std::shared_ptr<diagnostic_state> state_;
    std::string name_;
    std::chrono::steady_clock::time_point start_{};
    std::optional<std::uint64_t> thread_;
};
} // namespace pipeforge

// Macro expansion captures the caller's lexical scope/function. One scope per
// source line; disabled macros do not evaluate either argument.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define PIPEFORGE_DETAIL_JOIN_IMPL(left, right) left##right
#define PIPEFORGE_DETAIL_JOIN(left, right) PIPEFORGE_DETAIL_JOIN_IMPL(left, right)
#if PIPEFORGE_ENABLE_PROFILING && !defined(PIPEFORGE_DISABLE_PROFILING)
#define PIPEFORGE_PROFILE_SCOPE(session, name)                                                     \
    const ::pipeforge::profile_scope PIPEFORGE_DETAIL_JOIN(pipeforge_scope_, __LINE__)             \
    {                                                                                              \
        (session), (name)                                                                          \
    }
#define PIPEFORGE_PROFILE_FUNCTION(session) PIPEFORGE_PROFILE_SCOPE((session), __func__)
#else
#define PIPEFORGE_PROFILE_SCOPE(session, name) static_cast<void>(0)
#define PIPEFORGE_PROFILE_FUNCTION(session) static_cast<void>(0)
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif // PIPEFORGE_DIAGNOSTICS_HPP
