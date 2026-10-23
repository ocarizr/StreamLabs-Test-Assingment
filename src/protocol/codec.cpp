#include "pipeforge/protocol.hpp"
#include "pipeforge/v1/protocol.pb.h"
#include "pipeforge/validation.hpp"

#include <algorithm>
#include <cmath>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <set>
#include <string_view>
#include <type_traits>
#include <utf8_validity.h>

namespace pipeforge::protocol {
namespace {
namespace pb = ::pipeforge::wire::v1;
constexpr int max_unknown_fields = 128;

bool bounded_unknowns(const google::protobuf::UnknownFieldSet& unknown, int& remaining, int depth)
{
    if (depth > max_recursion_depth || unknown.field_count() > remaining) {
        return false;
    }

    remaining -= unknown.field_count();
    for (int index = 0; index < unknown.field_count(); ++index) {
        const auto& field = unknown.field(index);
        if (field.type() == google::protobuf::UnknownField::TYPE_LENGTH_DELIMITED &&
            field.length_delimited().size() > max_string_bytes) {
            return false;
        }

        if (field.type() == google::protobuf::UnknownField::TYPE_GROUP &&
            !bounded_unknowns(field.group(), remaining, depth + 1)) {
            return false;
        }
    }

    return true;
}

// Common resource limits use reflection; Protobuf itself parses the bytes.
std::optional<failure_code>
validate_message(const google::protobuf::Message& message, std::size_t& nodes, int& unknowns)
{
    if (nodes == 0) {
        return failure_code::limit_exceeded;
    }

    --nodes;
    const auto* reflection = message.GetReflection();
    if (!bounded_unknowns(reflection->GetUnknownFields(message), unknowns, 0)) {
        return failure_code::limit_exceeded;
    }

    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields) {
        const int count = field->is_repeated() ? reflection->FieldSize(message, field) : 1;
        if (count > static_cast<int>(max_entries)) {
            return failure_code::limit_exceeded;
        }

        for (int index = 0; index < count; ++index) {
            if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
                const auto& child = field->is_repeated()
                                        ? reflection->GetRepeatedMessage(message, field, index)
                                        : reflection->GetMessage(message, field);
                if (const auto error = validate_message(child, nodes, unknowns)) {
                    return error;
                }
            }
            else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
                std::string scratch;
                const auto& text =
                    field->is_repeated()
                        ? reflection->GetRepeatedStringReference(message, field, index, &scratch)
                        : reflection->GetStringReference(message, field, &scratch);
                if (text.size() > max_string_bytes) {
                    return failure_code::limit_exceeded;
                }

                if (!utf8_range::IsStructurallyValid(text)) {
                    return failure_code::invalid_fields;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<value> read_value(const pb::Value& input)
{
    switch (input.data_case()) {
    case pb::Value::kText:
        return value{input.text()};
    case pb::Value::kInteger:
        return value{input.integer()};
    case pb::Value::kReal:
        if (std::isfinite(input.real())) {
            return value{input.real()};
        }

        return std::nullopt;
    default:
        return std::nullopt;
    }
}

void write_value(const value& input, pb::Value& output)
{
    std::visit(
        [&output](const auto& item) {
            using type = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<type, std::string>) {
                output.set_text(item);
            }
            else if constexpr (std::is_same_v<type, std::int64_t>) {
                output.set_integer(item);
            }
            else {
                output.set_real(item);
            }
        },
        input);
}

template <typename Repeated>
std::optional<std::vector<named_value>> read_named(const Repeated& entries)
{
    std::vector<named_value> output;
    output.reserve(static_cast<std::size_t>(entries.size()));
    for (const auto& entry : entries) {
        const auto data = read_value(entry.value());
        if (!data) {
            return std::nullopt;
        }

        output.push_back({entry.name(), *data});
    }

    if (!valid_names(output)) {
        return std::nullopt;
    }

    return output;
}

template <typename Repeated>
void write_named(const std::vector<named_value>& entries, Repeated& output)
{
    for (const auto& entry : entries) {
        auto* added = output.Add();
        added->set_name(entry.name);
        write_value(entry.data, *added->mutable_value());
    }
}

std::optional<operation> read_operation(const pb::Request& input)
{
    switch (input.operation_case()) {
    case pb::Request::kListClasses:
        if (valid_name(input.list_classes().after(), true) &&
            valid_page_size(input.list_classes().limit())) {
            return list_classes{input.list_classes().after(), input.list_classes().limit()};
        }

        break;
    case pb::Request::kDescribeClass:
        if (valid_name(input.describe_class().name())) {
            return describe_class{input.describe_class().name()};
        }

        break;
    case pb::Request::kPutValue:
        if (const auto data = read_value(input.put_value().value())) {
            return put_value{*data};
        }

        break;
    case pb::Request::kGetValue:
        if (input.get_value().id() != 0) {
            return get_value{input.get_value().id()};
        }

        break;
    case pb::Request::kListValues:
        if (valid_page_size(input.list_values().limit())) {
            return list_values{input.list_values().after(), input.list_values().limit()};
        }

        break;
    case pb::Request::kCreateObject:
        if (valid_name(input.create_object().class_name())) {
            if (auto arguments = read_named(input.create_object().arguments())) {
                return create_object{input.create_object().class_name(), std::move(*arguments)};
            }
        }

        break;
    case pb::Request::kGetObject:
        if (input.get_object().id() != 0) {
            return get_object{input.get_object().id()};
        }

        break;
    case pb::Request::kReleaseObject:
        if (input.release_object().id() != 0) {
            return release_object{input.release_object().id()};
        }

        break;
    case pb::Request::kGetAttribute:
        if (input.get_attribute().id() != 0 && valid_name(input.get_attribute().name())) {
            return get_attribute{input.get_attribute().id(), input.get_attribute().name()};
        }

        break;
    case pb::Request::kInvokeMethod:
        if (input.invoke_method().id() != 0 && valid_name(input.invoke_method().name())) {
            if (auto arguments = read_named(input.invoke_method().arguments())) {
                return invoke_method{input.invoke_method().id(),
                                     input.invoke_method().name(),
                                     std::move(*arguments)};
            }
        }

        break;
    default:
        break;
    }

    return std::nullopt;
}

void write_operation(const operation& input, pb::Request& output)
{
    std::visit(
        [&output](const auto& item) {
            using type = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<type, list_classes>) {
                output.mutable_list_classes()->set_after(item.after);
                output.mutable_list_classes()->set_limit(item.limit);
            }
            else if constexpr (std::is_same_v<type, describe_class>) {
                output.mutable_describe_class()->set_name(item.name);
            }
            else if constexpr (std::is_same_v<type, put_value>) {
                write_value(item.data, *output.mutable_put_value()->mutable_value());
            }
            else if constexpr (std::is_same_v<type, get_value>) {
                output.mutable_get_value()->set_id(item.id);
            }
            else if constexpr (std::is_same_v<type, list_values>) {
                output.mutable_list_values()->set_after(item.after);
                output.mutable_list_values()->set_limit(item.limit);
            }
            else if constexpr (std::is_same_v<type, create_object>) {
                output.mutable_create_object()->set_class_name(item.class_name);
                write_named(item.arguments, *output.mutable_create_object()->mutable_arguments());
            }
            else if constexpr (std::is_same_v<type, get_object>) {
                output.mutable_get_object()->set_id(item.id);
            }
            else if constexpr (std::is_same_v<type, release_object>) {
                output.mutable_release_object()->set_id(item.id);
            }
            else if constexpr (std::is_same_v<type, get_attribute>) {
                output.mutable_get_attribute()->set_id(item.id);
                output.mutable_get_attribute()->set_name(item.name);
            }
            else if constexpr (std::is_same_v<type, invoke_method>) {
                output.mutable_invoke_method()->set_id(item.id);
                output.mutable_invoke_method()->set_name(item.name);
                write_named(item.arguments, *output.mutable_invoke_method()->mutable_arguments());
            }
        },
        input);
}

std::optional<value_record> read_record(const pb::ValueRecord& input)
{
    if (const auto data = read_value(input.value()); data && input.id() != 0) {
        return value_record{input.id(), *data};
    }

    return std::nullopt;
}

void write_record(const value_record& input, pb::ValueRecord& output)
{
    output.set_id(input.id);
    write_value(input.data, *output.mutable_value());
}

template <typename Repeated>
std::optional<std::vector<parameter>> read_parameters(const Repeated& input)
{
    std::vector<parameter> output;
    output.reserve(static_cast<std::size_t>(input.size()));
    for (const auto& argument : input) {
        const auto kind = static_cast<value_kind>(argument.kind());
        if (!valid_kind(kind) || static_cast<int>(kind) != argument.kind()) {
            return std::nullopt;
        }

        output.push_back({argument.name(), kind});
    }

    if (!valid_names(output)) {
        return std::nullopt;
    }

    return output;
}

template <typename Repeated>
void write_parameters(const std::vector<parameter>& input, Repeated& output)
{
    for (const auto& argument : input) {
        auto* added = output.Add();
        added->set_name(argument.name);
        added->set_kind(static_cast<pb::ValueKind>(argument.kind));
    }
}

std::optional<class_description> read_description(const pb::ClassDescription& input)
{
    if (!valid_name(input.name())) {
        return std::nullopt;
    }

    auto constructor = read_parameters(input.constructor_parameters());
    if (!constructor) {
        return std::nullopt;
    }

    class_description output{input.name(), {}, {}, std::move(*constructor)};
    output.attributes.reserve(static_cast<std::size_t>(input.attributes_size()));
    for (const auto& field : input.attributes()) {
        const auto kind = static_cast<value_kind>(field.kind());
        if (!valid_kind(kind) || static_cast<int>(kind) != field.kind()) {
            return std::nullopt;
        }

        output.attributes.push_back({field.name(), kind, field.read_only()});
    }

    output.methods.reserve(static_cast<std::size_t>(input.methods_size()));
    for (const auto& function : input.methods()) {
        method descriptor{function.name(), {}, static_cast<value_kind>(function.result_kind())};
        if (!valid_kind(descriptor.result_kind) ||
            static_cast<int>(descriptor.result_kind) != function.result_kind()) {
            return std::nullopt;
        }

        auto parameters = read_parameters(function.parameters());
        if (!parameters) {
            return std::nullopt;
        }

        descriptor.parameters = std::move(*parameters);

        output.methods.push_back(std::move(descriptor));
    }

    if (!valid_names(output.attributes) || !valid_names(output.methods)) {
        return std::nullopt;
    }

    return output;
}

void write_description(const class_description& input, pb::ClassDescription& output)
{
    output.set_name(input.name);
    write_parameters(input.constructor_parameters, *output.mutable_constructor_parameters());
    for (const auto& field : input.attributes) {
        auto* added = output.add_attributes();
        added->set_name(field.name);
        added->set_kind(static_cast<pb::ValueKind>(field.kind));
        added->set_read_only(field.read_only);
    }

    for (const auto& function : input.methods) {
        auto* added = output.add_methods();
        added->set_name(function.name);
        added->set_result_kind(static_cast<pb::ValueKind>(function.result_kind));
        write_parameters(function.parameters, *added->mutable_parameters());
    }
}

std::optional<outcome> read_outcome(const pb::Response& input)
{
    switch (input.outcome_case()) {
    case pb::Response::kClasses: {
        classes_page output{{input.classes().names().begin(), input.classes().names().end()},
                            input.classes().next()};
        if (!std::ranges::all_of(output.names,
                                 [](const auto& name) {
                                     return valid_name(name);
                                 }) ||
            !std::ranges::is_sorted(output.names) ||
            std::ranges::adjacent_find(output.names) != output.names.end() ||
            (!output.next.empty() &&
             (output.names.empty() || output.next != output.names.back()))) {
            return std::nullopt;
        }

        return output;
    }
    case pb::Response::kDescription:
        if (auto description = read_description(input.description())) {
            return std::move(*description);
        }

        break;
    case pb::Response::kValueRecord:
        if (auto record = read_record(input.value_record())) {
            return std::move(*record);
        }

        break;
    case pb::Response::kValues: {
        values_page output{{}, input.values().next()};
        output.values.reserve(static_cast<std::size_t>(input.values().values_size()));
        std::uint64_t previous = 0;
        for (const auto& record : input.values().values()) {
            auto data = read_record(record);
            if (!data || data->id <= previous) {
                return std::nullopt;
            }

            previous = data->id;
            output.values.push_back(std::move(*data));
        }

        if (output.next != 0 && (output.values.empty() || output.next != previous)) {
            return std::nullopt;
        }

        return output;
    }
    case pb::Response::kObject:
        if (input.object().id() != 0 && valid_name(input.object().class_name())) {
            if (auto state = read_named(input.object().state())) {
                return object_snapshot{
                    input.object().id(), input.object().class_name(), std::move(*state)};
            }
        }

        break;
    case pb::Response::kReleased:
        return released{};
    case pb::Response::kValue:
        if (auto data = read_value(input.value())) {
            return std::move(*data);
        }

        break;
    case pb::Response::kError:
        if (input.error().code() >= pb::INVALID_REQUEST &&
            input.error().code() <= pb::NUMERIC_OVERFLOW) {
            return remote_error{static_cast<error_code>(input.error().code()),
                                input.error().message()};
        }

        break;
    default:
        break;
    }

    return std::nullopt;
}

void write_outcome(const outcome& input, pb::Response& output)
{
    std::visit(
        [&output](const auto& item) {
            using type = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<type, classes_page>) {
                for (const auto& name : item.names) {
                    output.mutable_classes()->add_names(name);
                }

                output.mutable_classes()->set_next(item.next);
            }
            else if constexpr (std::is_same_v<type, class_description>) {
                write_description(item, *output.mutable_description());
            }
            else if constexpr (std::is_same_v<type, value_record>) {
                write_record(item, *output.mutable_value_record());
            }
            else if constexpr (std::is_same_v<type, values_page>) {
                for (const auto& record : item.values) {
                    write_record(record, *output.mutable_values()->add_values());
                }

                output.mutable_values()->set_next(item.next);
            }
            else if constexpr (std::is_same_v<type, object_snapshot>) {
                output.mutable_object()->set_id(item.id);
                output.mutable_object()->set_class_name(item.class_name);
                write_named(item.state, *output.mutable_object()->mutable_state());
            }
            else if constexpr (std::is_same_v<type, released>) {
                output.mutable_released();
            }
            else if constexpr (std::is_same_v<type, value>) {
                write_value(item, *output.mutable_value());
            }
            else if constexpr (std::is_same_v<type, remote_error>) {
                output.mutable_error()->set_code(static_cast<pb::ErrorCode>(item.code));
                output.mutable_error()->set_message(item.message);
            }
        },
        input);
}

template <typename Envelope> std::optional<failure> validate_envelope(const Envelope& message)
{
    const auto id = message.request_id();
    if (id == 0) {
        return failure{failure_code::invalid_fields, true, std::nullopt};
    }

    if (!message.has_version()) {
        return failure{failure_code::invalid_fields, false, id};
    }

    if (message.version() != version) {
        return failure{failure_code::unsupported_version, false, id};
    }

    std::size_t nodes = max_message_nodes;
    int unknowns = max_unknown_fields;
    if (const auto error = validate_message(message, nodes, unknowns)) {
        return failure{*error, false, id};
    }

    return std::nullopt;
}

template <typename Envelope>
std::optional<failure> parse(std::span<const std::byte> payload, Envelope& message)
{
    if (payload.empty() || payload.size() > max_payload_bytes) {
        return failure{failure_code::limit_exceeded, true, std::nullopt};
    }

    google::protobuf::io::ArrayInputStream source(payload.data(), static_cast<int>(payload.size()));
    google::protobuf::io::CodedInputStream input(&source);
    input.SetTotalBytesLimit(static_cast<int>(max_payload_bytes));
    input.SetRecursionLimit(max_recursion_depth);
    if (!message.ParseFromCodedStream(&input) || !input.ConsumedEntireMessage()) {
        return failure{failure_code::malformed, true, std::nullopt};
    }

    return validate_envelope(message);
}

template <typename Envelope> result<bytes> serialize(const Envelope& message)
{
    const auto count = message.ByteSizeLong();
    if (count > max_payload_bytes) {
        return failure{failure_code::limit_exceeded, false, message.request_id()};
    }

    bytes output(count);
    if (!message.SerializeToArray(output.data(), static_cast<int>(count))) {
        return failure{failure_code::malformed, true, std::nullopt};
    }

    return output;
}
} // namespace

