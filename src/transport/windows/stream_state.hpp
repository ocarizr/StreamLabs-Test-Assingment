#ifndef PIPEFORGE_STREAM_STATE_HPP
#define PIPEFORGE_STREAM_STATE_HPP

#include <atomic>

#include "native.hpp"

namespace pipeforge::transport {
class stream_state {
  public:
    explicit stream_state(std::shared_ptr<native::unique_handle> pipe) : pipe(std::move(pipe)) {}

    std::shared_ptr<native::unique_handle> pipe;
    std::array<std::byte, native::buffer_bytes> input{};
    std::size_t begin = 0;
    std::size_t end = 0;
    protocol::frame_decoder decoder;
    std::atomic<bool> failed{false};
};
} // namespace pipeforge::transport
#endif // PIPEFORGE_STREAM_STATE_HPP
