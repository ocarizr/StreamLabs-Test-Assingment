#include <filesystem>
#include <future>
#include <set>

#include "pipe_test_support.hpp"

namespace pipeforge::transport {
namespace {
namespace p = protocol;
using test_support::call;
using test_support::endpoint;
using test_support::host;
using test_support::raw_write;
using test_support::soon;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
TEST(PipeFailures, OversizedAndZeroLengthFramesAreRejectedBeforePayload)
{
    for (auto header : {p::bytes(4, std::byte{0}),
                        p::bytes{std::byte{1}, std::byte{0}, std::byte{0x10}, std::byte{0}}}) {
        const auto name = endpoint();
        auto server = host(name);
        ASSERT_NE(server, nullptr);
        auto connected = pipe_stream::connect(name, soon());
        ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
        auto stream = std::move(std::get<pipe_stream>(connected));
        raw_write(stream, header);
        const auto closed = stream.receive(soon());
        ASSERT_TRUE(std::holds_alternative<failure>(closed));
        EXPECT_EQ(std::get<failure>(closed).kind, failure_kind::disconnected);
        server->stop();
        EXPECT_EQ(server->statistics().protocol_failures, 1);
    }
}

TEST(PipeFailures, PartialFrameDeadlineClosesWithoutInventingACorrelationId)
{
    const auto name = endpoint();
    auto server = host(name, 1, std::chrono::milliseconds{100});
    ASSERT_NE(server, nullptr);
    auto connected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    auto stream = std::move(std::get<pipe_stream>(connected));
    const std::array<std::byte, 2> header{std::byte{1}, std::byte{0}};
    raw_write(stream, header);
    const auto closed = stream.receive(soon());
    ASSERT_TRUE(std::holds_alternative<failure>(closed));
    EXPECT_EQ(std::get<failure>(closed).kind, failure_kind::disconnected);
    server->stop();
    EXPECT_EQ(server->statistics().io_failures, 1);
}

TEST(PipeFailures, IndependentSessionsDispatchConcurrentlyAndPreserveOwnership)
{
    const auto name = endpoint();
    auto server = host(name, max_sessions);
    ASSERT_NE(server, nullptr);
    std::vector<std::future<std::uint64_t>> calls;
    calls.reserve(max_sessions);
    for (std::size_t session = 0; session < max_sessions; ++session) {
        calls.push_back(std::async(std::launch::async, [&name]() -> std::uint64_t {
            auto connection = pipe_stream::connect(name, soon());
            EXPECT_TRUE(std::holds_alternative<pipe_stream>(connection));
            if (!std::holds_alternative<pipe_stream>(connection)) {
                return 0;
            }

            auto stream = std::move(std::get<pipe_stream>(connection));
            auto response =
                call(stream, {1, p::create_object{"Counter", {{"initial", std::int64_t{0}}}}});
            const auto* snapshot = std::get_if<p::object_snapshot>(&response.body);
            EXPECT_NE(snapshot, nullptr);
            if (!snapshot) {
                return 0;
            }

            const auto id = snapshot->id;
            for (int index = 0; index < 16; ++index) {
                const auto result =
                    call(stream,
                         {static_cast<std::uint64_t>(index) + 2,
                          p::invoke_method{id, "increment", {{"delta", std::int64_t{1}}}}});
                EXPECT_EQ(result.body, p::outcome{p::value{std::int64_t{index} + 1}});
            }

            return id;
        }));
    }

    std::set<std::uint64_t> identities;
    for (auto& pending : calls) {
        EXPECT_NE(pending.wait_for(std::chrono::seconds{10}), std::future_status::timeout);
        identities.insert(pending.get());
    }

    EXPECT_EQ(identities.size(), max_sessions);
    EXPECT_FALSE(identities.contains(0));
    server->stop();
    EXPECT_EQ(server->statistics().active_sessions, 0);
}

TEST(PipeFailures, RepeatedStartupDisconnectShutdownDoesNotAccumulateHandles)
{
    auto cycle = [] {
        const auto name = endpoint();
        auto server = host(name, 1);
        ASSERT_NE(server, nullptr);
        auto connected = pipe_stream::connect(name, soon());
        ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
        auto stream = std::move(std::get<pipe_stream>(connected));
        EXPECT_EQ(call(stream, {1, p::list_classes{}}).id, 1);
        stream.close();
        server->stop();
    };
    cycle();
    DWORD before = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int index = 0; index < 32; ++index) {
        cycle();
    }

    DWORD after = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    constexpr DWORD lazy_runtime_handle_slack = 4;
    EXPECT_LE(after, before + lazy_runtime_handle_slack);
}

TEST(PipeFailures, AbruptPeerProcessExitDuringHeaderClosesAndReleasesTheSession)
{
    const auto name = endpoint();
    auto server = host(name, 1);
    ASSERT_NE(server, nullptr);
    const auto executable = std::filesystem::path(PIPEFORGE_PEER_PATH).wstring();
    auto command = L"\"" + executable + L"\" \"" + name + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    ASSERT_TRUE(CreateProcessW(executable.c_str(),
                               command.data(),
                               nullptr,
                               nullptr,
                               FALSE,
                               CREATE_NO_WINDOW,
                               nullptr,
                               nullptr,
                               &startup,
                               &process));
    native::unique_handle child(process.hProcess);
    const native::unique_handle thread(process.hThread);
    const auto waited = WaitForSingleObject(child.get(), 5000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), INFINITE);
    }

    ASSERT_EQ(waited, WAIT_OBJECT_0);
    DWORD exit_code = 0;
    ASSERT_TRUE(GetExitCodeProcess(child.get(), &exit_code));
    ASSERT_EQ(exit_code, 0);
    // Reconnection waits for the same anchored instance to finish cleanup.
    auto reconnected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(reconnected));
    auto stream = std::move(std::get<pipe_stream>(reconnected));
    EXPECT_EQ(call(stream, {1, p::list_classes{}}).id, 1);
    server->stop();
    EXPECT_EQ(server->statistics().active_sessions, 0);
    EXPECT_EQ(server->statistics().protocol_failures, 1);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::transport
