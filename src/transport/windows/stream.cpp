#include <algorithm>

#include "stream_state.hpp"

namespace pipeforge::transport {
pipe_stream::pipe_stream(std::unique_ptr<stream_state> state) : state_(std::move(state)) {}

pipe_stream::pipe_stream(pipe_stream&&) noexcept = default;
pipe_stream& pipe_stream::operator=(pipe_stream&&) noexcept = default;
pipe_stream::~pipe_stream() = default;

result<pipe_stream>
pipe_stream::connect(std::wstring_view endpoint, deadline until, const std::stop_token& stop)
{
    if (!valid_endpoint(endpoint)) {
        return failure{failure_kind::invalid_configuration};
    }

    if (auto error = native::preflight(until, stop)) {
        return *error;
    }

    native::unique_handle cancelled(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!cancelled) {
        return native::io_failure(GetLastError());
    }

    const std::stop_callback callback(stop, native::signal_event{cancelled.get()});
    const std::wstring name(endpoint);
    constexpr DWORD retry_milliseconds = 50;
    while (true) {
        if (auto error = native::preflight(until, stop)) {
            return *error;
        }

        native::unique_handle pipe(
            CreateFileW(name.c_str(),
                        GENERIC_READ | GENERIC_WRITE,
                        0,
                        nullptr,
                        OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                        nullptr));
        if (pipe) {
            auto owned = std::make_shared<native::unique_handle>(std::move(pipe));
            return pipe_stream(std::make_unique<stream_state>(std::move(owned)));
        }

        const auto code = GetLastError();
        if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND) {
            return native::io_failure(code);
        }

        const auto slice = std::min(native::remaining(until), retry_milliseconds);
        // Zero means "use the server default" to WaitNamedPipe, not a poll.
        if (slice == 0) {
            return failure{failure_kind::timeout};
        }

        if (WaitNamedPipeW(name.c_str(), slice)) {
            continue;
        }

        const auto wait_error = GetLastError();
        if (wait_error != ERROR_FILE_NOT_FOUND && wait_error != ERROR_SEM_TIMEOUT &&
            wait_error != ERROR_PIPE_BUSY) {
            return native::io_failure(wait_error);
        }

        // WaitNamedPipe returns immediately for a missing name. Back off on a
        // cancellable event rather than spinning while the server starts.
        if (wait_error == ERROR_FILE_NOT_FOUND) {
            const auto waited = WaitForSingleObject(cancelled.get(), slice);
            if (waited == WAIT_FAILED) {
                return native::io_failure(GetLastError());
            }
        }
    }
}

result<std::size_t>
pipe_stream::send(std::span<const std::byte> payload, deadline until, const std::stop_token& stop)
{
    if (!state_ || state_->failed.load()) {
        return failure{failure_kind::disconnected};
    }

    if (auto error = native::preflight(until, stop)) {
        state_->failed = true;
        return *error;
    }

    auto framed = protocol::frame(payload);
    if (const auto* error = std::get_if<protocol::failure>(&framed)) {
        state_->failed = true;
        return failure{failure_kind::protocol_violation, 0, *error};
    }

    const auto& owned = std::get<protocol::bytes>(framed);
    auto remaining = std::span<const std::byte>(owned);
    while (!remaining.empty()) {
        const auto written = native::write_some(state_->pipe->get(), remaining, until, stop);
        if (const auto* error = std::get_if<failure>(&written)) {
            state_->failed = true;
            return *error;
        }

        const auto count = std::get<std::size_t>(written);
        if (count == 0 || count > remaining.size()) {
            state_->failed = true;
            return failure{failure_kind::disconnected};
        }

        remaining = remaining.subspan(count);
    }

    return owned.size();
}

result<protocol::bytes> pipe_stream::receive(deadline until, const std::stop_token& stop)
{
    if (!state_ || state_->failed.load()) {
        return failure{failure_kind::disconnected};
    }

    while (true) {
        if (auto error = native::preflight(until, stop)) {
            state_->failed = true;
            return *error;
        }

        if (state_->begin < state_->end) {
            auto progress = state_->decoder.consume(
                std::span(state_->input).first(state_->end).subspan(state_->begin));
            if (const auto* error = std::get_if<protocol::failure>(&progress)) {
                state_->failed = true;
                return failure{failure_kind::protocol_violation, 0, *error};
            }

            auto& decoded = std::get<protocol::frame_progress>(progress);
            state_->begin += decoded.consumed;
            if (decoded.payload) {
                return std::move(*decoded.payload);
            }
        }

        const auto received = native::read_some(state_->pipe->get(), state_->input, until, stop);
        if (const auto* error = std::get_if<failure>(&received)) {
            state_->failed = true;
            auto closed = *error;
            if (closed.kind == failure_kind::disconnected) {
                closed.framing = state_->decoder.finish();
                if (closed.framing) {
                    closed.kind = failure_kind::protocol_violation;
                }
            }

            return closed;
        }

        const auto count = std::get<std::size_t>(received);
        if (count == 0) {
            state_->failed = true;
            const auto unfinished = state_->decoder.finish();
            return failure{unfinished ? failure_kind::protocol_violation
                                      : failure_kind::disconnected,
                           0,
                           unfinished};
        }

        state_->begin = 0;
        state_->end = count;
    }
}

void pipe_stream::close() noexcept
{
    state_.reset();
}

void* pipe_stream::native_handle() const noexcept
{
    return state_ ? state_->pipe->get() : nullptr;
}
} // namespace pipeforge::transport
