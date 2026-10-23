#include "pipeforge/protocol.hpp"

#include <algorithm>
#include <iterator>

namespace pipeforge::protocol {
result<bytes> frame(std::span<const std::byte> payload)
{
    if (payload.empty() || payload.size() > max_payload_bytes) {
        return failure{failure_code::limit_exceeded, true, std::nullopt};
    }

    const auto size = static_cast<std::uint32_t>(payload.size());
    constexpr std::uint32_t byte_mask = 0xff;
    constexpr unsigned int byte_bits = 8;
    bytes output;
    output.reserve(payload.size() + frame_header_bytes);
    for (unsigned int index = 0; index < frame_header_bytes; ++index) {
        output.push_back(static_cast<std::byte>((size >> (index * byte_bits)) & byte_mask));
    }

    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

result<frame_progress> frame_decoder::consume(std::span<const std::byte> input)
{
    if (poisoned_) {
        return failure{failure_code::malformed, true, std::nullopt};
    }

    const auto original_size = input.size();
    if (header_used_ < header_.size()) {
        auto available = std::span{header_}.subspan(header_used_);
        const auto count = std::min(input.size(), available.size());
        std::ranges::copy(input.first(count), available.begin());
        input = input.subspan(count);
        header_used_ += count;
        if (header_used_ != header_.size()) {
            return frame_progress{count, std::nullopt};
        }

        std::uint32_t length = 0;
        constexpr unsigned int byte_bits = 8;
        unsigned int shift = 0;
        for (const auto byte : header_) {
            length |= std::to_integer<std::uint32_t>(byte) << shift;
            shift += byte_bits;
        }

        if (length == 0 || length > max_payload_bytes) {
            poisoned_ = true;
            return failure{failure_code::limit_exceeded, true, std::nullopt};
        }

        expected_ = length;
        payload_.reserve(expected_);
    }

    const auto count = std::min(input.size(), expected_ - payload_.size());
    const auto chunk = input.first(count);
    payload_.insert(payload_.end(), chunk.begin(), chunk.end());
    input = input.subspan(count);
    if (payload_.size() != expected_) {
        return frame_progress{original_size - input.size(), std::nullopt};
    }

    auto completed = std::move(payload_);
    payload_ = {};
    header_used_ = 0;
    expected_ = 0;
    return frame_progress{original_size - input.size(), std::move(completed)};
}

std::optional<failure> frame_decoder::finish() const
{
    if (poisoned_ || header_used_ != 0) {
        return failure{
            poisoned_ ? failure_code::malformed : failure_code::truncated, true, std::nullopt};
    }

    return std::nullopt;
}
} // namespace pipeforge::protocol
