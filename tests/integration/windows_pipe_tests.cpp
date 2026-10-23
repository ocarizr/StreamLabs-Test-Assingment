#include "pipeforge/pipe_server.hpp"

#include <atomic>
#include <future>

#include <gtest/gtest.h>

#include "domain_test_support.hpp"
#include "native.hpp"
#include "pipe_test_support.hpp"

namespace pipeforge::transport {
namespace {
namespace p = protocol;
using test_support::registered_classes;

// Deliberate wire-byte, timeout, ID and native-error fixtures.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
using test_support::call;
using test_support::endpoint;
using test_support::host;
using test_support::raw_write;
using test_support::soon;

TEST(PipeTransport, RoundTripsTypedValuesAndKeepsSessionState)
{
    const auto name = endpoint();
    auto server = host(name);
    ASSERT_NE(server, nullptr);
    auto connected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    auto stream = std::move(std::get<pipe_stream>(connected));
    const auto saved = call(stream, {1, p::put_value{std::string("hello pipe")}});
    const auto* value = std::get_if<p::value_record>(&saved.body);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(call(stream, {2, p::get_value{value->id}}).body, saved.body);
    const auto made =
        call(stream, {3, p::create_object{"Counter", {{"initial", std::int64_t{7}}}}});
    const auto* object = std::get_if<p::object_snapshot>(&made.body);
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(
        call(stream, {4, p::invoke_method{object->id, "increment", {{"delta", std::int64_t{2}}}}})
            .body,
        p::outcome{p::value{std::int64_t{9}}});
    stream.close();
    server->stop();
    EXPECT_EQ(server->statistics().active_sessions, 0);
    EXPECT_EQ(server->statistics().completed_requests, 4);
}

TEST(PipeTransport, FragmentedHeadersAndConcatenatedFramesPreserveOrder)
{
    const auto name = endpoint();
    auto server = host(name);
    ASSERT_NE(server, nullptr);
    auto connected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    auto stream = std::move(std::get<pipe_stream>(connected));
    const auto encoded = p::encode_request({1, p::list_classes{}});
    ASSERT_TRUE(std::holds_alternative<p::bytes>(encoded));
    const auto framed = p::frame(std::get<p::bytes>(encoded));
    ASSERT_TRUE(std::holds_alternative<p::bytes>(framed));
    const auto& bytes = std::get<p::bytes>(framed);
    for (auto byte : bytes) {
        raw_write(stream, std::span{&byte, std::size_t{1}});
    }

    auto joined = bytes;
    joined.insert(joined.end(), bytes.begin(), bytes.end());
    raw_write(stream, joined);
    for (int index = 0; index < 3; ++index) {
        const auto received = stream.receive(soon());
        ASSERT_TRUE(std::holds_alternative<p::bytes>(received));
        const auto response = p::decode_response(std::get<p::bytes>(received));
        ASSERT_TRUE(std::holds_alternative<p::response>(response));
        EXPECT_TRUE(std::holds_alternative<p::classes_page>(std::get<p::response>(response).body));
    }
}

TEST(PipeTransport, SemanticErrorsAreCorrelatedButMalformedFramesCloseTheSession)
{
    const auto name = endpoint();
    auto server = host(name);
    ASSERT_NE(server, nullptr);
    auto connected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    auto stream = std::move(std::get<pipe_stream>(connected));
    const p::bytes no_operation{std::byte{0x08}, std::byte{1}, std::byte{0x10}, std::byte{42}};
    ASSERT_TRUE(std::holds_alternative<std::size_t>(stream.send(no_operation, soon())));
    const auto received = stream.receive(soon());
    ASSERT_TRUE(std::holds_alternative<p::bytes>(received));
    const auto decoded = p::decode_response(std::get<p::bytes>(received));
    ASSERT_TRUE(std::holds_alternative<p::response>(decoded));
    EXPECT_EQ(std::get<p::response>(decoded).id, 42);
    EXPECT_TRUE(std::holds_alternative<p::remote_error>(std::get<p::response>(decoded).body));
    EXPECT_EQ(call(stream, {43, p::list_classes{}}).id, 43);
    const p::bytes malformed{std::byte{0x80}};
    ASSERT_TRUE(std::holds_alternative<std::size_t>(stream.send(malformed, soon())));
    const auto closed = stream.receive(soon());
    ASSERT_TRUE(std::holds_alternative<failure>(closed));
    EXPECT_EQ(std::get<failure>(closed).kind, failure_kind::disconnected);
}

TEST(PipeTransport, BoundedSessionsIsolationAndListenerOwnership)
{
    const auto name = endpoint();
    auto server = host(name, 1);
    ASSERT_NE(server, nullptr);
    const auto occupied = pipe_server::start(registered_classes(), {name});
    ASSERT_TRUE(std::holds_alternative<failure>(occupied));
    auto first = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(first));
    auto stream = std::move(std::get<pipe_stream>(first));
    const auto made =
        call(stream, {1, p::create_object{"Counter", {{"initial", std::int64_t{0}}}}});
    const auto* snapshot = std::get_if<p::object_snapshot>(&made.body);
    ASSERT_NE(snapshot, nullptr);
    const auto busy = pipe_stream::connect(name, clock::now() + std::chrono::milliseconds{50});
    ASSERT_TRUE(std::holds_alternative<failure>(busy));
    EXPECT_EQ(std::get<failure>(busy).kind, failure_kind::timeout);
    stream.close();
    auto reconnect = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(reconnect));
    auto again = std::move(std::get<pipe_stream>(reconnect));
    const auto stale = call(again, {1, p::get_object{snapshot->id}});
    const auto* error = std::get_if<p::remote_error>(&stale.body);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, p::error_code::not_found);
    server->stop();
    server->stop();
    auto restarted = host(name, 1);
    ASSERT_NE(restarted, nullptr);
}

TEST(PipeTransport, PendingReadTimeoutAndPreCancelledConnectReturnTypedErrors)
{
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = pipe_stream::connect(endpoint(), soon(), stop.get_token());
    ASSERT_TRUE(std::holds_alternative<failure>(cancelled));
    EXPECT_EQ(std::get<failure>(cancelled).kind, failure_kind::cancelled);
    const auto name = endpoint();
    auto server = host(name);
    ASSERT_NE(server, nullptr);
    auto connected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    auto stream = std::move(std::get<pipe_stream>(connected));
    const auto timed_out = stream.receive(clock::now() + std::chrono::milliseconds{50});
    ASSERT_TRUE(std::holds_alternative<failure>(timed_out));
    EXPECT_EQ(std::get<failure>(timed_out).kind, failure_kind::timeout);
    server->stop();
    EXPECT_EQ(server->statistics().active_sessions, 0);
}

TEST(PipeTransport, ShutdownCancelsIdleAcceptsAndPendingReads)
{
    const auto name = endpoint();
    auto server = host(name, 4);
    ASSERT_NE(server, nullptr);
    auto connected = pipe_stream::connect(name, soon());
    ASSERT_TRUE(std::holds_alternative<pipe_stream>(connected));
    auto stream = std::move(std::get<pipe_stream>(connected));
    EXPECT_EQ(call(stream, {1, p::list_classes{}}).id, 1);
    server->request_stop();
    server->stop();
    const auto closed = stream.receive(soon());
    ASSERT_TRUE(std::holds_alternative<failure>(closed));
    EXPECT_EQ(std::get<failure>(closed).kind, failure_kind::disconnected);
    EXPECT_EQ(server->statistics().active_sessions, 0);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::transport
