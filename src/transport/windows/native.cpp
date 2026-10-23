#include "native.hpp"

#include <algorithm>
#include <limits>
#include <sddl.h>
#include <utility>

namespace pipeforge::transport::native {
unique_handle::unique_handle(HANDLE handle) noexcept : handle_(handle) {}

unique_handle::~unique_handle()
{
    reset();
}

unique_handle::unique_handle(unique_handle&& other) noexcept : handle_(other.release()) {}

unique_handle& unique_handle::operator=(unique_handle&& other) noexcept
{
    if (this != &other) {
        reset(other.release());
    }

    return *this;
}

unique_handle::operator bool() const noexcept
{
    return handle_ && handle_ != INVALID_HANDLE_VALUE;
}

HANDLE unique_handle::get() const noexcept
{
    return handle_;
}

void unique_handle::reset(HANDLE handle) noexcept
{
    if (*this) {
        CloseHandle(handle_);
    }

    handle_ = handle;
}

HANDLE unique_handle::release() noexcept
{
    return std::exchange(handle_, nullptr);
}

void local_deleter::operator()(void* memory) const noexcept
{
    LocalFree(memory);
}

failure io_failure(DWORD code)
{
    if (code == ERROR_BROKEN_PIPE || code == ERROR_PIPE_NOT_CONNECTED || code == ERROR_NO_DATA) {
        return {failure_kind::disconnected, code};
    }

    if (code == ERROR_ACCESS_DENIED) {
        return {failure_kind::access_denied, code};
    }

    if (code == ERROR_OPERATION_ABORTED) {
        return {failure_kind::cancelled, code};
    }

    return {failure_kind::io_failure, code};
}

DWORD remaining(deadline until)
{
    if (until == deadline::max()) {
        return INFINITE;
    }

    const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(until - clock::now());
    if (milliseconds <= std::chrono::milliseconds::zero()) {
        return 0;
    }

    constexpr auto largest_wait = static_cast<std::int64_t>(INFINITE - 1);
    return static_cast<DWORD>(std::min<std::int64_t>(milliseconds.count(), largest_wait));
}

std::optional<failure> preflight(deadline until, const std::stop_token& stop)
{
    if (stop.stop_requested()) {
        return failure{failure_kind::cancelled};
    }

    if (clock::now() >= until) {
        return failure{failure_kind::timeout};
    }

    return std::nullopt;
}

result<local_memory> logon_security()
{
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        return io_failure(GetLastError());
    }

    unique_handle token(raw_token);
    alignas(TOKEN_GROUPS) std::array<std::byte, sizeof(TOKEN_GROUPS) + SECURITY_MAX_SID_SIZE>
        storage{};
    DWORD size = 0;
    if (!GetTokenInformation(token.get(),
                             TokenLogonSid,
                             storage.data(),
                             static_cast<DWORD>(storage.size()),
                             &size)) {
        return io_failure(GetLastError());
    }

    if (size < sizeof(TOKEN_GROUPS) || size > storage.size()) {
        return failure{failure_kind::access_denied, ERROR_INVALID_SID};
    }

    // Win32 fills an aligned TOKEN_GROUPS record and its trailing SID in this buffer.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    if (groups->GroupCount != 1 || !IsValidSid(groups->Groups[0].Sid)) {
        return failure{failure_kind::access_denied, ERROR_INVALID_SID};
    }

    LPWSTR raw_sid = nullptr;
    if (!ConvertSidToStringSidW(groups->Groups[0].Sid, &raw_sid)) {
        return io_failure(GetLastError());
    }

    const local_memory sid(raw_sid);
    const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(raw_sid) + L")";
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &raw_descriptor, nullptr)) {
        return io_failure(GetLastError());
    }

    return local_memory(raw_descriptor);
}

result<std::shared_ptr<unique_handle>>
create_instance(const std::wstring& name, std::size_t slots, bool first, void* descriptor)
{
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const auto flags = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                       (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : DWORD{0});
    unique_handle pipe(CreateNamedPipeW(name.c_str(),
                                        flags,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                            PIPE_REJECT_REMOTE_CLIENTS,
                                        static_cast<DWORD>(slots),
                                        buffer_bytes,
                                        buffer_bytes,
                                        0,
                                        &attributes));
    if (!pipe) {
        return io_failure(GetLastError());
    }

    return std::make_shared<unique_handle>(std::move(pipe));
}

void signal_event::operator()() const noexcept
{
    SetEvent(event);
}