std::uint32_t supported_version()
{
    return version;
}

result<request> decode_request(std::span<const std::byte> payload)
{
    pb::Request message;
    if (const auto error = parse(payload, message)) {
        return *error;
    }

    if (auto body = read_operation(message)) {
        return request{message.request_id(), std::move(*body), message.version()};
    }

    return failure{failure_code::invalid_fields, false, message.request_id()};
}

result<response> decode_response(std::span<const std::byte> payload)
{
    pb::Response message;
    if (const auto error = parse(payload, message)) {
        return *error;
    }

    if (auto body = read_outcome(message)) {
        return response{message.request_id(), std::move(*body), message.version()};
    }

    return failure{failure_code::invalid_fields, false, message.request_id()};
}

result<bytes> encode_request(const request& input)
{
    pb::Request message;
    message.set_version(input.protocol_version);
    message.set_request_id(input.id);
    write_operation(input.body, message);
    if (auto error = validate_envelope(message)) {
        return *error;
    }

    if (!read_operation(message)) {
        return failure{failure_code::invalid_fields, false, input.id};
    }

    return serialize(message);
}

result<bytes> encode_response(const response& input)
{
    pb::Response message;
    message.set_version(input.protocol_version);
    message.set_request_id(input.id);
    write_outcome(input.body, message);
    if (auto error = validate_envelope(message)) {
        return *error;
    }

    if (!read_outcome(message)) {
        return failure{failure_code::invalid_fields, false, input.id};
    }

    return serialize(message);
}
} // namespace pipeforge::protocol
