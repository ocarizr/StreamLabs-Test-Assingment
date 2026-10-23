#include "pipeforge/diagnostics.hpp"
#include "pipeforge/services.hpp"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "domain_test_support.hpp"
#include "test_files.hpp"

namespace pipeforge::server {
namespace {
namespace p = protocol;
using test_support::expect_result;
using test_support::registered_classes;
using service_diagnostics_test = test_support::diagnostics_test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
p::response round_trip(dispatcher& service, const p::request& request)
{
    const auto encoded = p::encode_request(request);
    if (!std::holds_alternative<p::bytes>(encoded)) {
        ADD_FAILURE() << "Request must be encodable";
        return {};
    }

    const auto decoded = p::decode_request(std::get<p::bytes>(encoded));
    if (!std::holds_alternative<p::request>(decoded)) {
        ADD_FAILURE() << "Request must decode";
        return {};
    }

    const auto result = service.dispatch(std::get<p::request>(decoded));
    if (!std::holds_alternative<p::response>(result)) {
        ADD_FAILURE() << "Valid request must receive a correlated response";
        return {};
    }

    auto response = std::get<p::response>(result);
    EXPECT_EQ(response.id, request.id);
    const auto wire = p::encode_response(response);
    if (!std::holds_alternative<p::bytes>(wire)) {
        ADD_FAILURE() << "Domain outcome must fit the wire contract";
        return {};
    }

    EXPECT_LE(std::get<p::bytes>(wire).size(), p::max_payload_bytes);
    const auto received = p::decode_response(std::get<p::bytes>(wire));
    if (!std::holds_alternative<p::response>(received)) {
        ADD_FAILURE() << "Domain response must decode";
        return {};
    }

    EXPECT_EQ(std::get<p::response>(received), response);
    return response;
}

TEST(ServiceIntegration, EveryOperationSurvivesTheCodecServiceBoundary)
{
    auto registry = registered_classes();
    domain::identifier_source ids;
    domain::session_store store(registry, ids);
    dispatcher service(store);
    EXPECT_EQ(expect_result<p::classes_page>(round_trip(service, {1, p::list_classes{}}).body)
                  .names.size(),
              2);
    EXPECT_EQ(expect_result<p::class_description>(
                  round_trip(service, {2, p::describe_class{"Counter"}}).body)
                  .name,
              "Counter");
    const auto saved = expect_result<p::value_record>(
        round_trip(service, {3, p::put_value{std::int64_t{-7}}}).body);
    EXPECT_EQ(expect_result<p::value_record>(round_trip(service, {4, p::get_value{saved.id}}).body),
              saved);
    EXPECT_EQ(expect_result<p::values_page>(round_trip(service, {5, p::list_values{}}).body).values,
              std::vector<p::value_record>{saved});
    const auto snapshot = expect_result<p::object_snapshot>(
        round_trip(service, {6, p::create_object{"Counter", {{"initial", std::int64_t{7}}}}}).body);
    EXPECT_EQ(expect_result<p::object_snapshot>(
                  round_trip(service, {7, p::get_object{snapshot.id}}).body),
              snapshot);
    EXPECT_EQ(expect_result<p::value>(
                  round_trip(service, {8, p::get_attribute{snapshot.id, "value"}}).body),
              p::value{std::int64_t{7}});
    EXPECT_EQ(
        expect_result<p::value>(
            round_trip(
                service,
                {9, p::invoke_method{snapshot.id, "increment", {{"delta", std::int64_t{-2}}}}})
                .body),
        p::value{std::int64_t{5}});
    expect_result<p::released>(round_trip(service, {10, p::release_object{snapshot.id}}).body);
    EXPECT_EQ(
        expect_result<p::remote_error>(round_trip(service, {11, p::get_object{snapshot.id}}).body)
            .code,
        p::error_code::not_found);
    const auto zero =
        expect_result<p::value_record>(round_trip(service, {12, p::put_value{-0.0}}).body);
    EXPECT_TRUE(std::signbit(std::get<double>(zero.data)));
}

TEST(ServiceIntegration, ByteBoundedPagesDeliverEveryValueWithoutOversizedResponses)
{
    auto registry = registered_classes();
    domain::identifier_source ids;
    domain::session_store store(registry, ids);
    dispatcher service(store);
    constexpr std::size_t count = 20;
    for (std::size_t index = 0; index < count; ++index) {
        expect_result<p::value_record>(
            round_trip(service, {index + 1, p::put_value{std::string(p::max_string_bytes, 'x')}})
                .body);
    }

    std::size_t delivered = 0;
    std::uint64_t cursor = 0;
    while (delivered < count) {
        const auto page = expect_result<p::values_page>(
            round_trip(service, {count + delivered + 1, p::list_values{cursor, 64}}).body);
        ASSERT_FALSE(page.values.empty());
        EXPECT_GT(page.values.front().id, cursor);
        delivered += page.values.size();
        ASSERT_LE(delivered, count);
        if (page.next != 0) {
            EXPECT_GT(page.next, cursor);
        }

        cursor = page.next;
        if (cursor == 0) {
            break;
        }
    }

    EXPECT_EQ(cursor, 0);

    EXPECT_EQ(delivered, count);
}

TEST_F(service_diagnostics_test, LogsOnlyTrustedCategoriesAndProfilesDispatch)
{
    auto registry = registered_classes();
    domain::identifier_source ids;
    domain::session_store store(registry, ids);
    diagnostic_session diagnostics(
        "service-test",
        {.log_file = file("service.log"), .trace_file = file("service.trace.json")});
    dispatcher service(store, &diagnostics);
    round_trip(service, {41, p::put_value{std::string("peer-secret-do-not-log")}});
    round_trip(service, {42, p::describe_class{"SecretClassName"}});
    ASSERT_TRUE(diagnostics.finish());
    const auto log = test_support::read_file(file("service.log"));
    EXPECT_NE(log.find("request=41 operation=put_value code=0"), std::string::npos);
    EXPECT_NE(log.find("request=42 operation=describe_class code=2"), std::string::npos);
    EXPECT_EQ(log.find("peer-secret"), std::string::npos);
    EXPECT_EQ(log.find("SecretClassName"), std::string::npos);
    if (!profiling_enabled) {
        EXPECT_FALSE(std::filesystem::exists(file("service.trace.json")));
        return;
    }

    const auto trace = nlohmann::json::parse(test_support::read_file(file("service.trace.json")));
    const auto& events = trace.at("traceEvents");
    const auto count = std::count_if(events.begin(), events.end(), [](const auto& event) {
        return event.at("ph") == "X";
    });
    EXPECT_EQ(count, 2);
    EXPECT_EQ(trace.dump().find("peer-secret"), std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::server