overlapped_operation::overlapped_operation(HANDLE handle, const std::stop_token& stop)
    : handle_(handle)
{
    completed_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completed_) {
        setup_error_ = GetLastError();
        return;
    }

    cancelled_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!cancelled_) {
        setup_error_ = GetLastError();
        return;
    }

    operation_.hEvent = completed_.get();
    callback_.emplace(stop, signal_event{cancelled_.get()});
}

overlapped_operation::~overlapped_operation()
{
    if (pending_) {
        // Safety fallback for any future early-return/unwinding path. The kernel
        // must stop referencing operation_ before its events/storage are freed.
        CancelIoEx(handle_, &operation_);
        DWORD transferred = 0;
        GetOverlappedResult(handle_, &operation_, &transferred, TRUE);
    }
}

std::optional<failure> overlapped_operation::setup_error() const
{
    return setup_error_ == ERROR_SUCCESS ? std::nullopt : std::optional{io_failure(setup_error_)};
}

OVERLAPPED* overlapped_operation::operation() noexcept
{
    return &operation_;
}

void overlapped_operation::mark_pending() noexcept
{
    pending_ = true;
}

detail::wake_result overlapped_operation::wait(deadline until)
{
    const std::array<HANDLE, 2> events{completed_.get(), cancelled_.get()};
    const auto signalled = WaitForMultipleObjects(
        static_cast<DWORD>(events.size()), events.data(), FALSE, remaining(until));
    if (signalled == WAIT_OBJECT_0) {
        return {detail::wake_reason::completed};
    }

    if (signalled == WAIT_OBJECT_0 + 1) {
        return {detail::wake_reason::stopped};
    }

    if (signalled == WAIT_TIMEOUT) {
        return {detail::wake_reason::expired};
    }

    return {detail::wake_reason::failed, GetLastError()};
}

std::uint32_t overlapped_operation::cancel()
{
    return CancelIoEx(handle_, &operation_) ? ERROR_SUCCESS : GetLastError();
}

result<std::size_t> overlapped_operation::complete(bool wait)
{
    DWORD transferred = 0;
    if (GetOverlappedResult(handle_, &operation_, &transferred, wait ? TRUE : FALSE)) {
        pending_ = false;
        return static_cast<std::size_t>(transferred);
    }

    const auto code = GetLastError();
    if (code == ERROR_IO_INCOMPLETE) {
        CancelIoEx(handle_, &operation_);
        GetOverlappedResult(handle_, &operation_, &transferred, TRUE);
    }

    pending_ = false;
    return io_failure(code);
}

result<std::size_t> accept(HANDLE pipe, deadline until, const std::stop_token& stop)
{
    if (auto error = preflight(until, stop)) {
        return *error;
    }

    overlapped_operation pending(pipe, stop);
    if (auto error = pending.setup_error()) {
        return *error;
    }

    if (ConnectNamedPipe(pipe, pending.operation())) {
        return std::size_t{0};
    }

    const auto code = GetLastError();
    if (code == ERROR_PIPE_CONNECTED) {
        return std::size_t{0};
    }

    if (code != ERROR_IO_PENDING) {
        return io_failure(code);
    }

    pending.mark_pending();
    return detail::resolve_pending(pending, until);
}

result<std::size_t>
read_some(HANDLE pipe, std::span<std::byte> buffer, deadline until, const std::stop_token& stop)
{
    if (auto error = preflight(until, stop)) {
        return *error;
    }

    overlapped_operation pending(pipe, stop);
    if (auto error = pending.setup_error()) {
        return *error;
    }

    DWORD transferred = 0;
    const auto count = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), buffer_bytes));
    if (ReadFile(pipe, buffer.data(), count, &transferred, pending.operation())) {
        return static_cast<std::size_t>(transferred);
    }

    const auto code = GetLastError();
    if (code != ERROR_IO_PENDING) {
        return io_failure(code);
    }

    pending.mark_pending();
    return detail::resolve_pending(pending, until);
}

result<std::size_t> write_some(HANDLE pipe,
                               std::span<const std::byte> buffer,
                               deadline until,
                               const std::stop_token& stop)
{
    if (auto error = preflight(until, stop)) {
        return *error;
    }

    overlapped_operation pending(pipe, stop);
    if (auto error = pending.setup_error()) {
        return *error;
    }

    DWORD transferred = 0;
    const auto count = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), buffer_bytes));
    if (WriteFile(pipe, buffer.data(), count, &transferred, pending.operation())) {
        return static_cast<std::size_t>(transferred);
    }

    const auto code = GetLastError();
    if (code != ERROR_IO_PENDING) {
        return io_failure(code);
    }

    pending.mark_pending();
    return detail::resolve_pending(pending, until);
}
} // namespace pipeforge::transport::native
