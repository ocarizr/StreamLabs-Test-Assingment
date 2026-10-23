#include "pipeforge/services.hpp"

#include <gtest/gtest.h>

namespace pipeforge::server {
namespace {
namespace p = protocol;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
TEST(Services, DispatchesCorrelatedProtocolRoundTrips)
{
    domain::class_registry registry;
    ASSERT_FALSE(registry.add(domain::counter_definition()));
    domain::identifier_source ids;
    domain::session_store store(registry, ids);
    dispatcher service(store);
    const auto result = service.dispatch({42, p::put_value{std::int64_t{-7}}});
    ASSERT_TRUE(std::holds_alternative<p::response>(result));
    const auto& response = std::get<p::response>(result);
    EXPECT_EQ(response.id, 42);
    ASSERT_TRUE(std::holds_alternative<p::value_record>(response.body));
    const auto encoded = p::encode_response(response);
    ASSERT_TRUE(std::holds_alternative<p::bytes>(encoded));
    const auto decoded = p::decode_response(std::get<p::bytes>(encoded));
    ASSERT_TRUE(std::holds_alternative<p::response>(decoded));
    EXPECT_EQ(std::get<p::response>(decoded), response);
}

TEST(Services, RejectsUntrustedIdsAndMapsRecoverableCodecFailures)
{
    domain::class_registry registry;
    domain::identifier_source ids;
    domain::session_store store(registry, ids);
    dispatcher service(store);
    EXPECT_TRUE(
        std::holds_alternative<p::failure>(service.dispatch({0, p::put_value{std::int64_t{1}}})));
    EXPECT_EQ(store.accounted_bytes(), 0);
    for (const auto code : {p::failure_code::invalid_fields,
                            p::failure_code::unsupported_version,
                            p::failure_code::limit_exceeded}) {
        const auto result = service.reject({code, false, 17});
        ASSERT_TRUE(std::holds_alternative<p::response>(result));
        EXPECT_EQ(std::get<p::response>(result).id, 17);
        ASSERT_TRUE(std::holds_alternative<p::remote_error>(std::get<p::response>(result).body));
        const auto expected =
            code == p::failure_code::limit_exceeded        ? p::error_code::quota_exceeded
            : code == p::failure_code::unsupported_version ? p::error_code::unsupported_version
                                                           : p::error_code::invalid_request;
        EXPECT_EQ(std::get<p::remote_error>(std::get<p::response>(result).body).code, expected);
    }

    EXPECT_TRUE(
        std::holds_alternative<p::failure>(service.reject({p::failure_code::malformed, true, 17})));
    EXPECT_TRUE(std::holds_alternative<p::failure>(
        service.reject({p::failure_code::invalid_fields, false, 0})));
    EXPECT_TRUE(std::holds_alternative<p::failure>(
        service.reject({p::failure_code::invalid_fields, false, std::nullopt})));
    EXPECT_TRUE(std::holds_alternative<p::failure>(
        service.reject({p::failure_code::malformed, false, 17})));
    EXPECT_TRUE(std::holds_alternative<p::failure>(
        service.reject({p::failure_code::truncated, false, 17})));
    const auto unsupported = service.dispatch({42, p::put_value{std::int64_t{1}}, 2});
    ASSERT_TRUE(std::holds_alternative<p::response>(unsupported));
    const auto* error = std::get_if<p::remote_error>(&std::get<p::response>(unsupported).body);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, p::error_code::unsupported_version);
    EXPECT_EQ(store.accounted_bytes(), 0);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::server
