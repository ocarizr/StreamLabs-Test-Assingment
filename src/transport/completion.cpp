#include "completion.hpp"

#include <algorithm>

namespace pipeforge::transport {
bool valid_endpoint(std::wstring_view name)
{
    constexpr std::wstring_view prefix = L"\\\\.\\pipe\\";
    constexpr std::size_t max_endpoint_characters = 240;
    if (!name.starts_with(prefix) || name.size() <= prefix.size() ||
        name.size() > max_endpoint_characters) {
        return false;
    }

    return std::ranges::all_of(name.substr(prefix.size()), [](wchar_t character) {
        return (character >= L'a' && character <= L'z') ||
               (character >= L'A' && character <= L'Z') ||
               (character >= L'0' && character <= L'9') || character == L'_' || character == L'-' ||
               character == L'.';
    });
}

namespace detail {
result<std::size_t> resolve_pending(completion_backend& operation, deadline until)
{
    const auto wake = operation.wait(until);
    if (wake.reason == wake_reason::completed) {
        return operation.complete(false);
    }

    const auto cancellation_error = operation.cancel();
    // Cancellation can lose to successful completion or return ERROR_NOT_FOUND.
    // Neither permits releasing buffers/events until completion is observed.
    const auto drained = operation.complete(true);
    const auto* completion_error = std::get_if<failure>(&drained);
    const auto native_error = wake.reason == wake_reason::failed ? wake.native_error
                              : completion_error                 ? completion_error->native_error
                                                                 : cancellation_error;
    const auto kind = wake.reason == wake_reason::expired   ? failure_kind::timeout
                      : wake.reason == wake_reason::stopped ? failure_kind::cancelled
                                                            : failure_kind::io_failure;
    return failure{kind, native_error};
}
} // namespace detail
} // namespace pipeforge::transport
