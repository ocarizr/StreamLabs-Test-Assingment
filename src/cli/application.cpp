#include "pipeforge/application.hpp"

#include "pipeforge/diagnostics.hpp"
#include "pipeforge/protocol.hpp"
#include "pipeforge/version.hpp"

#include <charconv>
#include <map>
#include <memory>
#include <ostream>
#include <set>

namespace pipeforge {
namespace {
int print_command(std::string_view name,
                  application_command command,
                  std::ostream& output,
                  [[maybe_unused]] const diagnostic_session& session)
{
    PIPEFORGE_PROFILE_FUNCTION(session);
    if (command == application_command::version) {
        output << name << ' ' << version << '\n';
        return 0;
    }

    output << "Usage: " << name << " [--log-file PATH] [--trace-file PATH] COMMAND [OPTIONS]\n"
           << "Commands: --help, --version, "
           << (name == "pipeforge-server" ? "--serve" : "--demo-sync, --demo-async, --demo-mixed")
           << '\n'
           << "Options: --endpoint LOCAL_PIPE --timeout-ms 10..60000; server: --sessions 1..4\n"
           << "Server: type stop or close stdin for graceful shutdown.\n"
           << "Profiling: " << (profiling_enabled ? "enabled" : "disabled") << '\n'
           << "Protocol: " << protocol::supported_version() << " (Protobuf)\n"
           << "Runtime IPC commands require Windows x64.\n";
    return 0;
}

std::optional<invocation> parse(std::string_view name,
                                std::span<const std::string_view> arguments,
                                diagnostic_options& diagnostics,
                                std::ostream& errors)
{
    invocation options;
    bool has_command = false;
    std::set<std::string_view> supplied;
    const std::map<std::string_view, application_command> commands{
        {"--help", application_command::help},
        {"--version", application_command::version},
        {"--serve", application_command::serve},
        {"--demo-sync", application_command::demo_sync},
        {"--demo-async", application_command::demo_async},
        {"--demo-mixed", application_command::demo_mixed}};
    auto remaining = arguments;
    while (!remaining.empty()) {
        const auto argument = remaining.front();
        remaining = remaining.subspan(1);
        const auto command = commands.find(argument);
        if (command != commands.end()) {
            if (has_command) {
                errors << "Exactly one command is required.\n";
                return std::nullopt;
            }

            has_command = true;
            options.command = command->second;
            continue;
        }

        if (argument != "--log-file" && argument != "--trace-file" && argument != "--endpoint" &&
            argument != "--timeout-ms" && argument != "--sessions") {
            errors << "Unsupported arguments. Use --help for available options.\n";
            return std::nullopt;
        }

        if (!supplied.insert(argument).second || remaining.empty() || remaining.front().empty() ||
            remaining.front().starts_with("--")) {
            errors << "Options require one unique value.\n";
            return std::nullopt;
        }

        const auto value = remaining.front();
        remaining = remaining.subspan(1);
        if (argument == "--log-file" || argument == "--trace-file") {
            (argument == "--log-file" ? diagnostics.log_file : diagnostics.trace_file) = value;
        }
        else if (argument == "--endpoint") {
            options.endpoint.assign(value.begin(), value.end());
            if (!transport::valid_endpoint(options.endpoint)) {
                errors << "Endpoint must be a bounded local named-pipe name.\n";
                return std::nullopt;
            }
        }
        else {
            std::uint32_t number = 0;
            const auto* end = std::to_address(value.end());
            const auto converted = std::from_chars(std::to_address(value.begin()), end, number);
            if (converted.ec != std::errc{} || converted.ptr != end) {
                errors << "Numeric options require unsigned decimal integers.\n";
                return std::nullopt;
            }

            if (argument == "--sessions") {
                if (number == 0 || number > transport::max_sessions) {
                    errors << "Session count is outside the supported bounds.\n";
                    return std::nullopt;
                }

                options.sessions = number;
            }
            else {
                options.timeout = std::chrono::milliseconds{number};
                if (options.timeout < transport::minimum_timeout ||
                    options.timeout > transport::maximum_timeout) {
                    errors << "Timeout is outside the supported bounds.\n";
                    return std::nullopt;
                }
            }
        }
    }

    if (!has_command) {
        errors << "A command is required. Use --help for available options.\n";
        return std::nullopt;
    }

    const auto runtime = options.command != application_command::help &&
                         options.command != application_command::version;
    if ((!runtime && (supplied.contains("--endpoint") || supplied.contains("--timeout-ms") ||
                      supplied.contains("--sessions"))) ||
        (runtime &&
         ((options.command == application_command::serve) != (name == "pipeforge-server"))) ||
        (supplied.contains("--sessions") && options.command != application_command::serve)) {
        errors << "Command/options do not apply to this executable.\n";
        return std::nullopt;
    }

    return options;
}
} // namespace

int run_application(std::string_view name,
                    std::span<const std::string_view> arguments,
                    std::ostream& output,
                    std::ostream& errors,
                    command_handler handler)
{
    diagnostic_options options;
    const auto parsed = parse(name, arguments, options, errors);
    if (!parsed) {
        return 2;
    }

    const auto runtime = parsed->command != application_command::help &&
                         parsed->command != application_command::version;
    if (runtime && !handler) {
        errors << "Runtime IPC commands require Windows x64.\n";
        return 2;
    }

    if constexpr (!profiling_enabled) {
        if (!options.trace_file.empty()) {
            errors << "Profiling is disabled in this build.\n";
            return 2;
        }
    }

    try {
        diagnostic_session session(name, options);
        int result = 0;
        {
            PIPEFORGE_PROFILE_SCOPE(session, "cli.command");
            session.log(log_level::info, "Command started");
            result = runtime ? handler(*parsed, output, errors, session)
                             : print_command(name, parsed->command, output, session);
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
