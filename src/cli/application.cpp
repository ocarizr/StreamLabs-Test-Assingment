#include "pipeforge/application.hpp"

#include "pipeforge/version.hpp"

#include <ostream>

namespace pipeforge {
int run_application(std::string_view name,
                    std::span<const std::string_view> arguments,
                    std::ostream& output,
                    std::ostream& errors)
{
    if (arguments.size() == 1 && arguments.front() == "--version") {
        output << name << ' ' << version << '\n';
        return 0;
    }
    if (arguments.size() == 1 && arguments.front() == "--help") {
        output << "Usage: " << name << " [--help | --version]\n"
               << "IPC is not implemented in this build infrastructure milestone.\n";
        return 0;
    }
    if (arguments.empty()) {
        errors << "IPC is not implemented in this build infrastructure milestone.\n";
    }
    else {
        errors << "Unsupported arguments. Use --help for available options.\n";
    }
    return 2;
}
} // namespace pipeforge
