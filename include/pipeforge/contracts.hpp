/** @file
 * Owning values, operation metadata and versioned request/response contracts.
 */
#ifndef PIPEFORGE_CONTRACTS_HPP
#define PIPEFORGE_CONTRACTS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

/** Shared value contracts; no generated types, serialization or platform dependencies. */
namespace pipeforge::protocol {
/** Supported major wire revision. */
inline constexpr std::uint32_t version = 1;
/** Maximum bytes in one serialized payload, excluding its frame header. */
inline constexpr std::size_t max_payload_bytes = std::size_t{1024} * 1024;
/** Maximum UTF-8 bytes in one text value or unknown length-delimited field. */
inline constexpr std::size_t max_string_bytes = std::size_t{64} * 1024;
/** Little-endian uint32 payload-length prefix size. */
inline constexpr unsigned int frame_header_bytes = 4;
/** Maximum entries in any single contract collection. */
inline constexpr std::size_t max_entries = 64;
/** Maximum ASCII identifier length. */
inline constexpr std::size_t max_name_bytes = 128;
/** Maximum Protobuf parser nesting depth. */
inline constexpr int max_recursion_depth = 16;
/** Default requested page cardinality. */
inline constexpr std::uint32_t default_page_size = 32;

/** Owning binary buffer; never a view into another operation. */
using bytes = std::vector<std::byte>;
/** Exactly one UTF-8 string, signed int64 or finite double. */
using value = std::variant<std::string, std::int64_t, double>;

/** Stable wire codes. Expected domain failures are data, not thrown exceptions. */
enum class error_code : std::uint8_t {
    invalid_request = 1, ///< Malformed semantic fields or incorrect arguments.
    not_found,           ///< Unknown registered name or session-owned identity.
    quota_exceeded,      ///< Storage, cardinality or logical byte budget exhausted.
    timeout,             ///< Operation deadline expired.
    cancelled,           ///< Operation cancelled.
    access_denied,       ///< Requested operation is not authorized.
    transport_failure,   ///< Transport-level failure category.
    internal_failure,    ///< Trusted server adapter failed its declared contract.
    unsupported_version, ///< Wire revision is not supported.
    numeric_overflow,    ///< Arithmetic would exceed signed int64 bounds; state is unchanged.
};
/** Scalar alternatives permitted by the wire contract. */
enum class value_kind : std::uint8_t {
    text = 1, ///< Bounded valid UTF-8 text.
    integer,  ///< Signed 64-bit integer.
    real,     ///< Finite IEEE-754 double.
};

/** One uniquely named argument or state field. */
struct named_value {
    std::string name; ///< Validated ASCII field identifier.
    value data;       ///< Owning typed field value.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const named_value&) const = default;
};

/** A retained session value and its opaque identity. */
struct value_record {
    std::uint64_t id = 0; ///< Nonzero server-assigned identity.
    value data;           ///< Owning copy of the stored value.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const value_record&) const = default;
};

/** Required named parameter in constructor or method metadata. */
struct parameter {
    std::string name;                   ///< Unique argument identifier.
    value_kind kind = value_kind::text; ///< Required scalar alternative.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const parameter&) const = default;
};

/** Discoverable attribute contract. */
struct attribute {
    std::string name;                   ///< Attribute identifier within its class.
    value_kind kind = value_kind::text; ///< Scalar alternative returned by reads.
    bool read_only = true;              ///< Whether the metadata prohibits attribute writes.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const attribute&) const = default;
};

/** Discoverable server-side operation contract. */
struct method {
    std::string name;                             ///< Method identifier within its class.
    std::vector<parameter> parameters;            ///< Exact required named argument set.
    value_kind result_kind = value_kind::integer; ///< Scalar alternative returned on success.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const method&) const = default;
};

/** Discoverable registered class metadata; all collections are bounded. */
struct class_description {
    std::string name;                  ///< Unique startup registry identifier.
    std::vector<attribute> attributes; ///< Declared readable state fields.
    std::vector<method> methods;       ///< Declared invocable server methods.
    std::vector<parameter>
        constructor_parameters{}; ///< Exact named argument set required by the factory.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const class_description&) const = default;
};

/** Owning value snapshot of server state; never contains executable memory. */
struct object_snapshot {
    std::uint64_t id = 0;           ///< Session-owned object identity.
    std::string class_name;         ///< Registered factory identifier.
    std::vector<named_value> state; ///< Current named attribute values.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const object_snapshot&) const = default;
};

// Cursor zero/empty starts a page. A zero/empty response cursor ends pagination.
/** Request sorted class names after an exclusive cursor. */
struct list_classes {
    std::string after; ///< Empty starts enumeration; otherwise the last observed class name.
    std::uint32_t limit = default_page_size; ///< Nonzero page limit, at most max_entries.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const list_classes&) const = default;
};

/** Request the metadata of one registered class. */
struct describe_class {
    std::string name; ///< Exact registry identifier.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const describe_class&) const = default;
};

/** Retain one typed value in the current session. */
struct put_value {
    value data; ///< Value copied into the server repository.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const put_value&) const = default;
};

/** Retrieve one value owned by the current session. */
struct get_value {
    std::uint64_t id = 0; ///< Nonzero retained value identity.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const get_value&) const = default;
};

/** Request retained values after an exclusive numeric cursor. */
struct list_values {
    std::uint64_t after = 0; ///< Zero starts enumeration; otherwise the last observed value ID.
    std::uint32_t limit = default_page_size; ///< Nonzero page limit, at most max_entries.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const list_values&) const = default;
};

/** Construct a server object through a trusted registered factory. */
struct create_object {
    std::string class_name;             ///< Exact registry identifier.
    std::vector<named_value> arguments; ///< Unique named constructor arguments matching metadata.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const create_object&) const = default;
};

/** Retrieve identity, type and state of a session-owned object. */
struct get_object {
    std::uint64_t id = 0; ///< Nonzero retained object identity.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const get_object&) const = default;
};

/** Destroy a session-owned object and invalidate its ID. */
struct release_object {
    std::uint64_t id = 0; ///< Nonzero retained object identity.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const release_object&) const = default;
};

/** Read one declared attribute of a session-owned object. */
struct get_attribute {
    std::uint64_t id = 0; ///< Nonzero retained object identity.
    std::string name;     ///< Declared attribute identifier.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const get_attribute&) const = default;
};

/** Invoke bounded server-side behavior on a session-owned object. */
struct invoke_method {
    std::uint64_t id = 0;               ///< Nonzero retained object identity.
    std::string name;                   ///< Declared method identifier.
    std::vector<named_value> arguments; ///< Unique named arguments matching method metadata.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const invoke_method&) const = default;
};

/** Closed set of version-1 operations. */
using operation = std::variant<list_classes,
                               describe_class,
                               put_value,
                               get_value,
                               list_values,
                               create_object,
                               get_object,
                               release_object,
                               get_attribute,
                               invoke_method>;

/** Sorted class-name page with an exclusive continuation cursor. */
struct classes_page {
    std::vector<std::string> names; ///< Unique increasing registry identifiers.
    std::string next;               ///< Last returned name if more remain; empty ends enumeration.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const classes_page&) const = default;
};

/** Increasing value-ID page with an exclusive continuation cursor. */
struct values_page {
    std::vector<value_record> values; ///< Owning typed records in increasing ID order.
    std::uint64_t next = 0;           ///< Last returned ID if more remain; zero ends enumeration.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const values_page&) const = default;
};

/** Successful acknowledgement of explicit object destruction. */
struct released {
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const released&) const = default;
};

