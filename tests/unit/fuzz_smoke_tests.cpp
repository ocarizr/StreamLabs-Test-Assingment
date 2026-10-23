#include "pipeforge/protocol.hpp"

#include <random>

#include <gtest/gtest.h>

#include "../fuzz/protocol_fuzz.hpp"

namespace pipeforge::test_support {
TEST(ProtocolFuzz, SeededMutationsExerciseCodecAndFragmentedFrames)
{
    const auto encoded = protocol::encode_request({1, protocol::put_value{std::int64_t{-1}}});
    ASSERT_TRUE(std::holds_alternative<protocol::bytes>(encoded));
    const auto& payload = std::get<protocol::bytes>(encoded);
    exercise_protocol(payload);
    const auto framed = protocol::frame(payload);
    ASSERT_TRUE(std::holds_alternative<protocol::bytes>(framed));
    exercise_protocol(std::get<protocol::bytes>(framed));
    constexpr unsigned int seed = 2026;
    constexpr unsigned int trials = 512;
    constexpr unsigned int byte_mask = 0xff;
    // Reproducible regression mutations, not security tokens or production randomness.
    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::mt19937 random(seed);
    for (unsigned int trial = 0; trial < trials; ++trial) {
        auto mutation = payload;
        for (auto& byte : mutation) {
            if (random() % 2 == 0) {
                byte = static_cast<std::byte>(random() & byte_mask);
            }
        }

        exercise_protocol(mutation);
    }
}
} // namespace pipeforge::test_support
