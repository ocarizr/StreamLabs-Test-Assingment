#include "pipeforge/client.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>

#include <gtest/gtest.h>

namespace pipeforge::client {
namespace {
namespace p = protocol;
namespace t = transport;

// Deliberate deadlines, correlation IDs and payload values.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
struct peer_state {
    std::mutex mutex;
    std::condition_variable_any changed;
    bool allow_connect = true;
    bool allow_write = true;
    bool closed = false;
    std::deque<p::request> requests;
    std::deque<p::bytes> responses;
    std::optional<t::failure> read_failure;
};

t::deadline soon()
{
    return t::clock::now() + std::chrono::seconds{3};
}

class controlled_channel final : public channel {
  public:
    explicit controlled_channel(std::shared_ptr<peer_state> state) : state_(std::move(state)) {}

    t::result<std::size_t> send(std::span<const std::byte> payload,
                                t::deadline until,
                                const std::stop_token& stop) override
    {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_until(lock, stop, until, [this] {
                return state_->allow_write;
            })) {
            return t::failure{stop.stop_requested() ? t::failure_kind::cancelled
                                                    : t::failure_kind::timeout};
        }

        auto decoded = p::decode_request(payload);
        if (!std::holds_alternative<p::request>(decoded)) {
            return t::failure{t::failure_kind::protocol_violation};
        }

        state_->requests.push_back(std::get<p::request>(std::move(decoded)));
        state_->changed.notify_all();
        return payload.size();
    }

    t::result<p::bytes> receive(t::deadline until, const std::stop_token& stop) override
    {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_until(lock, stop, until, [this] {
                return !state_->responses.empty() || state_->read_failure.has_value();
            })) {
            return t::failure{stop.stop_requested() ? t::failure_kind::cancelled
                                                    : t::failure_kind::timeout};
        }

        if (state_->read_failure) {
            return *state_->read_failure;
        }

        auto payload = std::move(state_->responses.front());
        state_->responses.pop_front();
        return payload;
    }

    void close() noexcept override
    {
        state_->closed = true;
    }

  private:
    std::shared_ptr<peer_state> state_;
};

connector controlled_connector(const std::shared_ptr<peer_state>& state)
{
    return [state](std::wstring_view,
                   t::deadline until,
                   const std::stop_token& stop) -> t::result<std::unique_ptr<channel>> {
        std::unique_lock lock(state->mutex);
        if (!state->changed.wait_until(lock, stop, until, [&state] {
                return state->allow_connect;
            })) {
            return t::failure{stop.stop_requested() ? t::failure_kind::cancelled
                                                    : t::failure_kind::timeout};
        }

        return std::make_unique<controlled_channel>(state);
    };
}

std::vector<p::request> take_requests(const std::shared_ptr<peer_state>& state, std::size_t count)
{
    std::unique_lock lock(state->mutex);
    const auto ready = state->changed.wait_until(lock, soon(), [&] {
        return state->requests.size() >= count;
    });
    EXPECT_TRUE(ready);
    std::vector<p::request> requests(state->requests.begin(), state->requests.end());
    state->requests.clear();
    return requests;
}

void reply(const std::shared_ptr<peer_state>& state, const p::response& response)
{
    auto encoded = p::encode_response(response);
    ASSERT_TRUE(std::holds_alternative<p::bytes>(encoded));
    const std::lock_guard lock(state->mutex);
    state->responses.push_back(std::get<p::bytes>(std::move(encoded)));
    state->changed.notify_all();
}