/** Correlated semantic failure that may leave the session usable. */
struct remote_error {
    error_code code = error_code::internal_failure; ///< Stable failure category.
    std::string message; ///< Bounded trusted description; never reflect peer input.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const remote_error&) const = default;
};

/** Closed set of success results and structured remote errors. */
using outcome = std::variant<classes_page,
                             class_description,
                             value_record,
                             values_page,
                             object_snapshot,
                             released,
                             value,
                             remote_error>;

/** Correlation IDs are nonzero and unique among outstanding calls within a session. */
struct request {
    std::uint64_t id = 0; ///< Nonzero unique correlation ID among pending calls.
    operation body;       ///< Exactly one typed operation.
    std::uint32_t protocol_version = version; ///< Explicit major wire revision; currently version.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const request&) const = default;
};

/** Correlated reply containing either a typed outcome or a remote error. */
struct response {
    std::uint64_t id = 0; ///< Echo of the trusted request correlation ID.
    outcome body;         ///< Exactly one outcome matching the request or a remote error.
    std::uint32_t protocol_version = version; ///< Explicit supported major wire revision.
    /// Structural equality, including the selected scalar alternative.
    bool operator==(const response&) const = default;
};

} // namespace pipeforge::protocol
#endif // PIPEFORGE_CONTRACTS_HPP
