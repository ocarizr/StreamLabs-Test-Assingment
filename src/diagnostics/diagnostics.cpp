#include "pipeforge/diagnostics.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <system_error>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace pipeforge {
namespace {
constexpr std::size_t log_file_bytes = std::size_t{5} * 1024 * 1024;
constexpr std::size_t rotated_log_files = 3;
constexpr std::size_t trace_thread_limit = 128;
constexpr std::size_t profile_name_bytes = 256;

std::uint64_t process_id()
{
#ifdef _WIN32
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

std::filesystem::path canonical_output(const std::filesystem::path& path)
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        throw diagnostic_io_error("Cannot resolve diagnostic output path");
    }

    const auto canonical = std::filesystem::weakly_canonical(absolute, error);
    if (error) {
        throw diagnostic_io_error("Cannot normalize diagnostic output path");
    }

    return canonical;
}

void validate_paths(const diagnostic_options& options)
{
    if (options.log_file.empty() || options.trace_file.empty()) {
        return;
    }

    const auto log_path = canonical_output(options.log_file);
    const auto trace_path = canonical_output(options.trace_file);
    bool same_path = log_path == trace_path;
#ifdef _WIN32
    same_path = same_path || ::CompareStringOrdinal(
                                 log_path.c_str(), -1, trace_path.c_str(), -1, TRUE) == CSTR_EQUAL;
#endif
    std::error_code error;
    const bool aliases = std::filesystem::equivalent(log_path, trace_path, error);
    if (same_path || (!error && aliases)) {
        throw diagnostic_io_error("Log and trace output must use different files");
    }
}

spdlog::level::level_enum native_level(log_level level)
{
    switch (level) {
    case log_level::debug:
        return spdlog::level::debug;
    case log_level::info:
        return spdlog::level::info;
    case log_level::warning:
        return spdlog::level::warn;
    case log_level::error:
        return spdlog::level::err;
    }

    return spdlog::level::err;
}
} // namespace

class diagnostic_state {
  public:
    diagnostic_state(std::string_view application, const diagnostic_options& options)
        : origin_(profiling_enabled && !options.trace_file.empty()
                      ? std::chrono::steady_clock::now()
                      : std::chrono::steady_clock::time_point{}),
          event_limit_(options.trace_event_limit)
    {
        validate_paths(options);
        if (!options.log_file.empty()) {
            const auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                options.log_file.native(), log_file_bytes, rotated_log_files);
            log_ = std::make_shared<spdlog::logger>(std::string(application), sink);
            configure_errors(log_);
            log_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [pid %P] [thread %t] %v");
            log_->set_level(spdlog::level::debug);
        }

