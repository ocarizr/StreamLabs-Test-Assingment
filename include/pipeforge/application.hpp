#ifndef PIPEFORGE_APPLICATION_HPP
#define PIPEFORGE_APPLICATION_HPP

#include "pipeforge/transport.hpp"

#include <iosfwd>
#include <span>
#include <string_view>

namespace pipeforge {
class diagnostic_session;

enum class application_command : std::uint8_t {
    help,
    version,
    serve,
    demo_sync,
    demo_async,
    demo_mixed
};

/** Validated runtime arguments; diagnostic paths are handled by run_application. */
struct invocation {
    application_command command = application_command::help;
    std::wstring endpoint{transport::default_endpoint};
    std::chrono::milliseconds timeout = transport::default_timeout;
    std::size_t sessions = transport::max_sessions;
};

using command_handler = int (*)(const invocation&,
                                std::ostream&,
                                std::ostream&,
                                diagnostic_session&);

/** Windows application composition, supplied only by the corresponding executable. */
int run_client_command(const invocation& options,
                       std::ostream& output,
                       std::ostream& errors,
                       diagnostic_session& diagnostics);
int run_server_command(const invocation& options,
                       std::ostream& output,
                       std::ostream& errors,
                       diagnostic_session& diagnostics);
/**
 * Validate CLI arguments, own opt-in diagnostics and dispatch one command.
 * @param name Executable display name, supplied by its entry point.
 * @param arguments Arguments excluding argv[0]; borrowed for this call only.
 * @param output Destination for help, version and command results.
 * @param errors Destination for usage errors and unavailable-operation diagnostics.
 * @param handler Platform composition for runtime commands; null reports unsupported IPC.
 * @return 0 on success, 1 on runtime/I/O failure, 2 for invalid/unsupported invocation.
 * No state is retained. Stream exceptions propagate if enabled by the caller.
 */
int run_application(std::string_view name,
                    std::span<const std::string_view> arguments,
                    std::ostream& output,
                    std::ostream& errors,
                    command_handler handler = nullptr);
} // namespace pipeforge

#endif // PIPEFORGE_APPLICATION_HPP
