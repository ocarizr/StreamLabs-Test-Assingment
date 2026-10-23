#include "pipeforge/domain.hpp"

#include <limits>

#include "domain_test_support.hpp"

namespace pipeforge::domain {
namespace {
namespace p = protocol;
using test_support::expect_result;
using test_support::registered_classes;
// Boundary fixtures intentionally use literal IDs, sizes and metadata values.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
enum class behavior : std::uint8_t {
    success,
    fail_after_mutation,
    wrong_result,
    wrong_state,
    null_clone
};

class probe_object final : public object {
  public:
    probe_object(std::shared_ptr<int> live, behavior mode, std::int64_t value = 0)
        : live_(std::move(live)), mode_(mode), value_(value)
    {
        ++*live_;
    }

    ~probe_object() override
    {
        --*live_;
    }

    probe_object(const probe_object&) = delete;
    probe_object& operator=(const probe_object&) = delete;
    probe_object(probe_object&&) = delete;
    probe_object& operator=(probe_object&&) = delete;

    [[nodiscard]] std::unique_ptr<object> clone() const override
    {
        return mode_ == behavior::null_clone ? nullptr
                                             : std::make_unique<probe_object>(live_, mode_, value_);
    }

    [[nodiscard]] std::vector<p::named_value> state() const override
    {
        return {{mode_ == behavior::wrong_state && value_ != 0 ? "unexpected" : "value", value_}};
    }

    [[nodiscard]] result<p::value> invoke(std::string_view,
                                          std::span<const p::named_value>) override
    {
        ++value_;
        if (mode_ == behavior::fail_after_mutation) {
            return p::remote_error{p::error_code::invalid_request, "peer-secret-must-not-escape"};
        }

        if (mode_ == behavior::wrong_result) {
            return p::value{std::string("incorrect kind")};
        }

        return p::value{value_};
    }

