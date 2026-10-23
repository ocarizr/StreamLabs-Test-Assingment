#include <cstdint>

#include "protocol_fuzz.hpp"

// libFuzzer's fixed C entry point; fatal allocation exhaustion terminates fuzzing.
// NOLINTNEXTLINE(bugprone-exception-escape,readability-identifier-naming)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    pipeforge::test_support::exercise_protocol(std::as_bytes(std::span{data, size}));
    return 0;
}
