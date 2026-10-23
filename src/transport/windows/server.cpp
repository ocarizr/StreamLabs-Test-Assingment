#include "pipeforge/diagnostics.hpp"
#include "pipeforge/pipe_server.hpp"
#include "pipeforge/services.hpp"

#include <atomic>
#include <mutex>
#include <thread>

#include "stream_state.hpp"

namespace pipeforge::transport {
class server_state {
  public:
    server_state(domain::class_registry registry,
                 server_options options,
                 diagnostic_session* diagnostics)
        : registry_(std::move(registry)), options_(std::move(options)), diagnostics_(diagnostics)
    {
    }

    ~server_state()
    {
        stop();
    }

    server_state(const server_state&) = delete;
    server_state& operator=(const server_state&) = delete;
    server_state(server_state&&) = delete;
    server_state& operator=(server_state&&) = delete;

    [[nodiscard]] std::optional<failure> launch()
    {
        auto descriptor = native::logon_security();
        if (const auto* error = std::get_if<failure>(&descriptor)) {
            return *error;
        }

        pipes_.reserve(options_.sessions);
        workers_.reserve(options_.sessions);
        for (std::size_t index = 0; index < options_.sessions; ++index) {
            auto pipe = native::create_instance(options_.endpoint,
                                                options_.sessions,
                                                index == 0,
                                                std::get<native::local_memory>(descriptor).get());
            if (const auto* error = std::get_if<failure>(&pipe)) {
                return *error;
            }

            pipes_.push_back(std::move(std::get<std::shared_ptr<native::unique_handle>>(pipe)));
        }

        try {
            for (const auto& pipe : pipes_) {
                workers_.emplace_back([this, pipe] {
                    run(pipe);
                });
            }
        }
        catch (const std::system_error& error) {
            // Thread creation is an OS-resource acquisition boundary. Stop/join
            // already-created workers before releasing pipe instances.
            stop();
            return failure{failure_kind::io_failure,
                           static_cast<std::uint32_t>(error.code().value())};
        }

        return std::nullopt;
    }

    void request_stop() noexcept
    {
        shutdown_.request_stop();
    }

    void stop() noexcept
    {
        request_stop();
        // jthread destruction joins; the shared stop signal is sent to every
        // worker first, so pending accepts/reads/writes are all cancelled.
        workers_.clear();
        pipes_.clear();
    }

    [[nodiscard]] server_statistics statistics() const noexcept
    {
        return {connections_.load(),
                completed_.load(),
                protocol_errors_.load(),
                io_errors_.load(),
                active_.load(),
                last_error_.load()};
    }

  private:
    void record(failure error)
    {
        if (error.kind == failure_kind::protocol_violation) {
            ++protocol_errors_;
        }
        else if (error.kind != failure_kind::disconnected && !shutdown_.stop_requested()) {
            ++io_errors_;
        }

        if (error.native_error != 0) {
            last_error_ = error.native_error;
        }

        if (diagnostics_ && error.kind != failure_kind::disconnected &&
            !shutdown_.stop_requested()) {
            diagnostics_->log(
                log_level::warning,
                "transport kind=" + std::to_string(static_cast<unsigned int>(error.kind)) +
                    " native=" + std::to_string(error.native_error));
        }
    }

    [[nodiscard]] bool dispatch(pipe_stream& stream,
                                server::dispatcher& service,
                                const protocol::bytes& payload,
                                deadline until,
                                const std::stop_token& stop)
    {
        auto decoded = protocol::decode_request(payload);
        protocol::result<protocol::response> response = protocol::failure{};
        {
            std::unique_lock gate(domain_gate_, std::defer_lock);
            constexpr auto gate_slice = std::chrono::milliseconds{10};
            while (true) {
                if (auto error = native::preflight(until, stop)) {
                    record(*error);
                    return false;
                }

                if (gate.try_lock_for(gate_slice)) {
                    break;
                }
            }

            // Acquiring the gate can consume the last deadline slice. Recheck
            // before dispatch so an expired queued request cannot mutate state.
            if (auto error = native::preflight(until, stop)) {
                record(*error);
                return false;
            }

            if (const auto* error = std::get_if<protocol::failure>(&decoded)) {
                ++protocol_errors_;
                response = service.reject(*error);
            }
            else {
                response = service.dispatch(std::get<protocol::request>(decoded));
            }
        }
        if (std::holds_alternative<protocol::failure>(response)) {
            return false;
        }

        const auto encoded = protocol::encode_response(std::get<protocol::response>(response));
        if (!std::holds_alternative<protocol::bytes>(encoded)) {
            record({failure_kind::protocol_violation});
            return false;
        }

        const auto sent = stream.send(std::get<protocol::bytes>(encoded), until, stop);
        if (const auto* error = std::get_if<failure>(&sent)) {
            record(*error);
            return false;
        }

        ++completed_;
        return true;
    }

