#include "pipeforge/client.hpp"

#include "pipe_test_support.hpp"

namespace pipeforge::client {
namespace {
namespace p = protocol;
namespace t = transport;
using t::test_support::endpoint;
using t::test_support::host;
using t::test_support::soon;

TEST(WindowsClient, ConcurrentLargeRequestsAndRepliesDoNotDeadlockTheDuplexPipe)
{
    const auto name = endpoint();
    auto server = host(name);
    ASSERT_NE(server, nullptr);
    connection client(named_pipe_connector());
    ASSERT_TRUE(std::holds_alternative<connected>(client.connect(name, soon())));
    const p::value input{std::string(p::max_string_bytes, 'x')};
    std::vector<std::future<call_result>> pending;
    pending.reserve(max_pending_calls);
    for (std::size_t index = 0; index < max_pending_calls; ++index) {
        pending.push_back(client.call_async(p::put_value{input}, soon()));
    }

    for (auto& future : pending) {
        const auto result = future.get();
        ASSERT_TRUE(std::holds_alternative<p::response>(result));
        const auto* record = std::get_if<p::value_record>(&std::get<p::response>(result).body);
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->data, input);
    }

    client.close();
    server->stop();
    EXPECT_EQ(server->statistics().active_sessions, 0);
    EXPECT_EQ(server->statistics().io_failures, 0);
}

TEST(WindowsClient, ServerExitIsTerminalAndClosingAnIdleReaderDrainsNativeIo)
{
    const auto name = endpoint();
    auto server = host(name);
    ASSERT_NE(server, nullptr);
    connection client(named_pipe_connector());
    ASSERT_TRUE(std::holds_alternative<connected>(client.connect_async(name, soon()).get()));
    EXPECT_TRUE(std::holds_alternative<p::response>(client.call(p::list_classes{}, soon())));
    server->stop();
    const auto result = client.call_async(p::list_classes{}, soon()).get();
    ASSERT_TRUE(std::holds_alternative<failure>(result));
    EXPECT_TRUE(std::get<failure>(result).kind == failure_kind::disconnected ||
                std::get<failure>(result).kind == failure_kind::not_connected);
    client.close();
    client.close();
}

TEST(WindowsClient, RepeatedClientAndHostLifecyclesDoNotAccumulateHandles)
{
    auto cycle = [] {
        const auto name = endpoint();
        auto server = host(name);
        ASSERT_NE(server, nullptr);
        connection client(named_pipe_connector());
        ASSERT_TRUE(std::holds_alternative<connected>(client.connect(name, soon())));
        EXPECT_TRUE(std::holds_alternative<p::response>(client.call(p::list_classes{}, soon())));
        client.close();
        server->stop();
    };
    cycle();
    DWORD before = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
    constexpr auto repetitions = 32;
    for (int iteration = 0; iteration < repetitions; ++iteration) {
        cycle();
    }

    DWORD after = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    constexpr DWORD lazy_runtime_handle_slack = 4;
    EXPECT_LE(after, before + lazy_runtime_handle_slack);
}
} // namespace
} // namespace pipeforge::client