TEST(Client, ConnectAndCallReturnPendingFuturesWithoutWaitingForThePeer)
{
    auto peer = std::make_shared<peer_state>();
    peer->allow_connect = false;
    connection client(controlled_connector(peer));
    auto opening = client.connect_async(std::wstring(t::default_endpoint), soon());
    EXPECT_EQ(opening.wait_for(std::chrono::milliseconds::zero()), std::future_status::timeout);
    {
        const std::lock_guard lock(peer->mutex);
        peer->allow_connect = true;
        peer->changed.notify_all();
    }
    ASSERT_TRUE(std::holds_alternative<connected>(opening.get()));
    auto pending = client.call_async(p::list_classes{}, soon());
    EXPECT_EQ(pending.wait_for(std::chrono::milliseconds::zero()), std::future_status::timeout);
    auto requests = take_requests(peer, 1);
    ASSERT_EQ(requests.size(), 1);
    reply(peer, {requests.front().id, p::classes_page{{"Counter"}, {}}});
    ASSERT_TRUE(std::holds_alternative<p::response>(pending.get()));
}

TEST(Client, CorrelatesOutOfOrderRepliesAndUsesTheSamePathForBlockingCalls)
{
    auto peer = std::make_shared<peer_state>();
    connection client(controlled_connector(peer));
    ASSERT_TRUE(std::holds_alternative<connected>(
        client.connect(std::wstring(t::default_endpoint), soon())));
    auto first = client.call_async(p::put_value{std::int64_t{7}}, soon());
    auto second = std::async(std::launch::async, [&] {
        return client.call(p::put_value{std::int64_t{8}}, soon());
    });
    auto requests = take_requests(peer, 2);
    ASSERT_EQ(requests.size(), 2);
    for (auto iterator = requests.rbegin(); iterator != requests.rend(); ++iterator) {
        reply(peer,
              {iterator->id,
               p::value_record{iterator->id, std::get<p::put_value>(iterator->body).data}});
    }

    auto one = first.get();
    auto two = second.get();
    ASSERT_TRUE(std::holds_alternative<p::response>(one));
    ASSERT_TRUE(std::holds_alternative<p::response>(two));
    EXPECT_EQ(std::get<p::value_record>(std::get<p::response>(one).body).data,
              p::value{std::int64_t{7}});
    EXPECT_EQ(std::get<p::value_record>(std::get<p::response>(two).body).data,
              p::value{std::int64_t{8}});
}

TEST(Client, UnknownIdsAndUnexpectedOutcomesFailEveryPendingPromise)
{
    for (bool wrong_id : {false, true}) {
        auto peer = std::make_shared<peer_state>();
        connection client(controlled_connector(peer));
        ASSERT_TRUE(std::holds_alternative<connected>(
            client.connect(std::wstring(t::default_endpoint), soon())));
        auto first = client.call_async(p::list_classes{}, soon());
        auto second = client.call_async(p::list_classes{}, soon());
        auto requests = take_requests(peer, 2);
        ASSERT_EQ(requests.size(), 2);
        reply(peer,
              {wrong_id ? requests.front().id + 100 : requests.front().id,
               wrong_id ? p::outcome{p::classes_page{}} : p::outcome{p::released{}}});
        for (auto* pending : {&first, &second}) {
            auto result = pending->get();
            ASSERT_TRUE(std::holds_alternative<failure>(result));
            EXPECT_EQ(std::get<failure>(result).kind, failure_kind::protocol_violation);
        }

        EXPECT_TRUE(std::holds_alternative<failure>(client.call(p::list_classes{}, soon())));
    }
}

