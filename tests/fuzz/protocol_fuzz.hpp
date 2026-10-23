#ifndef PIPEFORGE_PROTOCOL_FUZZ_HPP
#define PIPEFORGE_PROTOCOL_FUZZ_HPP
#include <cstddef>
#include <span>

namespace pipeforge::test_support {
/** Shared invariant harness for deterministic regressions and libFuzzer. */
void exercise_protocol(std::span<const std::byte> input);
} // namespace pipeforge::test_support
#endif
