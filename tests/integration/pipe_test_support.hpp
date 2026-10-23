#ifndef PIPEFORGE_PIPE_TEST_SUPPORT_HPP
#define PIPEFORGE_PIPE_TEST_SUPPORT_HPP
#include "pipeforge/pipe_server.hpp"

#include <atomic>

#include <gtest/gtest.h>

#include "domain_test_support.hpp"
#include "native.hpp"

namespace pipeforge::transport::test_support {
namespace p = protocol;
using ::pipeforge::test_support::registered_classes;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
inline deadline soon()
{
    return clock::now() + std::chrono::seconds{3};
}

inline std::wstring endpoint()
{
    static std::atomic<unsigned int> sequence{0};
    return L"\\\\.\\pipe\\pipeforge-m4-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
           std::to_wstring(++sequence);
}

inline std::unique_ptr<pipe_server> host(const std::wstring& name,
                                         std::size_t slots = 2,
                                         std::chrono::milliseconds timeout = default_timeout)
{
    auto started = pipe_server::start(registered_classes(), {name, timeout, slots});
    if (auto* instance = std::get_if<std::unique_ptr<pipe_server>>(&started)) {
        return std::move(*instance);
    }

    ADD_FAILURE() << "Named-pipe host must start; native="
                  << std::get<failure>(started).native_error;
    return nullptr;
}

inline p::response call(pipe_stream& stream, const p::request& request)
{
    const auto encoded = p::encode_request(request);
    EXPECT_TRUE(std::holds_alternative<p::bytes>(encoded));
    if (!std::holds_alternative<p::bytes>(encoded)) {
        return {};
    }

    const auto sent = stream.send(std::get<p::bytes>(encoded), soon());
    EXPECT_TRUE(std::holds_alternative<std::size_t>(sent));
    const auto received = stream.receive(soon());
    if (!std::holds_alternative<p::bytes>(received)) {
        ADD_FAILURE() << "Expected response; transport kind="
                      << static_cast<unsigned int>(std::get<failure>(received).kind);
        return {};
    }

    const auto decoded = p::decode_response(std::get<p::bytes>(received));
    EXPECT_TRUE(std::holds_alternative<p::response>(decoded));
    return std::holds_alternative<p::response>(decoded) ? std::get<p::response>(decoded)
                                                        : p::response{};
}

inline void raw_write(pipe_stream& stream, std::span<const std::byte> bytes)
{
    while (!bytes.empty()) {
        const auto sent = native::write_some(stream.native_handle(), bytes, soon(), {});
        ASSERT_TRUE(std::holds_alternative<std::size_t>(sent));
        const auto count = std::get<std::size_t>(sent);
        ASSERT_GT(count, 0);
        bytes = bytes.subspan(count);
    }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace pipeforge::transport::test_support
#endif // PIPEFORGE_PIPE_TEST_SUPPORT_HPP
