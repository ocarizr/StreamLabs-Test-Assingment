#ifndef PIPEFORGE_PROTOCOL_HPP
#define PIPEFORGE_PROTOCOL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

/**
 * Transport-independent version-1 contracts. Generated types stay internal.
 * Codecs are stateless and may run concurrently on immutable inputs. Returned
 * messages/buffers own their storage; allocation failures are not translated.
 */
namespace pipeforge::protocol {
inline constexpr std::uint32_t version = 1;
inline constexpr std::size_t max_payload_bytes = std::size_t{1024} * 1024;
inline constexpr std::size_t max_string_bytes = std::size_t{64} * 1024;
inline constexpr unsigned int frame_header_bytes = 4;
inline constexpr std::size_t max_entries = 64;
inline constexpr std::size_t max_name_bytes = 128;
inline constexpr int max_recursion_depth = 16;
inline constexpr std::uint32_t default_page_size = 32;

using bytes = std::vector<std::byte>;
using value = std::variant<std::string, std::int64_t, double>;

/** Stable wire codes. Expected domain failures are data, not thrown exceptions. */
enum class error_code : std::uint8_t {
    invalid_request = 1,
    not_found,
    quota_exceeded,
    timeout,
    cancelled,
    access_denied,
    transport_failure,
    internal_failure,
    unsupported_version,
    numeric_overflow
};
enum class value_kind : std::uint8_t { text = 1, integer, real };

struct named_value {
    std::string name;
    value data;
    bool operator==(const named_value&) const = default;
};

struct value_record {
    std::uint64_t id = 0;
    value data;
    bool operator==(const value_record&) const = default;
};

struct parameter {
    std::string name;
    value_kind kind = value_kind::text;
    bool operator==(const parameter&) const = default;
};

struct attribute {
    std::string name;
    value_kind kind = value_kind::text;
    bool read_only = true;
    bool operator==(const attribute&) const = default;
};

struct method {
    std::string name;
    std::vector<parameter> parameters;
    value_kind result_kind = value_kind::integer;
    bool operator==(const method&) const = default;
};

struct class_description {
    std::string name;
    std::vector<attribute> attributes;
    std::vector<method> methods;
    std::vector<parameter> constructor_parameters{};
    bool operator==(const class_description&) const = default;
};

struct object_snapshot {
    std::uint64_t id = 0;
    std::string class_name;
    std::vector<named_value> state;
    bool operator==(const object_snapshot&) const = default;
};

// Cursor zero/empty starts a page. A zero/empty response cursor ends pagination.
struct list_classes {
    std::string after;
    std::uint32_t limit = default_page_size;
    bool operator==(const list_classes&) const = default;
};

struct describe_class {
    std::string name;
    bool operator==(const describe_class&) const = default;
};

struct put_value {
    value data;
    bool operator==(const put_value&) const = default;
};

struct get_value {
    std::uint64_t id = 0;
    bool operator==(const get_value&) const = default;
};

struct list_values {
    std::uint64_t after = 0;
    std::uint32_t limit = default_page_size;
    bool operator==(const list_values&) const = default;
};

struct create_object {
    std::string class_name;
    std::vector<named_value> arguments;
    bool operator==(const create_object&) const = default;
};

struct get_object {
    std::uint64_t id = 0;
    bool operator==(const get_object&) const = default;
};

struct release_object {
    std::uint64_t id = 0;
    bool operator==(const release_object&) const = default;
};

struct get_attribute {
    std::uint64_t id = 0;
    std::string name;
    bool operator==(const get_attribute&) const = default;
};

struct invoke_method {
    std::uint64_t id = 0;
    std::string name;
    std::vector<named_value> arguments;
    bool operator==(const invoke_method&) const = default;
};

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

struct classes_page {
    std::vector<std::string> names;
    std::string next;
    bool operator==(const classes_page&) const = default;
};

struct values_page {
    std::vector<value_record> values;
    std::uint64_t next = 0;
    bool operator==(const values_page&) const = default;
};

struct released {
    bool operator==(const released&) const = default;
};

struct remote_error {
    error_code code = error_code::internal_failure;
    std::string message;
    bool operator==(const remote_error&) const = default;
};

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
    std::uint64_t id = 0;
    operation body;
    std::uint32_t protocol_version = version;
    bool operator==(const request&) const = default;
};

struct response {
    std::uint64_t id = 0;
    outcome body;
    std::uint32_t protocol_version = version;
    bool operator==(const response&) const = default;
};

enum class failure_code : std::uint8_t {
    malformed,
    invalid_fields,
    unsupported_version,
    limit_exceeded,
    truncated
};

/**
 * A codec/framing failure. close_session forbids continuing that byte stream.
 * A trusted correlation ID is supplied only after complete parsing. M3 maps
 * recoverable semantic failures to remote_error without reflecting peer data.
 */
struct failure {
    failure_code code = failure_code::malformed;
    bool close_session = true;
    std::optional<std::uint64_t> request_id;
    bool operator==(const failure&) const = default;
};

template <typename T> using result = std::variant<T, failure>;

/** Protocol revision supported by this linked codec; also used by CLI capability reporting. */
[[nodiscard]] std::uint32_t supported_version();

/** Encode/validate a payload (without framing). Expected validation failures are returned. */
[[nodiscard]] result<bytes> encode_request(const request& message);
[[nodiscard]] result<bytes> encode_response(const response& message);
/** Decode one entire bounded payload. Unknown fields are bounded, discarded, and never echoed. */
[[nodiscard]] result<request> decode_request(std::span<const std::byte> payload);
[[nodiscard]] result<response> decode_response(std::span<const std::byte> payload);
/** Prefix a nonempty payload with its unsigned 32-bit little-endian byte count. */
[[nodiscard]] result<bytes> frame(std::span<const std::byte> payload);

struct frame_progress {
    std::size_t consumed = 0;
    std::optional<bytes> payload;
};

/**
 * Single-owner incremental stream decoder; does no I/O. consume() stops after
 * one frame, reports consumed bytes, and buffers at most one bounded payload.
 * Call again on the unconsumed suffix for concatenated frames. Errors poison
 * this decoder; create a fresh instance for a new session. Not thread-safe.
 */
class frame_decoder {
  public:
    [[nodiscard]] result<frame_progress> consume(std::span<const std::byte> input);
    /** At peer EOF, report partial header/payload; clean frame boundaries succeed. */
    [[nodiscard]] std::optional<failure> finish() const;

  private:
    std::array<std::byte, frame_header_bytes> header_{};
    std::size_t header_used_ = 0;
    std::size_t expected_ = 0;
    bytes payload_;
    bool poisoned_ = false;
};
} // namespace pipeforge::protocol
#endif // PIPEFORGE_PROTOCOL_HPP
