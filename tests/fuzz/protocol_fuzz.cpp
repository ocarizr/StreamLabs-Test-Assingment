#include "protocol_fuzz.hpp"

#include "pipeforge/protocol.hpp"

#include <algorithm>
#include <cstdlib>

namespace pipeforge::test_support {
namespace {
template <typename Message, typename Encoder, typename Decoder>
void check_round_trip(const protocol::result<Message>& decoded, Encoder encode, Decoder decode)
{
    if (const auto* message = std::get_if<Message>(&decoded)) {
        const auto encoded = encode(*message);
        const auto* payload = std::get_if<protocol::bytes>(&encoded);
        if (payload == nullptr) {
            std::abort();
        }

        const auto again = decode(*payload);
        const auto* restored = std::get_if<Message>(&again);
        if (restored == nullptr || *restored != *message) {
            std::abort();
        }
    }
}
} // namespace

void exercise_protocol(std::span<const std::byte> input)
{
    check_round_trip(
        protocol::decode_request(input), protocol::encode_request, protocol::decode_request);
    check_round_trip(
        protocol::decode_response(input), protocol::encode_response, protocol::decode_response);
    protocol::frame_decoder decoder;
    while (!input.empty()) {
        constexpr std::size_t chunk_limit = 32;
        const auto count =
            std::min(input.size(), std::to_integer<std::size_t>(input.front()) % chunk_limit + 1);
        const auto progress = decoder.consume(input.first(count));
        if (std::holds_alternative<protocol::failure>(progress)) {
            break;
        }

        const auto& step = std::get<protocol::frame_progress>(progress);
        if (step.consumed == 0 || step.consumed > count) {
            std::abort();
        }

        if (step.payload) {
            check_round_trip(protocol::decode_request(*step.payload),
                             protocol::encode_request,
                             protocol::decode_request);
            check_round_trip(protocol::decode_response(*step.payload),
                             protocol::encode_response,
                             protocol::decode_response);
        }

        input = input.subspan(step.consumed);
    }

    static_cast<void>(decoder.finish());
}
} // namespace pipeforge::test_support