  private:
    std::shared_ptr<int> live_;
    behavior mode_;
    std::int64_t value_;
};

class_definition probe_definition(const std::shared_ptr<int>& live, behavior mode)
{
    return {{"Probe",
             {{"value", p::value_kind::integer, true}},
             {{"mutate", {}, p::value_kind::integer}},
             {}},
            [live, mode](std::span<const p::named_value>) -> result<std::unique_ptr<object>> {
                return std::unique_ptr<object>{std::make_unique<probe_object>(live, mode)};
            }};
}

TEST(DomainFailures, FailedCallsDiscardCandidateAndNeverLeakFactoryText)
{
    for (auto mode : {behavior::fail_after_mutation,
                      behavior::wrong_result,
                      behavior::wrong_state,
                      behavior::null_clone}) {
        auto live = std::make_shared<int>(0);
        class_registry registry;
        ASSERT_FALSE(registry.add(probe_definition(live, mode)));
        identifier_source ids;
        session_store store(registry, ids);
        const auto id =
            expect_result<p::object_snapshot>(store.execute(p::create_object{"Probe", {}})).id;
        const auto before = store.accounted_bytes();
        const auto error =
            expect_result<p::remote_error>(store.execute(p::invoke_method{id, "mutate", {}}));
        EXPECT_EQ(error.code,
                  mode == behavior::fail_after_mutation ? p::error_code::invalid_request
                                                        : p::error_code::internal_failure);
        EXPECT_EQ(error.message.find("peer-secret"), std::string::npos);
        EXPECT_EQ(expect_result<p::value>(store.execute(p::get_attribute{id, "value"})),
                  p::value{std::int64_t{0}});
        EXPECT_EQ(store.accounted_bytes(), before);
        EXPECT_EQ(*live, 1);
        store.close();
        EXPECT_EQ(*live, 0);
    }
}

TEST(DomainFailures, ReleaseCloseAndDestructorDestroyAllOwnedInstances)
{
    auto live = std::make_shared<int>(0);
    class_registry registry;
    ASSERT_FALSE(registry.add(probe_definition(live, behavior::success)));
    identifier_source ids;
    {
        session_store store(registry, ids);
        const auto first =
            expect_result<p::object_snapshot>(store.execute(p::create_object{"Probe", {}})).id;
        expect_result<p::object_snapshot>(store.execute(p::create_object{"Probe", {}}));
        EXPECT_EQ(*live, 2);
        expect_result<p::value>(store.execute(p::invoke_method{first, "mutate", {}}));
        EXPECT_EQ(*live, 2);
        expect_result<p::released>(store.execute(p::release_object{first}));
        EXPECT_EQ(*live, 1);
    }
    EXPECT_EQ(*live, 0);
}

TEST(DomainFailures, SharedQuotaAndTextLimitRollBackGrowth)
{
    auto registry = registered_classes();
    identifier_source ids;
    const auto initial_charge = record_charge + std::string_view("TextBuffer").size() +
                                record_charge + std::string_view("text").size() + 1;
    session_store store(registry, ids, {1, 1, initial_charge + 2});
    const auto id =
        expect_result<p::object_snapshot>(
            store.execute(p::create_object{"TextBuffer", {{"initial", std::string("x")}}}))
            .id;
    EXPECT_EQ(store.accounted_bytes(), initial_charge);
    EXPECT_EQ(expect_result<p::remote_error>(
                  store.execute(p::invoke_method{id, "append", {{"suffix", std::string("abc")}}}))
                  .code,
              p::error_code::quota_exceeded);
    EXPECT_EQ(store.accounted_bytes(), initial_charge);
    EXPECT_EQ(expect_result<p::value>(store.execute(p::get_attribute{id, "text"})),
              p::value{std::string("x")});
    EXPECT_EQ(expect_result<p::value>(
                  store.execute(p::invoke_method{id, "append", {{"suffix", std::string("ab")}}})),
              p::value{std::string("xab")});
    EXPECT_EQ(store.accounted_bytes(), initial_charge + 2);
    session_store full(registry, ids);
    const auto full_id =
        expect_result<p::object_snapshot>(
            full.execute(p::create_object{"TextBuffer",
                                          {{"initial", std::string(p::max_string_bytes, 'x')}}}))
            .id;
    EXPECT_EQ(expect_result<p::remote_error>(
                  full.execute(p::invoke_method{full_id, "append", {{"suffix", std::string("a")}}}))
                  .code,
              p::error_code::quota_exceeded);
    EXPECT_EQ(std::get<std::string>(
                  expect_result<p::value>(full.execute(p::get_attribute{full_id, "text"})))
                  .size(),
              p::max_string_bytes);
}

TEST(DomainFailures, InvalidFactoryStateAndNullFactoryResultsAreNotStored)
{
    class_registry registry;
    auto missing = counter_definition();
    missing.description.name = "Missing";
    missing.create = [](std::span<const p::named_value>) -> result<std::unique_ptr<object>> {
        return std::unique_ptr<object>{};
    };
    ASSERT_FALSE(registry.add(std::move(missing)));
    auto invalid = counter_definition();
    invalid.description.name = "Invalid";
    invalid.description.attributes.front().name = "unimplemented";
    ASSERT_FALSE(registry.add(std::move(invalid)));
    identifier_source ids;
    session_store store(registry, ids);
    for (const auto* name : {"Missing", "Invalid"}) {
        EXPECT_EQ(expect_result<p::remote_error>(
                      store.execute(p::create_object{name, {{"initial", std::int64_t{1}}}}))
                      .code,
                  p::error_code::internal_failure);
        EXPECT_EQ(store.accounted_bytes(), 0);
    }

    EXPECT_EQ(ids.next(), 1);
}

TEST(DomainFailures, IdExhaustionDoesNotMutateRepositories)
{
    auto registry = registered_classes();
    identifier_source ids(std::numeric_limits<std::uint64_t>::max());
    session_store store(registry, ids);
    expect_result<p::value_record>(store.execute(p::put_value{std::int64_t{1}}));
    const auto before = store.accounted_bytes();
    EXPECT_EQ(expect_result<p::remote_error>(store.execute(p::put_value{std::int64_t{2}})).code,
              p::error_code::quota_exceeded);
    EXPECT_EQ(expect_result<p::remote_error>(
                  store.execute(p::create_object{"Counter", {{"initial", std::int64_t{1}}}}))
                  .code,
              p::error_code::quota_exceeded);
    EXPECT_EQ(store.accounted_bytes(), before);
}

TEST(Registry, RejectsInvalidAndUnboundedDefinitions)
{
    for (int scenario = 0; scenario < 5; ++scenario) {
        class_registry registry;
        auto definition = counter_definition();
        switch (scenario) {
        case 0:
            definition.create = {};
            break;
        case 1:
            definition.description.attributes.push_back(definition.description.attributes.front());
            break;
        case 2:
            definition.description.methods.front().parameters.push_back(
                definition.description.methods.front().parameters.front());
            break;
        case 3:
            definition.max_state_bytes = 0;
            break;
        default:
            definition.max_state_bytes = max_object_state_bytes + 1;
            break;
        }

        EXPECT_TRUE(registry.add(std::move(definition)));
    }

    class_registry registry;
    for (std::size_t index = 0; index < p::max_entries; ++index) {
        auto definition = counter_definition();
        definition.description.name = "Counter" + std::to_string(index);
        EXPECT_FALSE(registry.add(std::move(definition)));
    }

    const auto overflow = registry.add(text_buffer_definition());
    ASSERT_TRUE(overflow);
    if (overflow) {
        EXPECT_EQ(overflow->code, p::error_code::quota_exceeded);
    }
}

TEST(Registry, ConstructorParametersMatchByNameNotPosition)
{
    auto live = std::make_shared<int>(0);
    auto definition = probe_definition(live, behavior::success);
    definition.description.constructor_parameters = {{"left", p::value_kind::integer},
                                                     {"right", p::value_kind::text}};
    class_registry registry;
    ASSERT_FALSE(registry.add(std::move(definition)));
    identifier_source ids;
    session_store store(registry, ids);
    expect_result<p::object_snapshot>(store.execute(
        p::create_object{"Probe", {{"right", std::string("r")}, {"left", std::int64_t{1}}}}));
    EXPECT_EQ(*live, 1);
    EXPECT_EQ(expect_result<p::remote_error>(
                  store.execute(p::create_object{
                      "Probe", {{"left", std::int64_t{1}}, {"right", std::int64_t{1}}}}))
                  .code,
              p::error_code::invalid_request);
    EXPECT_EQ(*live, 1);
}

TEST(DomainFailures, ConfigurationCannotEnlargeHardStorageCeilings)
{
    auto registry = registered_classes();
    identifier_source ids;
    constexpr auto unlimited = std::numeric_limits<std::size_t>::max();
    session_store store(registry, ids, {unlimited, unlimited, unlimited});
    for (std::size_t index = 0; index < max_stored_values; ++index) {
        expect_result<p::value_record>(store.execute(p::put_value{std::int64_t{0}}));
    }

    EXPECT_EQ(expect_result<p::remote_error>(store.execute(p::put_value{std::int64_t{0}})).code,
              p::error_code::quota_exceeded);
    for (std::size_t index = 0; index < max_stored_objects; ++index) {
        expect_result<p::object_snapshot>(
            store.execute(p::create_object{"Counter", {{"initial", std::int64_t{0}}}}));
    }

    EXPECT_EQ(expect_result<p::remote_error>(
                  store.execute(p::create_object{"Counter", {{"initial", std::int64_t{0}}}}))
                  .code,
              p::error_code::quota_exceeded);
    store.close();
    EXPECT_EQ(store.accounted_bytes(), 0);
}

TEST(Registry, AggregateMetadataMustFitCodecNodeLimit)
{
    class_registry registry;
    auto definition = counter_definition();
    definition.description.methods.clear();
    for (std::size_t method = 0; method < p::max_entries; ++method) {
        p::method descriptor{"method" + std::to_string(method), {}, p::value_kind::integer};
        for (std::size_t parameter = 0; parameter < p::max_entries; ++parameter) {
            descriptor.parameters.push_back(
                {"argument" + std::to_string(parameter), p::value_kind::integer});
        }

        definition.description.methods.push_back(std::move(descriptor));
    }

    const auto error = registry.add(std::move(definition));
    ASSERT_TRUE(error);
    if (error) {
        EXPECT_EQ(error->code, p::error_code::invalid_request);
    }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::domain