TEST(Client, BoundsPendingCallsAndCancelsQueuedRequestsWithoutClosingTheSession)
{
    auto peer = std::make_shared<peer_state>();
    peer->allow_write = false;
    connection client(controlled_connector(peer));
    ASSERT_TRUE(std::holds_alternative<connected>(
        client.connect(std::wstring(t::default_endpoint), soon())));
    auto first = client.call_async(p::list_classes{}, soon());
    std::stop_source cancellation;
    std::vector<std::future<call_result>> queued;
    queued.reserve(max_pending_calls - 1);
    for (std::size_t index = 1; index < max_pending_calls; ++index) {
        queued.push_back(client.call_async(p::list_classes{}, soon(), cancellation.get_token()));
    }

    auto rejected = client.call(p::list_classes{}, soon());
    ASSERT_TRUE(std::holds_alternative<failure>(rejected));
    EXPECT_EQ(std::get<failure>(rejected).kind, failure_kind::quota_exceeded);
    cancellation.request_stop();
    for (auto& pending : queued) {
        const auto result = pending.get();
        ASSERT_TRUE(std::holds_alternative<failure>(result));
        EXPECT_EQ(std::get<failure>(result).kind, failure_kind::cancelled);
    }

    auto replacement = client.call_async(p::list_classes{}, soon());
    {
        const std::lock_guard lock(peer->mutex);
        peer->allow_write = true;
        peer->changed.notify_all();
    }
    auto requests = take_requests(peer, 2);
    ASSERT_EQ(requests.size(), 2);
    for (const auto& request : requests) {
        reply(peer, {request.id, p::classes_page{}});
    }

    EXPECT_TRUE(std::holds_alternative<p::response>(first.get()));
    EXPECT_TRUE(std::holds_alternative<p::response>(replacement.get()));
}

TEST(Client, DeadlinesCancellationAndShutdownResolvePendingCallsExactlyOnce)
{
    for (bool cancel : {false, true}) {
        auto peer = std::make_shared<peer_state>();
        connection client(controlled_connector(peer));
        ASSERT_TRUE(std::holds_alternative<connected>(
            client.connect(std::wstring(t::default_endpoint), soon())));
        std::stop_source cancellation;
        auto pending =
            client.call_async(p::list_classes{},
                              cancel ? soon() : t::clock::now() + std::chrono::milliseconds{100},
                              cancellation.get_token());
        ASSERT_EQ(take_requests(peer, 1).size(), 1);
        if (cancel) {
            cancellation.request_stop();
        }

        auto result = pending.get();
        ASSERT_TRUE(std::holds_alternative<failure>(result));
        EXPECT_EQ(std::get<failure>(result).kind,
                  cancel ? failure_kind::cancelled : failure_kind::timeout);
        client.close();
        client.close();
        EXPECT_TRUE(peer->closed);
    }

    auto peer = std::make_shared<peer_state>();
    connection client(controlled_connector(peer));
    ASSERT_TRUE(std::holds_alternative<connected>(
        client.connect(std::wstring(t::default_endpoint), soon())));
    auto pending = client.call_async(p::list_classes{}, soon());
    client.close();
    EXPECT_EQ(std::get<failure>(pending.get()).kind, failure_kind::cancelled);
}

TEST(Client, PendingConnectCancellationAndPreexpiredConnectDoNotPublishSuccess)
{
    auto peer = std::make_shared<peer_state>();
    peer->allow_connect = false;
    connection client(controlled_connector(peer));
    std::stop_source stop;
    auto opening =
        client.connect_async(std::wstring(t::default_endpoint), soon(), stop.get_token());
    stop.request_stop();
    const auto cancelled = opening.get();
    ASSERT_TRUE(std::holds_alternative<failure>(cancelled));
    EXPECT_EQ(std::get<failure>(cancelled).kind, failure_kind::cancelled);
    client.close();

    connection expired(controlled_connector(std::make_shared<peer_state>()));
    const auto timed_out = expired.connect(std::wstring(t::default_endpoint), t::clock::now());
    ASSERT_TRUE(std::holds_alternative<failure>(timed_out));
    EXPECT_EQ(std::get<failure>(timed_out).kind, failure_kind::timeout);
}

