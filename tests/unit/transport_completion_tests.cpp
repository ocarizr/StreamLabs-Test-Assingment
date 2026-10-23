#include <gtest/gtest.h>

#include "completion.hpp"

namespace pipeforge::transport::detail {
namespace {
// Deliberate native-code and byte-count fixtures for completion races.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
class fake_completion final : public completion_backend {
  public:
    explicit fake_completion(wake_result wake, bool success = true) : wake_(wake), success_(success)
    {
    }

    [[nodiscard]] wake_result wait(deadline until) override
    {
        waited_until_ = until;
        return wake_;
    }

    [[nodiscard]] std::uint32_t cancel() override
    {
        cancelled_ = true;
        // Model ERROR_NOT_FOUND: the operation completed just before cancellation.
        return 1168;
    }

    [[nodiscard]] result<std::size_t> complete(bool wait) override
    {
        completed_ = true;
        drained_ = wait;
        return success_ ? result<std::size_t>{std::size_t{7}}
                        : result<std::size_t>{failure{failure_kind::disconnected, 109}};
    }

    [[nodiscard]] bool cancelled() const
    {
        return cancelled_;
    }

    [[nodiscard]] bool completed() const
    {
        return completed_;
    }

    [[nodiscard]] bool drained() const
    {
        return drained_;
    }

    [[nodiscard]] deadline waited_until() const
    {
        return waited_until_;
    }

  private:
    wake_result wake_;
    bool success_;
    bool cancelled_ = false;
    bool completed_ = false;
    bool drained_ = false;
    deadline waited_until_{};
};

TEST(TransportCompletion, SuccessfulWakeObservesBytesWithoutCancellation)
{
    fake_completion operation({wake_reason::completed});
    const auto until = deadline{std::chrono::milliseconds{123}};
    const auto result = resolve_pending(operation, until);
    ASSERT_TRUE(std::holds_alternative<std::size_t>(result));
    EXPECT_EQ(std::get<std::size_t>(result), 7);
    EXPECT_TRUE(operation.completed());
    EXPECT_FALSE(operation.cancelled());
    EXPECT_EQ(operation.waited_until(), until);
}

TEST(TransportCompletion, TimeoutAndCancellationDrainEvenWhenCompletionWinsTheRace)
{
    for (auto reason : {wake_reason::expired, wake_reason::stopped, wake_reason::failed}) {
        fake_completion operation({reason, 6});
        const auto result = resolve_pending(operation, deadline{});
        ASSERT_TRUE(std::holds_alternative<failure>(result));
        EXPECT_EQ(std::get<failure>(result).kind,
                  reason == wake_reason::expired   ? failure_kind::timeout
                  : reason == wake_reason::stopped ? failure_kind::cancelled
                                                   : failure_kind::io_failure);
        EXPECT_TRUE(operation.cancelled());
        EXPECT_TRUE(operation.completed());
        EXPECT_TRUE(operation.drained());
    }
}

TEST(TransportCompletion, PeerFailureIsPreservedAndCancellationStillDrains)
{
    fake_completion operation({wake_reason::completed}, false);
    const auto result = resolve_pending(operation, deadline{});
    ASSERT_TRUE(std::holds_alternative<failure>(result));
    EXPECT_EQ(std::get<failure>(result).kind, failure_kind::disconnected);
    EXPECT_EQ(std::get<failure>(result).native_error, 109);
    fake_completion cancelled({wake_reason::stopped}, false);
    EXPECT_TRUE(std::holds_alternative<failure>(resolve_pending(cancelled, deadline{})));
    EXPECT_TRUE(cancelled.drained());
}

TEST(TransportConfiguration, OnlyBoundedLocalPipeNamesAreAccepted)
{
    EXPECT_TRUE(valid_endpoint(default_endpoint));
    EXPECT_TRUE(valid_endpoint(L"\\\\.\\pipe\\pipeforge-test_1"));
    EXPECT_FALSE(valid_endpoint(L"\\\\remote\\pipe\\name"));
    EXPECT_FALSE(valid_endpoint(L"\\\\.\\pipe\\nested\\name"));
    EXPECT_FALSE(valid_endpoint(L"\\\\.\\pipe\\"));
    EXPECT_FALSE(valid_endpoint(std::wstring(300, L'a')));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::transport::detail
