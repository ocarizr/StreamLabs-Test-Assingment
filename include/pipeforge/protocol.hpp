#ifndef PIPEFORGE_PROTOCOL_HPP
#define PIPEFORGE_PROTOCOL_HPP

#include "pipeforge/contracts.hpp"

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
