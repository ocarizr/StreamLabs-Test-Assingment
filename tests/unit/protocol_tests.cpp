#include "pipeforge/protocol.hpp"
#include "pipeforge/v1/protocol.pb.h"

#include <cmath>
#include <limits>
#include <string_view>

#include <gtest/gtest.h>

namespace pipeforge::protocol {
namespace {
// Literal IDs, tags and lengths here are observable boundary/encoding fixtures.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
bytes wire_bytes(const google::protobuf::Message& message)
{
    bytes output(message.ByteSizeLong());
    EXPECT_TRUE(message.SerializeToArray(output.data(), static_cast<int>(output.size())));
    return output;
}

void round_trip(operation body)
{
    const request original{42, std::move(body)};
    const auto encoded = encode_request(original);
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    const auto decoded = decode_request(std::get<bytes>(encoded));
    ASSERT_TRUE(std::holds_alternative<request>(decoded));
    EXPECT_EQ(std::get<request>(decoded), original);
}

void round_trip(outcome body)
{
    const response original{42, std::move(body)};
    const auto encoded = encode_response(original);
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    const auto decoded = decode_response(std::get<bytes>(encoded));
    ASSERT_TRUE(std::holds_alternative<response>(decoded));
    EXPECT_EQ(std::get<response>(decoded), original);
}

TEST(Protocol, RoundTripsEveryRequestOperation)
{
    round_trip(operation{list_classes{"", 32}});
    round_trip(operation{describe_class{"Counter"}});
    round_trip(operation{put_value{std::string("ol\xc3\xa1 \xe4\xb8\x96\xe7\x95\x8c")}});
    round_trip(operation{get_value{7}});
    round_trip(operation{list_values{7, 8}});
    round_trip(operation{create_object{"Counter", {{"initial", std::int64_t{0}}}}});
    round_trip(operation{get_object{9}});
    round_trip(operation{release_object{9}});
    round_trip(operation{get_attribute{9, "value"}});
    round_trip(operation{invoke_method{9, "increment", {{"delta", std::int64_t{-1}}}}});
}

TEST(Protocol, RoundTripsEveryResponseOutcomeAndMetadata)
{
    round_trip(outcome{classes_page{{"Counter", "Other"}, "Other"}});
    round_trip(outcome{
        class_description{"Counter",
                          {{"value", value_kind::integer, true}},
                          {{"increment", {{"delta", value_kind::integer}}, value_kind::integer}},
                          {{"initial", value_kind::integer}}}});
    round_trip(outcome{value_record{1, std::int64_t{-7}}});
    round_trip(outcome{values_page{{{1, std::string("")}, {2, 3.5}}, 2}});
    round_trip(outcome{object_snapshot{1, "Counter", {{"value", std::int64_t{7}}}}});
    round_trip(outcome{released{}});
    round_trip(outcome{value{3.25}});
    round_trip(outcome{remote_error{error_code::not_found, "Unknown object"}});
}

TEST(Protocol, PreservesIntegerBoundariesEmptyStringsAndSignedZero)
{
    for (const auto integer : {std::numeric_limits<std::int64_t>::min(),
                               std::int64_t{0},
                               std::numeric_limits<std::int64_t>::max()}) {
        round_trip(operation{put_value{integer}});
    }

    round_trip(operation{put_value{std::string{}}});
    round_trip(operation{put_value{std::string("a\0b", 3)}});
    const auto encoded = encode_request({1, put_value{-0.0}});
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    const auto decoded = decode_request(std::get<bytes>(encoded));
    ASSERT_TRUE(std::holds_alternative<request>(decoded));
    EXPECT_TRUE(
        std::signbit(std::get<double>(std::get<put_value>(std::get<request>(decoded).body).data)));
}

TEST(Protocol, ChecksPresenceVersionAndCorrelationBeforeDispatch)
{
    wire::v1::Request message;
    message.set_request_id(17);
    message.mutable_get_object()->set_id(1);
    auto decoded = decode_request(wire_bytes(message));
    ASSERT_TRUE(std::holds_alternative<failure>(decoded));
    EXPECT_EQ(std::get<failure>(decoded), (failure{failure_code::invalid_fields, false, 17}));
    message.set_version(2);
    decoded = decode_request(wire_bytes(message));
    EXPECT_EQ(std::get<failure>(decoded).code, failure_code::unsupported_version);
    message.set_version(version);
    message.clear_operation();
    EXPECT_EQ(std::get<failure>(decode_request(wire_bytes(message))).code,
              failure_code::invalid_fields);
    message.set_request_id(0);
    EXPECT_TRUE(std::get<failure>(decode_request(wire_bytes(message))).close_session);
}

TEST(Protocol, RejectsUnsetValuesInvalidNamesAndDuplicateArguments)
{
    wire::v1::Request message;
    message.set_version(version);
    message.set_request_id(1);
    message.mutable_put_value()->mutable_value();
    EXPECT_TRUE(std::holds_alternative<failure>(decode_request(wire_bytes(message))));
    EXPECT_TRUE(std::holds_alternative<failure>(encode_request({1, describe_class{"../Counter"}})));
    EXPECT_TRUE(std::holds_alternative<failure>(encode_request(
        {1,
         invoke_method{
             1, "increment", {{"delta", std::int64_t{1}}, {"delta", std::int64_t{2}}}}})));
}

TEST(Protocol, RejectsInvalidNumbersEnumsAndIdentifiers)
{
    for (const auto real : {std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_TRUE(std::holds_alternative<failure>(encode_request({1, put_value{real}})));
    }

    EXPECT_TRUE(std::holds_alternative<failure>(encode_request({1, get_object{0}})));
    EXPECT_TRUE(std::holds_alternative<failure>(encode_request({1, list_values{0, 0}})));
    wire::v1::Response malformed;
    malformed.set_version(version);
    malformed.set_request_id(1);
    auto* description = malformed.mutable_description();
    description->set_name("Counter");
    auto* attribute = description->add_attributes();
    attribute->set_name("value");
    attribute->GetReflection()->SetEnumValue(
        attribute, attribute->GetDescriptor()->FindFieldByName("kind"), 255);
    EXPECT_TRUE(std::holds_alternative<failure>(decode_response(wire_bytes(malformed))));
}

TEST(Protocol, EnforcesStringCollectionAndFrameLimits)
{
    round_trip(operation{put_value{std::string(max_string_bytes, 'x')}});
    EXPECT_TRUE(std::holds_alternative<failure>(
        encode_request({1, put_value{std::string(max_string_bytes + 1, 'x')}})));
    create_object operation{"Counter", {}};
    for (std::size_t index = 0; index <= max_entries; ++index) {
        operation.arguments.push_back({"a" + std::to_string(index), std::int64_t{0}});
    }

    EXPECT_TRUE(std::holds_alternative<failure>(encode_request({1, operation})));
    EXPECT_EQ(std::get<failure>(encode_request({1, put_value{std::string("\xc0\xaf")}})).code,
              failure_code::invalid_fields);
    const bytes too_large(max_payload_bytes + 1);
    EXPECT_EQ(std::get<failure>(decode_request(too_large)).code, failure_code::limit_exceeded);
}

TEST(Protocol, RejectsMalformedTruncatedAndTrailingBytes)
{
    EXPECT_TRUE(std::holds_alternative<failure>(decode_request({})));
    auto encoded = encode_request({1, put_value{std::string("payload")}});
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    auto payload = std::get<bytes>(encoded);
    payload.pop_back();
    EXPECT_TRUE(std::get<failure>(decode_request(payload)).close_session);
    payload = std::get<bytes>(encoded);
    payload.push_back(std::byte{0});
    EXPECT_TRUE(std::get<failure>(decode_request(payload)).close_session);
    payload.assign(12, std::byte{0xff});
    EXPECT_TRUE(std::holds_alternative<failure>(decode_response(payload)));
}

TEST(Protocol, AcceptsBoundedUnknownFieldsWithoutReflectingThem)
{
    wire::v1::Request message;
    message.set_version(version);
    message.set_request_id(1);
    message.mutable_get_object()->set_id(7);
    message.GetReflection()->MutableUnknownFields(&message)->AddVarint(100, 123);
    const auto payload = wire_bytes(message);
    const auto decoded = decode_request(payload);
    ASSERT_TRUE(std::holds_alternative<request>(decoded));
    const auto encoded = encode_request(std::get<request>(decoded));
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    EXPECT_LT(std::get<bytes>(encoded).size(), payload.size());
    for (int index = 0; index < 129; ++index) {
        message.GetReflection()->MutableUnknownFields(&message)->AddVarint(101, 0);
    }

    EXPECT_EQ(std::get<failure>(decode_request(wire_bytes(message))).code,
              failure_code::limit_exceeded);
}

TEST(Protocol, UsesProtobufLastScalarAndOneofSemantics)
{
    wire::v1::Request first;
    first.set_version(version);
    first.set_request_id(1);
    first.mutable_get_object()->set_id(7);
    wire::v1::Request second;
    second.set_request_id(2);
    second.mutable_put_value()->mutable_value()->set_integer(-1);
    auto payload = wire_bytes(first);
    const auto suffix = wire_bytes(second);
    payload.insert(payload.end(), suffix.begin(), suffix.end());
    const auto decoded = decode_request(payload);
    ASSERT_TRUE(std::holds_alternative<request>(decoded));
    EXPECT_EQ(std::get<request>(decoded), (request{2, put_value{std::int64_t{-1}}}));
}

TEST(Protocol, BoundsUnknownGroupRecursionAndDecodedCardinality)
{
    wire::v1::Request message;
    message.set_version(version);
    message.set_request_id(1);
    message.mutable_get_object()->set_id(7);
    auto* group = message.GetReflection()->MutableUnknownFields(&message);
    for (int depth = 0; depth <= max_recursion_depth; ++depth) {
        group = group->AddGroup(100);
    }

    const auto nested = decode_request(wire_bytes(message));
    ASSERT_TRUE(std::holds_alternative<failure>(nested));
    EXPECT_TRUE(std::get<failure>(nested).close_session);
    message.Clear();
    message.set_version(version);
    message.set_request_id(1);
    auto* object = message.mutable_create_object();
    object->set_class_name("Counter");
    for (int index = 0; index < 10000; ++index) {
        object->add_arguments()->set_name("x");
    }

    const auto crowded = decode_request(wire_bytes(message));
    ASSERT_TRUE(std::holds_alternative<failure>(crowded));
    EXPECT_EQ(std::get<failure>(crowded).code, failure_code::limit_exceeded);

    wire::v1::Response metadata;
    metadata.set_version(version);
    metadata.set_request_id(1);
    auto* description = metadata.mutable_description();
    description->set_name("Counter");
    for (std::size_t method_index = 0; method_index < max_entries; ++method_index) {
        auto* method = description->add_methods();
        method->set_name("m" + std::to_string(method_index));
        method->set_result_kind(wire::v1::INTEGER);
        for (std::size_t parameter_index = 0; parameter_index < max_entries; ++parameter_index) {
            auto* parameter = method->add_parameters();
            parameter->set_name("p" + std::to_string(parameter_index));
            parameter->set_kind(wire::v1::INTEGER);
        }
    }

    const auto expanded = decode_response(wire_bytes(metadata));
    ASSERT_TRUE(std::holds_alternative<failure>(expanded));
    EXPECT_EQ(std::get<failure>(expanded).code, failure_code::limit_exceeded);
}

TEST(Protocol, MatchesGoldenV1BytesAndMergesRepeatedEmbeddedMessages)
{
    constexpr std::string_view golden{"\x08\x01\x10\x01\x82\x01\x02\x08\x01"};
    const auto golden_span = std::as_bytes(std::span{golden.data(), golden.size()});
    const auto encoded = encode_request({1, get_object{1}});
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    EXPECT_EQ(std::get<bytes>(encoded), (bytes{golden_span.begin(), golden_span.end()}));
    wire::v1::Request first;
    first.set_version(version);
    first.set_request_id(1);
    first.mutable_invoke_method()->set_name("increment");
    wire::v1::Request second;
    second.mutable_invoke_method()->set_id(7);
    auto payload = wire_bytes(first);
    const auto suffix = wire_bytes(second);
    payload.insert(payload.end(), suffix.begin(), suffix.end());
    const auto decoded = decode_request(payload);
    ASSERT_TRUE(std::holds_alternative<request>(decoded));
    EXPECT_EQ(std::get<request>(decoded), (request{1, invoke_method{7, "increment", {}}}));
}

TEST(Protocol, BoundsTotalPayloadAndRejectsInvalidResponseSemantics)
{
    values_page page;
    for (std::uint64_t id = 1; id <= 16; ++id) {
        page.values.push_back({id, std::string(max_string_bytes, 'x')});
    }

    EXPECT_EQ(std::get<failure>(encode_response({1, page})).code, failure_code::limit_exceeded);
    EXPECT_TRUE(
        std::holds_alternative<failure>(encode_response({1, classes_page{{"B", "A"}, ""}})));
    EXPECT_TRUE(std::holds_alternative<failure>(encode_response({1, values_page{{{1, 0.0}}, 2}})));
    wire::v1::Response message;
    message.set_version(version);
    message.set_request_id(1);
    auto* error = message.mutable_error();
    error->GetReflection()->SetEnumValue(
        error, error->GetDescriptor()->FindFieldByName("code"), 999);
    EXPECT_TRUE(std::holds_alternative<failure>(decode_response(wire_bytes(message))));
    message.clear_outcome();
    EXPECT_TRUE(std::holds_alternative<failure>(decode_response(wire_bytes(message))));
}

TEST(Framing, SupportsEverySplitAndConcatenatedFrames)
{
    const bytes payload{std::byte{1}, std::byte{2}, std::byte{3}};
    const auto encoded = frame(payload);
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    const auto& packet = std::get<bytes>(encoded);
    EXPECT_EQ(packet.front(), std::byte{3});
    for (std::size_t split = 0; split < packet.size(); ++split) {
        frame_decoder decoder;
        const auto first = decoder.consume(std::span{packet}.first(split));
        ASSERT_TRUE(std::holds_alternative<frame_progress>(first));
        EXPECT_FALSE(std::get<frame_progress>(first).payload.has_value());
        const auto last = decoder.consume(std::span{packet}.subspan(split));
        ASSERT_TRUE(std::holds_alternative<frame_progress>(last));
        EXPECT_EQ(std::get<frame_progress>(last).payload, payload);
        EXPECT_FALSE(decoder.finish().has_value());
    }

    auto concatenated = packet;
    concatenated.insert(concatenated.end(), packet.begin(), packet.end());
    frame_decoder decoder;
    const auto first = decoder.consume(concatenated);
    ASSERT_TRUE(std::holds_alternative<frame_progress>(first));
    EXPECT_EQ(std::get<frame_progress>(first).consumed, packet.size());
    const auto last = decoder.consume(std::span{concatenated}.subspan(packet.size()));
    EXPECT_EQ(std::get<frame_progress>(last).payload, payload);
}

TEST(Framing, RejectsOversizedHeadersBeforePayloadAndPoisonedReuse)
{
    frame_decoder decoder;
    const bytes header{std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    EXPECT_EQ(std::get<failure>(decoder.consume(header)).code, failure_code::limit_exceeded);
    EXPECT_TRUE(std::holds_alternative<failure>(decoder.consume({})));
    EXPECT_TRUE(decoder.finish().has_value());
    EXPECT_TRUE(std::holds_alternative<failure>(frame({})));
}

TEST(Framing, ReportsEOFOnlyForIncompleteFrames)
{
    frame_decoder decoder;
    EXPECT_FALSE(decoder.finish().has_value());
    const bytes prefix{std::byte{1}};
    static_cast<void>(decoder.consume(prefix));
    EXPECT_EQ(decoder.finish(),
              (std::optional{failure{failure_code::truncated, true, std::nullopt}}));
    frame_decoder payload_decoder;
    const bytes header{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
    static_cast<void>(payload_decoder.consume(header));
    EXPECT_EQ(payload_decoder.finish(),
              (std::optional{failure{failure_code::truncated, true, std::nullopt}}));
}

TEST(Framing, AcceptsExactLimitAndRejectsZeroAndLimitPlusOne)
{
    const bytes payload(max_payload_bytes, std::byte{1});
    const auto encoded = frame(payload);
    ASSERT_TRUE(std::holds_alternative<bytes>(encoded));
    const auto& packet = std::get<bytes>(encoded);
    EXPECT_EQ(packet.at(0), std::byte{0});
    EXPECT_EQ(packet.at(1), std::byte{0});
    EXPECT_EQ(packet.at(2), std::byte{0x10});
    EXPECT_EQ(packet.at(3), std::byte{0});
    frame_decoder decoder;
    const auto decoded = decoder.consume(packet);
    ASSERT_TRUE(std::holds_alternative<frame_progress>(decoded));
    EXPECT_EQ(std::get<frame_progress>(decoded).payload, payload);
    frame_decoder empty_decoder;
    const bytes empty_header(frame_header_bytes);
    EXPECT_TRUE(std::holds_alternative<failure>(empty_decoder.consume(empty_header)));
    const bytes oversized(max_payload_bytes + 1);
    EXPECT_TRUE(std::holds_alternative<failure>(frame(oversized)));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
} // namespace
} // namespace pipeforge::protocol