        if constexpr (profiling_enabled) {
            if (!options.trace_file.empty()) {
                const auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                    options.trace_file.native(), true);
                trace_ =
                    std::make_shared<spdlog::logger>(std::string(application) + ".profile", sink);
                configure_errors(trace_);
                trace_->set_pattern("%v");
                trace_->info("{}", "{\"traceEvents\":[");
                write_event({{"name", "process_name"},
                             {"ph", "M"},
                             {"pid", process_id()},
                             {"tid", 0},
                             {"args", {{"name", application}}}});
                trace_enabled_ = true;
            }
        }
    }

    void log(log_level level, std::string_view message)
    {
        const std::lock_guard lock(mutex_);
        if (log_ && !closing_) {
            log_->log(native_level(level), "{}", message);
        }
    }

    [[nodiscard]] bool tracing() const
    {
        return trace_enabled_;
    }

    [[nodiscard]] bool failed() const
    {
        return failed_->load();
    }

    std::optional<std::uint64_t> begin_scope()
    {
        const std::lock_guard lock(mutex_);
        if (!trace_ || closing_ || failed()) {
            return std::nullopt;
        }

        if (admitted_ >= event_limit_) {
            ++dropped_;
            return std::nullopt;
        }

        const auto identifier = std::this_thread::get_id();
        auto position = threads_.find(identifier);
        if (position == threads_.end()) {
            if (threads_.size() >= trace_thread_limit) {
                ++dropped_;
                return std::nullopt;
            }

            position = threads_.emplace(identifier, threads_.size() + 1).first;
        }

        ++active_;
        ++admitted_;
        return position->second;
    }

    void end_scope(std::string_view name,
                   std::uint64_t thread,
                   std::chrono::steady_clock::time_point start)
    {
        const auto stop = std::chrono::steady_clock::now();
        const auto timestamp =
            std::chrono::duration_cast<std::chrono::microseconds>(start - origin_).count();
        const auto duration =
            std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
        const std::lock_guard lock(mutex_);
        write_event({{"name", name},
                     {"cat", "pipeforge"},
                     {"ph", "X"},
                     {"pid", process_id()},
                     {"tid", thread},
                     {"ts", timestamp},
                     {"dur", duration}});
        --active_;
        if (closing_) {
            finalize();
        }
    }

    bool finish()
    {
        const std::lock_guard lock(mutex_);
        closing_ = true;
        return finalize();
    }

  private:
    void configure_errors(const std::shared_ptr<spdlog::logger>& logger) const
    {
        logger->set_error_handler([failure = failed_](const std::string&) {
            failure->store(true);
        });
    }

    void write_event(const nlohmann::json& event)
    {
        // The upstream serializer handles quotes, controls, Unicode and invalid UTF-8.
        const auto serialized =
            event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        trace_->info("{}{}", first_event_ ? "" : ",", serialized);
        first_event_ = false;
    }

    bool finalize()
    {
        if (active_ != 0) {
            return false;
        }

        if (trace_) {
            const nlohmann::json metadata{{"droppedEvents", dropped_}};
            trace_->info("{}",
                         "],\"displayTimeUnit\":\"ms\",\"otherData\":" + metadata.dump() + "}");
            trace_->flush();
            trace_.reset();
        }

        if (log_) {
            log_->flush();
            log_.reset();
        }

        return !failed();
    }

    std::shared_ptr<std::atomic<bool>> failed_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<spdlog::logger> log_;
    std::shared_ptr<spdlog::logger> trace_;
    const std::chrono::steady_clock::time_point origin_;
    std::mutex mutex_;
    std::map<std::thread::id, std::uint64_t> threads_;
    const std::size_t event_limit_;
    std::size_t active_ = 0;
    std::size_t admitted_ = 0;
    std::size_t dropped_ = 0;
    bool first_event_ = true;
    bool trace_enabled_ = false;
    bool closing_ = false;
};

diagnostic_session::diagnostic_session(std::string_view application,
                                       const diagnostic_options& options)
{
    if (options.log_file.empty() && options.trace_file.empty()) {
        return;
    }

    try {
        state_ = std::make_shared<diagnostic_state>(application, options);
    }
    catch (const spdlog::spdlog_ex& error) {
        throw diagnostic_io_error(error.what());
    }
}

// Sink errors are reported through state; unrecoverable allocation failures remain fatal.
// NOLINTNEXTLINE(bugprone-exception-escape)
diagnostic_session::~diagnostic_session()
{
    if (state_) {
        static_cast<void>(state_->finish());
    }
}

void diagnostic_session::log(log_level level, std::string_view message)
{
    if (state_) {
        state_->log(level, message);
    }
}

bool diagnostic_session::finish()
{
    return !state_ || state_->finish();
}

bool diagnostic_session::failed() const
{
    return state_ && state_->failed();
}

profile_scope::profile_scope(const diagnostic_session& session, std::string_view name)
    : state_(session.state_)
{
    if (!state_ || !state_->tracing()) {
        state_.reset();
        return;
    }

    name_ = name.substr(0, profile_name_bytes);
    thread_ = state_->begin_scope();
    if (thread_) {
        start_ = std::chrono::steady_clock::now();
    }
}

// I/O errors go to the sink handler; allocation exhaustion is fatal during cleanup.
// NOLINTNEXTLINE(bugprone-exception-escape)
profile_scope::~profile_scope()
{
    if (thread_) {
        state_->end_scope(name_, *thread_, start_);
    }
}
} // namespace pipeforge
