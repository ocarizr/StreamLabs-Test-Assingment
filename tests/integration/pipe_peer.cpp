#include <span>
#include <string>

#include "native.hpp"

// Fatal allocation failures are not recoverable I/O errors in this test peer.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* argv[])
{
    if (argc != 2) {
        return 1;
    }

    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    const std::string_view endpoint(arguments.subspan(1).front());
    const std::wstring name(endpoint.begin(), endpoint.end());
    namespace t = pipeforge::transport;
    namespace p = pipeforge::protocol;
    auto connected = t::pipe_stream::connect(name, t::clock::now() + t::default_timeout);
    if (!std::holds_alternative<t::pipe_stream>(connected)) {
        return 1;
    }

    auto& pipe = std::get<t::pipe_stream>(connected);
    const auto payload = p::encode_request({1, p::list_classes{}});
    if (!std::holds_alternative<p::bytes>(payload)) {
        return 1;
    }

    if (!std::holds_alternative<std::size_t>(
            pipe.send(std::get<p::bytes>(payload), t::clock::now() + t::default_timeout))) {
        return 1;
    }

    if (!std::holds_alternative<p::bytes>(pipe.receive(t::clock::now() + t::default_timeout))) {
        return 1;
    }

    const std::array<std::byte, 2> partial_header{std::byte{1}, std::byte{0}};
    const auto sent = t::native::write_some(
        pipe.native_handle(), partial_header, t::clock::now() + t::default_timeout, {});
    if (!std::holds_alternative<std::size_t>(sent)) {
        return 1;
    }

    // Deliberately bypass C++ teardown to exercise OS cleanup on peer process exit.
    ExitProcess(0);
}
