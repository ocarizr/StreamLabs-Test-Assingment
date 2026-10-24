/** @file
 * Opt-in process diagnostics and bounded scope profiling.
 */
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
/** Whether scope recording is compiled into this build. */
inline constexpr bool profiling_enabled = PIPEFORGE_ENABLE_PROFILING != 0;
/** Default maximum complete events retained per trace. */
inline constexpr std::size_t default_trace_event_limit = 100000;

/** Severity for trusted diagnostic messages. */
enum class log_level : std::uint8_t {
    debug,   ///< Detailed local control-flow diagnostics.
    info,    ///< Ordinary lifecycle information.
    warning, ///< Recoverable condition needing attention.
    error,   ///< Operation or diagnostic sink failure.
};

/** Optional explicit output paths; no diagnostics files are created by default. */
struct diagnostic_options {
    std::filesystem::path log_file{};   ///< Optional append/rotating log path.
    std::filesystem::path trace_file{}; ///< Optional replaced Chrome Trace JSON path.
    std::size_t trace_event_limit = default_trace_event_limit; ///< Maximum accepted events; later
                                                               ///< events are counted as dropped.
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
    /** Open explicitly requested sinks; throws diagnostic_io_error on path/setup failure. */
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
    /** Retain session state and record a bounded trusted label on the current thread. */
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
/** Internal token concatenation after the caller supplies expanded tokens. */
#define PIPEFORGE_DETAIL_JOIN_IMPL(left, right) left##right
/** Internal expansion step for unique lexical profiling variable names. */
#define PIPEFORGE_DETAIL_JOIN(left, right) PIPEFORGE_DETAIL_JOIN_IMPL(left, right)
#if PIPEFORGE_ENABLE_PROFILING && !defined(PIPEFORGE_DISABLE_PROFILING)
/** Record a complete event for this lexical scope; disabled builds evaluate no arguments. */
#define PIPEFORGE_PROFILE_SCOPE(session, name)                                                     \
    const ::pipeforge::profile_scope PIPEFORGE_DETAIL_JOIN(pipeforge_scope_, __LINE__)             \
    {                                                                                              \
        (session), (name)                                                                          \
    }
/** Profile the enclosing function using its compiler-supplied name. */
#define PIPEFORGE_PROFILE_FUNCTION(session) PIPEFORGE_PROFILE_SCOPE((session), __func__)
#else
/** Record a complete event for this lexical scope; disabled builds evaluate no arguments. */
#define PIPEFORGE_PROFILE_SCOPE(session, name) static_cast<void>(0)
/** Profile the enclosing function using its compiler-supplied name. */
#define PIPEFORGE_PROFILE_FUNCTION(session) static_cast<void>(0)
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif // PIPEFORGE_DIAGNOSTICS_HPP
