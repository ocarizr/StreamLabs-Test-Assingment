#include "pipeforge/application.hpp"
#include "pipeforge/diagnostics.hpp"
#include "pipeforge/pipe_server.hpp"

#include <iostream>

namespace pipeforge {
int run_server_command(const invocation& options,
                       std::ostream& output,
                       std::ostream& errors,
                       diagnostic_session& diagnostics)
{
    domain::class_registry registry;
    if (registry.add(domain::counter_definition()) ||
        registry.add(domain::text_buffer_definition())) {
        errors << "Built-in class registration failed.\n";
        return 1;
    }

    auto started = transport::pipe_server::start(
        std::move(registry), {options.endpoint, options.timeout, options.sessions}, &diagnostics);
    if (const auto* error = std::get_if<transport::failure>(&started)) {
        errors << "Server startup failed kind=" << static_cast<unsigned int>(error->kind)
               << " native=" << error->native_error << '\n';
        return 1;
    }

    auto& server = std::get<std::unique_ptr<transport::pipe_server>>(started);
    output << "READY ";
    // Successful host creation already validated this endpoint as ASCII.
    for (const auto character : options.endpoint) {
        output.put(static_cast<char>(character));
    }

    output << '\n' << std::flush;
    if (!output) {
        server->stop();
        return 1;
    }

    // stdin is the controller's shutdown channel. No global signal handler owns
    // the host or destroys it while its worker operations are still pending.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "stop" || line.empty()) {
            break;
        }
    }

    server->stop();
    output << "STOPPED connections=" << server->statistics().connections << '\n';
    return std::cin.bad() || !output ? 1 : 0;
}
} // namespace pipeforge
