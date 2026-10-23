cmake_minimum_required(VERSION 3.25)

# Release automation supplies the normalized version from the pushed tag.
set(PIPEFORGE_VERSION "0.1.0" CACHE STRING "PipeForge binary version (major.minor.patch)")

if(NOT PIPEFORGE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "PIPEFORGE_VERSION must be major.minor.patch")
endif()

project(PipeForge VERSION ${PIPEFORGE_VERSION} LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build, for example cmake --preset windows-debug")
endif()

# Build options and toolchain compatibility.
# Static CRT makes the M1 Windows packages self-contained on supported Windows.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(PIPEFORGE_WARNINGS_AS_ERRORS "Treat maintained project warnings as errors" ON)
option(PIPEFORGE_SANITIZERS "Enable Linux Clang ASan/UBSan (including leak detection)" OFF)
option(PIPEFORGE_WINDOWS_ASAN "Enable whole-build Windows clang-cl AddressSanitizer" OFF)
option(PIPEFORGE_ENABLE_PROFILING "Compile profiling scope macros into the application" ON)
option(PIPEFORGE_BUILD_FUZZER "Build the Linux Clang libFuzzer decoder harness" OFF)
option(PIPEFORGE_BUILD_BENCHMARKS "Build non-gating protocol measurements" OFF)

if(
    PIPEFORGE_BUILD_FUZZER
    AND (
        NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
        OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        OR NOT PIPEFORGE_SANITIZERS
    )
)
    message(FATAL_ERROR "libFuzzer requires Linux Clang with PIPEFORGE_SANITIZERS=ON")
endif()

if(
    PIPEFORGE_SANITIZERS
    AND (
        NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
        OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    )
)
    message(
        FATAL_ERROR
            "The combined sanitizer configuration requires Linux Clang; "
            "GCC/UBSan cannot compile the pinned Abseil constexpr hash policy "
            "(https://github.com/abseil/abseil-cpp/issues/1634)"
    )
endif()

if(
    PIPEFORGE_WINDOWS_ASAN
    AND (
        NOT WIN32
        OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        OR NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"
        OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 22
        OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
        OR NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
        OR PIPEFORGE_SANITIZERS
    )
)
    message(
        FATAL_ERROR
            "Windows ASan requires x64 clang-cl 22+ RelWithDebInfo, without Linux sanitizers"
    )
endif()

# Apply instrumentation before creating dependencies or project targets.
include(cmake/Sanitizers.cmake)
include(CTest)

if(PIPEFORGE_BUILD_FUZZER AND NOT BUILD_TESTING)
    message(FATAL_ERROR "PIPEFORGE_BUILD_FUZZER requires BUILD_TESTING=ON")
endif()

# Load shared build policies and pinned runtime dependencies.
include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)
pipeforge_runtime_dependencies()
include(cmake/Protocol.cmake)

# Portable completion policy and domain libraries.
add_library(pipeforge-transport-core STATIC src/transport/completion.cpp)
target_include_directories(pipeforge-transport-core PUBLIC include PRIVATE src/transport)
target_link_libraries(pipeforge-transport-core PUBLIC pipeforge-protocol)
pipeforge_project_options(pipeforge-transport-core)

add_library(
    pipeforge-domain
    STATIC
    src/domain/domain.cpp
    src/domain/builtins.cpp
)
target_include_directories(pipeforge-domain PUBLIC include)
target_link_libraries(pipeforge-domain PUBLIC pipeforge-contracts)
pipeforge_project_options(pipeforge-domain)

# Generate the version header consumed by both CLI applications.
configure_file(cmake/version.hpp.in generated/pipeforge/version.hpp @ONLY)

# Shared diagnostics and server-side request dispatch.
add_library(pipeforge-diagnostics STATIC src/diagnostics/diagnostics.cpp)
target_include_directories(pipeforge-diagnostics PUBLIC include)
target_link_libraries(pipeforge-diagnostics PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json)
target_compile_definitions(
    pipeforge-diagnostics PUBLIC PIPEFORGE_ENABLE_PROFILING=$<BOOL:${PIPEFORGE_ENABLE_PROFILING}>
)
pipeforge_project_options(pipeforge-diagnostics)

if(WIN32)
    target_compile_definitions(pipeforge-diagnostics PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_library(pipeforge-services STATIC src/server/dispatcher.cpp)
target_link_libraries(pipeforge-services PUBLIC pipeforge-domain pipeforge-diagnostics)
pipeforge_project_options(pipeforge-services)

# Windows-only stream adapters and the bounded server host.
if(WIN32)
    add_library(
        pipeforge-transport
        STATIC
        src/transport/windows/native.cpp
        src/transport/windows/stream.cpp
    )
    target_link_libraries(pipeforge-transport PUBLIC pipeforge-transport-core PRIVATE Advapi32)
    target_compile_definitions(pipeforge-transport PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    pipeforge_project_options(pipeforge-transport)

    add_library(pipeforge-pipe-server STATIC src/transport/windows/server.cpp)
    target_link_libraries(pipeforge-pipe-server PUBLIC pipeforge-transport pipeforge-services)
    pipeforge_project_options(pipeforge-pipe-server)
endif()

# Common CLI support for the separate client and server applications.
add_library(pipeforge-cli STATIC src/cli/application.cpp)
target_link_libraries(pipeforge-cli PUBLIC pipeforge-diagnostics pipeforge-protocol)
target_include_directories(pipeforge-cli PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")
pipeforge_project_options(pipeforge-cli)

# Install runtime components for the client, server and combined release ZIPs.
foreach(role IN ITEMS client server)
    add_executable(pipeforge-${role} apps/${role}/main.cpp)
    target_link_libraries(pipeforge-${role} PRIVATE pipeforge-cli)
    pipeforge_project_options(pipeforge-${role})
    install(TARGETS pipeforge-${role} RUNTIME DESTINATION . COMPONENT ${role})
    install(FILES "${PIPEFORGE_RUNTIME_NOTICES}" DESTINATION . COMPONENT ${role})
endforeach()

# Only the server links domain services and the Windows host.
target_link_libraries(pipeforge-server PRIVATE pipeforge-services)

if(WIN32)
    target_link_libraries(pipeforge-server PRIVATE pipeforge-pipe-server)
endif()

# Optional test suites and protocol measurements.
if(BUILD_TESTING)
    pipeforge_test_dependencies()
    add_subdirectory(tests)
endif()

if(PIPEFORGE_BUILD_BENCHMARKS)
    add_executable(pipeforge-protocol-benchmark benchmarks/protocol_benchmark.cpp)
    target_link_libraries(
        pipeforge-protocol-benchmark PRIVATE pipeforge-protocol nlohmann_json::nlohmann_json
    )
    pipeforge_project_options(pipeforge-protocol-benchmark)
endif()

# Developer quality targets are available when their tools are installed.
find_package(Python3 3.10 COMPONENTS Interpreter QUIET)

if(Python3_FOUND)
    add_custom_target(
        format-check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_format.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking maintained C++ files with clang-format 19.1.5"
    )
    find_program(PIPEFORGE_CLANG_TIDY NAMES clang-tidy clang-tidy-19)

    if(PIPEFORGE_CLANG_TIDY)
        add_custom_target(
            static-analysis
            COMMAND
                "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_tidy.py"
                --build "${CMAKE_BINARY_DIR}"
                --clang-tidy "${PIPEFORGE_CLANG_TIDY}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Checking maintained C++ code with clang-tidy"
        )
    endif()
endif()
