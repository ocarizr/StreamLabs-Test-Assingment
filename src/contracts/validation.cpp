#include "pipeforge/validation.hpp"

#include <cmath>
#include <utf8_validity.h>

namespace pipeforge::protocol {
namespace {
bool valid_parameters(std::span<const parameter> parameters)
{
    return parameters.size() <= max_entries && valid_names(parameters) &&
           std::ranges::all_of(parameters, [](const auto& item) {
               return valid_kind(item.kind);
           });
}
} // namespace

bool valid_name(std::string_view name, bool allow_empty)
{
    if (name.empty()) {
        return allow_empty;
    }

    if (name.size() > max_name_bytes || (name.front() >= '0' && name.front() <= '9')) {
        return false;
    }

    return std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

bool valid_kind(value_kind kind)
{
    return kind == value_kind::text || kind == value_kind::integer || kind == value_kind::real;
}

bool valid_value(const value& data)
{
    if (data.valueless_by_exception()) {
        return false;
    }

    if (const auto* text = std::get_if<std::string>(&data)) {
        return text->size() <= max_string_bytes && utf8_range::IsStructurallyValid(*text);
    }

    const auto* real = std::get_if<double>(&data);
    return !real || std::isfinite(*real);
}

value_kind kind_of(const value& data)
{
    if (std::holds_alternative<std::string>(data)) {
        return value_kind::text;
    }

    return std::holds_alternative<std::int64_t>(data) ? value_kind::integer : value_kind::real;
}

std::size_t value_bytes(const value& data)
{
    const auto* text = std::get_if<std::string>(&data);
    return text ? text->size() : sizeof(std::int64_t);
}

bool valid_page_size(std::uint32_t size)
{
    return size != 0 && size <= max_entries;
}

bool valid_named_values(std::span<const named_value> entries)
{
    return entries.size() <= max_entries && valid_names(entries) &&
           std::ranges::all_of(entries, [](const auto& item) {
               return valid_value(item.data);
           });
}

bool valid_arguments(std::span<const named_value> arguments, std::span<const parameter> parameters)
{
    if (arguments.size() != parameters.size() || !valid_named_values(arguments)) {
        return false;
    }

    return std::ranges::all_of(parameters, [arguments](const auto& parameter) {
        const auto found = std::ranges::find(arguments, parameter.name, &named_value::name);
        return found != arguments.end() && kind_of(found->data) == parameter.kind;
    });
}

bool valid_description(const class_description& description)
{
    if (!valid_name(description.name) || description.attributes.size() > max_entries ||
        description.methods.size() > max_entries || !valid_names(description.attributes) ||
        !valid_names(description.methods) ||
        !valid_parameters(description.constructor_parameters)) {
        return false;
    }

    std::size_t nodes = 1 + description.attributes.size() + description.methods.size() +
                        description.constructor_parameters.size();
    for (const auto& attribute : description.attributes) {
        if (!valid_kind(attribute.kind)) {
            return false;
        }
    }

    for (const auto& method : description.methods) {
        if (!valid_kind(method.result_kind) || !valid_parameters(method.parameters)) {
            return false;
        }

        nodes += method.parameters.size();
    }

    // Leave room for the envelope itself in the codec's aggregate node limit.
    return nodes < max_message_nodes;
}

remote_error make_error(error_code code)
{
    switch (code) {
    case error_code::invalid_request:
        return {code, "Invalid request"};
    case error_code::not_found:
        return {code, "Resource not found"};
    case error_code::quota_exceeded:
        return {code, "Resource limit exceeded"};
    case error_code::timeout:
        return {code, "Operation timed out"};
    case error_code::cancelled:
        return {code, "Operation cancelled"};
    case error_code::access_denied:
        return {code, "Session access denied"};
    case error_code::transport_failure:
        return {code, "Transport failure"};
    case error_code::unsupported_version:
        return {code, "Unsupported protocol version"};
    case error_code::numeric_overflow:
        return {code, "Numeric overflow"};
    case error_code::internal_failure:
    default:
        return {error_code::internal_failure, "Internal failure"};
    }
}
} // namespace pipeforge::protocol
