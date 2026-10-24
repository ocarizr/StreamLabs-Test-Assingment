#include "pipeforge/client.hpp"

namespace pipeforge::client {
namespace {
/** Adapt the already-framed M4 stream; never frame an envelope twice. */
class pipe_channel final : public channel {
  public:
    explicit pipe_channel(transport::pipe_stream stream) : stream_(std::move(stream)) {}

    transport::result<std::size_t> send(std::span<const std::byte> payload,
                                        transport::deadline until,
                                        const std::stop_token& stop) override
    {
        return stream_.send(payload, until, stop);
    }

    transport::result<protocol::bytes> receive(transport::deadline until,
                                               const std::stop_token& stop) override
    {
        return stream_.receive(until, stop);
    }

    void close() noexcept override
    {
        stream_.close();
    }

  private:
    transport::pipe_stream stream_;
};
} // namespace

connector named_pipe_connector()
{
    return [](std::wstring_view endpoint,
              transport::deadline until,
              const std::stop_token& stop) -> transport::result<std::unique_ptr<channel>> {
        auto opened = transport::pipe_stream::connect(endpoint, until, stop);
        if (const auto* error = std::get_if<transport::failure>(&opened)) {
            return *error;
        }

        return std::make_unique<pipe_channel>(std::get<transport::pipe_stream>(std::move(opened)));
    };
}
} // namespace pipeforge::client
