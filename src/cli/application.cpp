#include "pipeforge/application.hpp"

#include "pipeforge/diagnostics.hpp"
#include "pipeforge/version.hpp"

#include <ostream>

namespace pipeforge {
namespace {
int print_command(std::string_view name,
                  std::string_view command,
                  std::ostream& output,
                  [[maybe_unused]] const diagnostic_session& session)
{
    PIPEFORGE_PROFILE_FUNCTION(session);
    if (command == "--version") {
        output << name << ' ' << version << '\n';
        return 0;
    }

    output << "Usage: " << name << " [--log-file PATH] [--trace-file PATH] [--help | --version]\n"
           << "Profiling: " << (profiling_enabled ? "enabled" : "disabled") << '\n'
           << "IPC is not implemented in this build infrastructure milestone.\n";
    return 0;
}
} // namespace

int run_application(std::string_view name,
                    std::span<const std::string_view> arguments,
                    std::ostream& output,
                    std::ostream& errors)
{
    if (arguments.empty()) {
        errors << "IPC is not implemented in this build infrastructure milestone.\n";
        return 2;
    }

    diagnostic_options options;
    std::string_view command;
    auto remaining = arguments;
    while (!remaining.empty()) {
        const auto argument = remaining.front();
        remaining = remaining.subspan(1);
        if (argument == "--log-file" || argument == "--trace-file") {
            auto& path = argument == "--log-file" ? options.log_file : options.trace_file;
            if (!path.empty() || remaining.empty() || remaining.front().empty() ||
                remaining.front().starts_with("--")) {
                errors << "Diagnostic options require one unique output path.\n";
                return 2;
            }

            path = remaining.front();
            remaining = remaining.subspan(1);
        }
        else if ((argument == "--help" || argument == "--version") && command.empty()) {
            command = argument;
        }
        else {
            errors << "Unsupported arguments. Use --help for available options.\n";
            return 2;
        }
    }

    if (command.empty()) {
        errors << "A --help or --version command is required; IPC is not implemented.\n";
        return 2;
    }

    if (!profiling_enabled && !options.trace_file.empty()) {
        errors << "Profiling is disabled in this build.\n";
        return 2;
    }

    try {
        diagnostic_session session(name, options);
        int result = 0;
        {
            PIPEFORGE_PROFILE_SCOPE(session, "cli.command");
            session.log(log_level::info, "Command started");
            result = print_command(name, command, output, session);
            session.log(log_level::info, "Command completed");
        }

        if (!session.finish()) {
            errors << "Diagnostics I/O failed while writing output.\n";
            return 1;
        }

        return result;
    }
    catch (const diagnostic_io_error& error) {
        errors << "Diagnostics I/O error: " << error.what() << '\n';
        return 1;
    }
}
} // namespace pipeforge
