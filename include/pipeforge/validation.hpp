/** @file
 * Shared field, metadata and resource validation.
 */
#ifndef PIPEFORGE_VALIDATION_HPP
#define PIPEFORGE_VALIDATION_HPP

#include "pipeforge/contracts.hpp"

#include <algorithm>
#include <set>
#include <span>
#include <string_view>

namespace pipeforge::protocol {
/** Aggregate known-message cardinality ceiling after bounded parsing. */
inline constexpr std::size_t max_message_nodes = 4096;
/** ASCII identifiers, shared by codec, registry and direct domain callers. */
[[nodiscard]] bool valid_name(std::string_view name, bool allow_empty = false);
/** Accept only the three supported value kinds. */
[[nodiscard]] bool valid_kind(value_kind kind);
/** Finite numbers or valid UTF-8 text within max_string_bytes, including empty text. */
[[nodiscard]] bool valid_value(const value& data);
/** Alternative type of a valid typed value, independent of serialization. */
[[nodiscard]] value_kind kind_of(const value& data);
/** Logical payload charge: text byte length, or sizeof(int64_t) for a scalar. */
[[nodiscard]] std::size_t value_bytes(const value& data);
/** Nonzero page limits bounded by max_entries. */
[[nodiscard]] bool valid_page_size(std::uint32_t size);

/** Names are valid and unique within this sequence; no ownership is retained. */
template <typename Range> [[nodiscard]] bool valid_names(const Range& entries)
{
    std::set<std::string_view> names;
    return std::ranges::all_of(entries, [&names](const auto& entry) {
        return valid_name(entry.name) && names.insert(entry.name).second;
    });
}

/** Bound cardinality and validate each named value. */
[[nodiscard]] bool valid_named_values(std::span<const named_value> entries);
/** Exact named parameter set and types, regardless of argument ordering. */
[[nodiscard]] bool valid_arguments(std::span<const named_value> arguments,
                                   std::span<const parameter> parameters);
/** Validate all metadata, uniqueness, kinds and aggregate message-node bounds. */
[[nodiscard]] bool valid_description(const class_description& description);
/** Stable diagnostic text without peer input; invalid codes become internal_failure. */
[[nodiscard]] remote_error make_error(error_code code);
} // namespace pipeforge::protocol
#endif // PIPEFORGE_VALIDATION_HPP
