#include "pipeforge/application.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char *argv[]) {
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    return pipeforge::run_application("pipeforge-server", arguments, std::cout, std::cerr);
}
