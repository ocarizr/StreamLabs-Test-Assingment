#ifndef PIPEFORGE_NATIVE_PIPE_HPP
#define PIPEFORGE_NATIVE_PIPE_HPP

#include <Windows.h>

#include "../completion.hpp"

namespace pipeforge::transport::native {
inline constexpr DWORD buffer_bytes = 16 * 1024;

/** Own exactly one real kernel handle; nullptr and INVALID_HANDLE_VALUE are empty. */
class unique_handle {
  public:
    explicit unique_handle(HANDLE handle = nullptr) noexcept;
    ~unique_handle();
    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;
    unique_handle(unique_handle&& other) noexcept;
    unique_handle& operator=(unique_handle&& other) noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] HANDLE get() const noexcept;
    void reset(HANDLE handle = nullptr) noexcept;
    [[nodiscard]] HANDLE release() noexcept;

  private:
    HANDLE handle_;
};

struct local_deleter {
    void operator()(void* memory) const noexcept;
};

using local_memory = std::unique_ptr<void, local_deleter>;

/** Protected DACL containing only the current process logon SID. Fails closed if absent. */
[[nodiscard]] result<local_memory> logon_security();
[[nodiscard]] result<std::shared_ptr<unique_handle>>
create_instance(const std::wstring& name, std::size_t slots, bool first, void* descriptor);
[[nodiscard]] failure io_failure(DWORD code);
[[nodiscard]] DWORD remaining(deadline until);
[[nodiscard]] std::optional<failure> preflight(deadline until, const std::stop_token& stop);

struct signal_event {
    HANDLE event;
    void operator()() const noexcept;
};

/**
 * Pinned operation storage. The stop callback signals its own event; only the
 * issuing thread cancels/drains the kernel operation. Members are ordered so
 * callback destruction completes before either event can be closed.
 */
class overlapped_operation final : public detail::completion_backend {
  public:
    overlapped_operation(HANDLE handle, const std::stop_token& stop);
    ~overlapped_operation() override;
    overlapped_operation(const overlapped_operation&) = delete;
    overlapped_operation& operator=(const overlapped_operation&) = delete;
    overlapped_operation(overlapped_operation&&) = delete;
    overlapped_operation& operator=(overlapped_operation&&) = delete;
    [[nodiscard]] std::optional<failure> setup_error() const;
    [[nodiscard]] OVERLAPPED* operation() noexcept;
    void mark_pending() noexcept;
    [[nodiscard]] detail::wake_result wait(deadline until) override;
    [[nodiscard]] std::uint32_t cancel() override;
    [[nodiscard]] result<std::size_t> complete(bool wait) override;

  private:
    HANDLE handle_;
    OVERLAPPED operation_{};
    unique_handle completed_;
    unique_handle cancelled_;
    DWORD setup_error_ = ERROR_SUCCESS;
    bool pending_ = false;
    std::optional<std::stop_callback<signal_event>> callback_;
};

[[nodiscard]] result<std::size_t> accept(HANDLE pipe, deadline until, const std::stop_token& stop);
[[nodiscard]] result<std::size_t>
read_some(HANDLE pipe, std::span<std::byte> buffer, deadline until, const std::stop_token& stop);
[[nodiscard]] result<std::size_t> write_some(HANDLE pipe,
                                             std::span<const std::byte> buffer,
                                             deadline until,
                                             const std::stop_token& stop);
} // namespace pipeforge::transport::native
#endif // PIPEFORGE_NATIVE_PIPE_HPP
