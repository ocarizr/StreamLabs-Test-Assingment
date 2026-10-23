#include "pipeforge/application.hpp"

#include <cstddef>
#include <iostream>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

// Fatal allocation failures are not recoverable I/O errors at this entry point.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* argv[])
{
    const std::span raw_arguments{argv, static_cast<std::size_t>(argc)};
    std::vector<std::string_view> arguments;
    for (const auto* argument : raw_arguments | std::views::drop(1)) {
        arguments.emplace_back(argument);
    }

    return pipeforge::run_application("pipeforge-server", arguments, std::cout, std::cerr);
}
