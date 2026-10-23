#ifndef PIPEFORGE_APPLICATION_HPP
#define PIPEFORGE_APPLICATION_HPP

#include <iosfwd>
#include <span>
#include <string_view>

namespace pipeforge {
/**
 * Runs the initial executable's command-line interface without starting IPC.
 * @param name Executable display name, supplied by its entry point.
 * @param arguments Arguments excluding argv[0]; borrowed for this call only.
 * @param output Destination for help and version information.
 * @param errors Destination for usage errors and unavailable-operation diagnostics.
 * @return 0 for help/version, 2 for unsupported invocation.
 * No state is retained. Stream exceptions propagate if enabled by the caller.
 */
int run_application(std::string_view name,
                    std::span<const std::string_view> arguments,
                    std::ostream& output,
                    std::ostream& errors);
} // namespace pipeforge

#endif // PIPEFORGE_APPLICATION_HPP
