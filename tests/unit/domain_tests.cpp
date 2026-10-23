#include "pipeforge/domain.hpp"

#include <limits>

#include <gtest/gtest.h>

#include "domain_test_support.hpp"

namespace pipeforge::domain {
namespace {
namespace p = protocol;

// IDs, quotas and values below are behavioral boundary fixtures.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
using test_support::expect_result;
using test_support::registered_classes;

class domain_test : public testing::Test {
  protected:
    [[nodiscard]] session_store& store()
    {
        return store_;
    }

    [[nodiscard]] session_store new_session(session_limits limits = {})
    {
        return {registry_, ids_, limits};
    }

    template <typename T> T call(const p::operation& operation)
    {
        return expect_result<T>(store().execute(operation));
    }

    std::uint64_t counter(std::int64_t initial = 0)
    {
        return call<p::object_snapshot>(p::create_object{"Counter", {{"initial", initial}}}).id;
    }

    void error(const p::operation& operation, p::error_code code)
    {
        EXPECT_EQ(call<p::remote_error>(operation).code, code);
    }

  private:
    class_registry registry_ = registered_classes();
    identifier_source ids_;
    session_store store_{registry_, ids_};
};

TEST_F(domain_test, PreservesTypedValuesAndExclusivePagination)
{
    const std::vector<p::value> inputs{std::string("ol\xc3\xa1"), std::int64_t{-9}, -0.0};
    std::vector<std::uint64_t> ids;
    for (const auto& input : inputs) {
        const auto saved = call<p::value_record>(p::put_value{input});
        EXPECT_NE(saved.id, 0);
        ids.push_back(saved.id);
        EXPECT_EQ(saved.data, input);
        EXPECT_EQ(call<p::value_record>(p::get_value{saved.id}), saved);
    }

    const auto first = call<p::values_page>(p::list_values{0, 2});
    ASSERT_EQ(first.values.size(), 2);
    EXPECT_EQ(first.next, ids.at(1));
    const auto last = call<p::values_page>(p::list_values{first.next, 2});
    ASSERT_EQ(last.values.size(), 1);
    EXPECT_EQ(last.values.front().id, ids.back());
    EXPECT_EQ(last.next, 0);
    EXPECT_TRUE(call<p::values_page>(p::list_values{ids.back(), 1}).values.empty());
}

TEST_F(domain_test, CounterRetainsIndependentStateAndChecksArguments)
{
    const auto first = counter(10);
    const auto second = counter(-3);
    EXPECT_EQ(call<p::value>(p::invoke_method{first, "increment", {{"delta", std::int64_t{4}}}}),
              p::value{std::int64_t{14}});
    EXPECT_EQ(call<p::value>(p::get_attribute{first, "value"}), p::value{std::int64_t{14}});
    EXPECT_EQ(call<p::value>(p::get_attribute{second, "value"}), p::value{std::int64_t{-3}});
    EXPECT_EQ(call<p::object_snapshot>(p::get_object{first}).state,
              (std::vector<p::named_value>{{"value", std::int64_t{14}}}));
    error(p::invoke_method{first, "increment", {}}, p::error_code::invalid_request);
    error(p::invoke_method{first, "increment", {{"delta", std::string("4")}}},
          p::error_code::invalid_request);
    error(p::invoke_method{first,
                           "increment",
                           {{"delta", std::int64_t{4}}, {"delta", std::int64_t{5}}}},
          p::error_code::invalid_request);
    error(p::invoke_method{first, "missing", {}}, p::error_code::not_found);
    error(p::get_attribute{first, "missing"}, p::error_code::not_found);
    error(p::create_object{"Counter", {{"wrong", std::int64_t{0}}}},
          p::error_code::invalid_request);
}

TEST_F(domain_test, RejectsBothIntegerOverflowsWithoutMutation)
{
    for (const auto initial :
         {std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min()}) {
        const auto id = counter(initial);
        const auto delta = initial > 0 ? std::int64_t{1} : std::int64_t{-1};
        error(p::invoke_method{id, "increment", {{"delta", delta}}},
              p::error_code::numeric_overflow);
        EXPECT_EQ(call<p::value>(p::get_attribute{id, "value"}), p::value{initial});
        EXPECT_EQ(call<p::value>(p::invoke_method{id, "increment", {{"delta", -delta}}}),
                  p::value{initial - delta});
        const auto extreme = counter();
        EXPECT_EQ(call<p::value>(p::invoke_method{extreme, "increment", {{"delta", initial}}}),
                  p::value{initial});
        error(p::invoke_method{extreme, "increment", {{"delta", initial}}},
              p::error_code::numeric_overflow);
        EXPECT_EQ(call<p::value>(p::get_attribute{extreme, "value"}), p::value{initial});
    }
}

TEST_F(domain_test, SecondClassRequiresNoDispatcherChanges)
{
    const auto id = call<p::object_snapshot>(
                        p::create_object{"TextBuffer", {{"initial", std::string("hello")}}})
                        .id;
    EXPECT_EQ(call<p::value>(p::invoke_method{id, "append", {{"suffix", std::string(" world")}}}),
              p::value{std::string("hello world")});
    EXPECT_EQ(call<p::value>(p::get_attribute{id, "text"}), p::value{std::string("hello world")});
    const auto description = call<p::class_description>(p::describe_class{"TextBuffer"});
    ASSERT_EQ(description.methods.size(), 1);
    EXPECT_EQ(description.methods.front().result_kind, p::value_kind::text);
    const auto page = call<p::classes_page>(p::list_classes{"", 1});
    EXPECT_EQ(page.names, std::vector<std::string>{"Counter"});
    EXPECT_EQ(page.next, "Counter");
    EXPECT_EQ(call<p::classes_page>(p::list_classes{page.next, 1}).names,
              std::vector<std::string>{"TextBuffer"});
}

TEST_F(domain_test, SessionIsolationReleaseAndDisconnectInvalidateIds)
{
    auto other = new_session();
    const auto object_id = counter();
    const auto value_id = call<p::value_record>(p::put_value{std::int64_t{1}}).id;
    const auto other_id =
        expect_result<p::object_snapshot>(
            other.execute(p::create_object{"Counter", {{"initial", std::int64_t{7}}}}))
            .id;
    EXPECT_NE(object_id, other_id);
    EXPECT_EQ(expect_result<p::remote_error>(other.execute(p::get_object{object_id})).code,
              p::error_code::not_found);
    EXPECT_EQ(expect_result<p::remote_error>(other.execute(p::get_value{value_id})).code,
              p::error_code::not_found);
    const auto before = store().accounted_bytes();
    call<p::released>(p::release_object{object_id});
    EXPECT_LT(store().accounted_bytes(), before);
    error(p::get_object{object_id}, p::error_code::not_found);
    error(p::release_object{object_id}, p::error_code::not_found);
    EXPECT_GT(counter(), other_id);
    store().close();
    store().close();
    EXPECT_EQ(store().accounted_bytes(), 0);
    error(p::get_value{value_id}, p::error_code::access_denied);
    EXPECT_EQ(expect_result<p::object_snapshot>(other.execute(p::get_object{other_id})).id,
              other_id);
}

TEST_F(domain_test, CountAndSharedByteQuotasRejectBeforeMutation)
{
    auto limited = new_session({1, 1, record_charge + sizeof(std::int64_t)});
    expect_result<p::value_record>(limited.execute(p::put_value{std::int64_t{1}}));
    EXPECT_EQ(expect_result<p::remote_error>(limited.execute(p::put_value{std::int64_t{2}})).code,
              p::error_code::quota_exceeded);
    EXPECT_EQ(expect_result<p::remote_error>(
                  limited.execute(p::create_object{"Counter", {{"initial", std::int64_t{0}}}}))
                  .code,
              p::error_code::quota_exceeded);
    EXPECT_EQ(limited.accounted_bytes(), record_charge + sizeof(std::int64_t));
    auto objects = new_session({0, 1, max_session_bytes});
    const auto id =
        expect_result<p::object_snapshot>(
            objects.execute(p::create_object{"Counter", {{"initial", std::int64_t{0}}}}))
            .id;
    EXPECT_EQ(expect_result<p::remote_error>(
                  objects.execute(p::create_object{"Counter", {{"initial", std::int64_t{0}}}}))
                  .code,
              p::error_code::quota_exceeded);
    expect_result<p::released>(objects.execute(p::release_object{id}));
    EXPECT_NE(expect_result<p::object_snapshot>(
                  objects.execute(p::create_object{"Counter", {{"initial", std::int64_t{0}}}}))
                  .id,
              id);
}

TEST_F(domain_test, RejectsInvalidDirectCallsAndBoundsLargePages)
{
    error(p::put_value{std::numeric_limits<double>::infinity()}, p::error_code::invalid_request);
    error(p::put_value{std::string("\xff")}, p::error_code::invalid_request);
    error(p::put_value{std::string(p::max_string_bytes + 1, 'x')}, p::error_code::invalid_request);
    error(p::get_value{0}, p::error_code::invalid_request);
    error(p::list_values{0, 0}, p::error_code::invalid_request);
    error(p::list_classes{"", 65}, p::error_code::invalid_request);
    error(p::describe_class{"NotRegistered"}, p::error_code::not_found);
    error(p::create_object{"NotRegistered", {}}, p::error_code::not_found);
    for (int index = 0; index < 20; ++index) {
        call<p::value_record>(p::put_value{std::string(p::max_string_bytes, 'x')});
    }

    const auto page = call<p::values_page>(p::list_values{0, 64});
    EXPECT_LT(page.values.size(), 20);
    EXPECT_NE(page.next, 0);
}

TEST(Registry, DuplicateInvalidMetadataAndFactoryFailure)
{
    class_registry registry;
    EXPECT_FALSE(registry.add(counter_definition()));
    const auto duplicate = registry.add(counter_definition());
    ASSERT_TRUE(duplicate);
    if (duplicate) {
        EXPECT_EQ(duplicate->code, p::error_code::invalid_request);
    }

    auto invalid = counter_definition();
    invalid.description.name = "bad name";
    EXPECT_TRUE(registry.add(std::move(invalid)));
    auto broken = counter_definition();
    broken.description.name = "Broken";
    broken.create = [](std::span<const p::named_value>) -> result<std::unique_ptr<object>> {
        return p::remote_error{p::error_code::internal_failure, "Factory unavailable"};
    };
    EXPECT_FALSE(registry.add(std::move(broken)));
    identifier_source ids;
    session_store store(registry, ids);
    EXPECT_EQ(expect_result<p::remote_error>(
                  store.execute(p::create_object{"Broken", {{"initial", std::int64_t{0}}}}))
                  .code,
              p::error_code::internal_failure);
    EXPECT_EQ(store.accounted_bytes(), 0);
}

TEST(Identifiers, ExhaustionNeverWrapsOrReusesIds)
{
    identifier_source ids(std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(ids.next(), std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(ids.next());
    EXPECT_FALSE(ids.next());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::domain