TEST(Client, InvalidCallsAreLocalAndQueuedTimeoutDoesNotPoisonAnUnsentSession)
{
    auto peer = std::make_shared<peer_state>();
    peer->allow_write = false;
    connection client(controlled_connector(peer));
    EXPECT_EQ(std::get<failure>(client.call(p::list_classes{}, soon())).kind,
              failure_kind::not_connected);
    ASSERT_TRUE(std::holds_alternative<connected>(
        client.connect(std::wstring(t::default_endpoint), soon())));
    EXPECT_EQ(std::get<failure>(client.call(p::get_value{0}, soon())).kind,
              failure_kind::invalid_request);
    auto first = client.call_async(p::list_classes{}, soon());
    auto expired =
        client.call_async(p::list_classes{}, t::clock::now() + std::chrono::milliseconds{50});
    const auto timed_out = expired.get();
    ASSERT_TRUE(std::holds_alternative<failure>(timed_out));
    EXPECT_EQ(std::get<failure>(timed_out).kind, failure_kind::timeout);
    {
        const std::lock_guard lock(peer->mutex);
        peer->allow_write = true;
        peer->changed.notify_all();
    }
    auto requests = take_requests(peer, 1);
    ASSERT_EQ(requests.size(), 1);
    reply(peer, {requests.front().id, p::classes_page{}});
    EXPECT_TRUE(std::holds_alternative<p::response>(first.get()));
}

TEST(Client, MalformedRepliesDuplicateRepliesAndPeerExitFailPredictably)
{
    for (int mode : {0, 1, 2}) {
        auto peer = std::make_shared<peer_state>();
        connection client(controlled_connector(peer));
        ASSERT_TRUE(std::holds_alternative<connected>(
            client.connect(std::wstring(t::default_endpoint), soon())));
        auto first = client.call_async(p::list_classes{}, soon());
        auto requests = take_requests(peer, 1);
        ASSERT_EQ(requests.size(), 1);
        if (mode == 1) {
            reply(peer, {requests.front().id, p::classes_page{}});
            EXPECT_TRUE(std::holds_alternative<p::response>(first.get()));
            first = client.call_async(p::list_classes{}, soon());
            ASSERT_EQ(take_requests(peer, 1).size(), 1);
            reply(peer, {requests.front().id, p::classes_page{}});
        }
        else {
            const std::lock_guard lock(peer->mutex);
            if (mode == 0) {
                peer->responses.push_back(p::bytes{std::byte{0x80}});
            }
            else {
                peer->read_failure = t::failure{t::failure_kind::disconnected, 109};
            }

            peer->changed.notify_all();
        }

        const auto result = first.get();
        ASSERT_TRUE(std::holds_alternative<failure>(result));
        EXPECT_EQ(std::get<failure>(result).kind,
                  mode == 2 ? failure_kind::disconnected : failure_kind::protocol_violation);
    }
}

TEST(Client, ConcurrentSubmittersReceiveTheirOwnResponsesAtTheFullAdmissionBound)
{
    auto peer = std::make_shared<peer_state>();
    connection client(controlled_connector(peer));
    ASSERT_TRUE(std::holds_alternative<connected>(
        client.connect(std::wstring(t::default_endpoint), soon())));
    std::vector<std::future<std::future<call_result>>> submissions;
    submissions.reserve(max_pending_calls);
    for (std::size_t index = 0; index < max_pending_calls; ++index) {
        submissions.push_back(std::async(std::launch::async, [&client, index] {
            return client.call_async(p::put_value{static_cast<std::int64_t>(index)}, soon());
        }));
    }

    auto requests = take_requests(peer, max_pending_calls);
    ASSERT_EQ(requests.size(), max_pending_calls);
    for (auto iterator = requests.rbegin(); iterator != requests.rend(); ++iterator) {
        reply(peer,
              {iterator->id,
               p::value_record{iterator->id, std::get<p::put_value>(iterator->body).data}});
    }

    for (std::size_t index = 0; index < submissions.size(); ++index) {
        const auto result = submissions.at(index).get().get();
        ASSERT_TRUE(std::holds_alternative<p::response>(result));
        EXPECT_EQ(std::get<p::value_record>(std::get<p::response>(result).body).data,
                  p::value{static_cast<std::int64_t>(index)});
    }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::client