    void serve(pipe_stream& stream, domain::session_store& store, const std::stop_token& stop)
    {
        server::dispatcher service(store, diagnostics_);
        while (!stop.stop_requested()) {
            const auto until = clock::now() + options_.request_timeout;
            auto payload = stream.receive(until, stop);
            if (const auto* error = std::get_if<failure>(&payload)) {
                record(*error);
                break;
            }

            if (!dispatch(stream, service, std::get<protocol::bytes>(payload), until, stop)) {
                break;
            }
        }
    }

    void run(const std::shared_ptr<native::unique_handle>& pipe)
    {
        const auto stop = shutdown_.get_token();
        while (!stop.stop_requested()) {
            const auto accepted = native::accept(pipe->get(), deadline::max(), stop);
            if (const auto* error = std::get_if<failure>(&accepted)) {
                if (stop.stop_requested()) {
                    break;
                }

                record(*error);
                DisconnectNamedPipe(pipe->get());
                if (error->kind == failure_kind::disconnected) {
                    continue;
                }

                request_stop();
                break;
            }

            ++connections_;
            ++active_;
            {
                domain::session_store store(registry_, identifiers_, options_.storage);
                pipe_stream stream(std::make_unique<stream_state>(pipe));
                serve(stream, store, stop);
                const std::lock_guard gate(domain_gate_);
                store.close();
            }
            --active_;
            if (!DisconnectNamedPipe(pipe->get())) {
                const auto code = GetLastError();
                if (code != ERROR_PIPE_NOT_CONNECTED) {
                    record(native::io_failure(code));
                    request_stop();
                }
            }
        }
    }

    domain::class_registry registry_;
    domain::identifier_source identifiers_;
    server_options options_;
    diagnostic_session* diagnostics_;
    std::stop_source shutdown_;
    std::timed_mutex domain_gate_;
    std::vector<std::shared_ptr<native::unique_handle>> pipes_;
    std::vector<std::jthread> workers_;
    std::atomic<std::uint64_t> connections_{0};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> protocol_errors_{0};
    std::atomic<std::uint64_t> io_errors_{0};
    std::atomic<std::size_t> active_{0};
    std::atomic<std::uint32_t> last_error_{0};
};

pipe_server::pipe_server(std::unique_ptr<server_state> state) : state_(std::move(state)) {}

pipe_server::~pipe_server() = default;

result<std::unique_ptr<pipe_server>> pipe_server::start(domain::class_registry registry,
                                                        server_options options,
                                                        diagnostic_session* diagnostics)
{
    if (!valid_endpoint(options.endpoint) || options.sessions == 0 ||
        options.sessions > max_sessions || options.request_timeout < minimum_timeout ||
        options.request_timeout > maximum_timeout) {
        return failure{failure_kind::invalid_configuration};
    }

    auto state =
        std::make_unique<server_state>(std::move(registry), std::move(options), diagnostics);
    if (auto error = state->launch()) {
        return *error;
    }

    return std::unique_ptr<pipe_server>(new pipe_server(std::move(state)));
}

void pipe_server::request_stop() noexcept
{
    state_->request_stop();
}

void pipe_server::stop() noexcept
{
    state_->stop();
}

server_statistics pipe_server::statistics() const noexcept
{
    return state_->statistics();
}
} // namespace pipeforge::transport
