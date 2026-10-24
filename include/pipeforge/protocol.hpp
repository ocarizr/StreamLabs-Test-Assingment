/** @file
 * Bounded Protobuf payload codecs and incremental framing.
 */
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
/** Codec and incremental framing rejection category. */
enum class failure_code : std::uint8_t {
    malformed,           ///< Input cannot be parsed as the expected envelope.
    invalid_fields,      ///< Parsed contract fields violate semantic rules.
    unsupported_version, ///< Parsed major protocol revision is unsupported.
    limit_exceeded,      ///< Payload, cardinality or parser budget exceeded.
    truncated,           ///< Peer EOF left a partial header or payload.
};

/**
 * A codec/framing failure. close_session forbids continuing that byte stream.
 * A trusted correlation ID is supplied only after complete parsing. The dispatcher maps
 * recoverable semantic failures to remote_error without reflecting peer data.
 */
struct failure {
    failure_code code = failure_code::malformed; ///< Stable codec/framing rejection category.
    bool close_session = true; ///< Whether the receiving byte stream is no longer usable.
    std::optional<std::uint64_t>
        request_id; ///< Trusted correlation ID only after complete envelope parsing.
    /** Structural comparison of rejection and correlation details. */
    bool operator==(const failure&) const = default;
};

/** Validated codec result or explicit framing/semantic failure. */
template <typename T> using result = std::variant<T, failure>;

/** Protocol revision supported by this linked codec; also used by CLI capability reporting. */
[[nodiscard]] std::uint32_t supported_version();

/** Encode/validate a payload (without framing). Expected validation failures are returned. */
[[nodiscard]] result<bytes> encode_request(const request& message);
/** Validate and serialize a complete response payload without framing. */
[[nodiscard]] result<bytes> encode_response(const response& message);
/** Decode one entire bounded payload. Unknown fields are bounded, discarded, and never echoed. */
[[nodiscard]] result<request> decode_request(std::span<const std::byte> payload);
/** Parse one complete bounded response, validating correlation and outcome fields. */
[[nodiscard]] result<response> decode_response(std::span<const std::byte> payload);
/** Prefix a nonempty payload with its unsigned 32-bit little-endian byte count. */
[[nodiscard]] result<bytes> frame(std::span<const std::byte> payload);

/** Result of consuming an input prefix; the caller retains the unconsumed suffix. */
struct frame_progress {
    std::size_t consumed = 0;     ///< Bytes consumed from this input slice before stopping.
    std::optional<bytes> payload; ///< Owned complete payload, absent until the frame is complete.
};

/**
 * Single-owner incremental stream decoder; does no I/O. consume() stops after
 * one frame, reports consumed bytes, and buffers at most one bounded payload.
 * Call again on the unconsumed suffix for concatenated frames. Errors poison
 * this decoder; create a fresh instance for a new session. Not thread-safe.
 */
class frame_decoder {
  public:
    /** Consume through at most one frame; retain partial input and poison on failure. */
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
