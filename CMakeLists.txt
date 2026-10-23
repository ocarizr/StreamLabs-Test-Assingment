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

# Static CRT makes the M1 Windows packages self-contained on supported Windows.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(PIPEFORGE_WARNINGS_AS_ERRORS "Treat maintained project warnings as errors" ON)
option(PIPEFORGE_SANITIZERS "Enable ASan/UBSan (including supported leak detection)" OFF)
option(PIPEFORGE_ENABLE_PROFILING "Compile profiling scope macros into the application" ON)
option(PIPEFORGE_BUILD_FUZZER "Build the Linux Clang libFuzzer decoder harness" OFF)
option(PIPEFORGE_BUILD_BENCHMARKS "Build non-gating protocol measurements" OFF)
if(PIPEFORGE_BUILD_FUZZER AND (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
    NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT PIPEFORGE_SANITIZERS))
    message(FATAL_ERROR "libFuzzer requires Linux Clang with PIPEFORGE_SANITIZERS=ON")
endif()
if(PIPEFORGE_SANITIZERS AND
    (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$"))
    message(FATAL_ERROR "The combined sanitizer configuration requires GCC/Clang on Linux")
endif()

include(CTest)
if(PIPEFORGE_BUILD_FUZZER AND NOT BUILD_TESTING)
    message(FATAL_ERROR "PIPEFORGE_BUILD_FUZZER requires BUILD_TESTING=ON")
endif()
include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)
pipeforge_runtime_dependencies()
include(cmake/Protocol.cmake)
configure_file(cmake/version.hpp.in generated/pipeforge/version.hpp @ONLY)

add_library(pipeforge-diagnostics STATIC src/diagnostics/diagnostics.cpp)
target_include_directories(pipeforge-diagnostics PUBLIC include)
target_link_libraries(pipeforge-diagnostics PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json)
target_compile_definitions(pipeforge-diagnostics PUBLIC
    PIPEFORGE_ENABLE_PROFILING=$<BOOL:${PIPEFORGE_ENABLE_PROFILING}>)
pipeforge_project_options(pipeforge-diagnostics)
if(WIN32)
    target_compile_definitions(pipeforge-diagnostics PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_library(pipeforge-cli STATIC src/cli/application.cpp)
target_link_libraries(pipeforge-cli PUBLIC pipeforge-diagnostics pipeforge-protocol)
target_include_directories(pipeforge-cli PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")
pipeforge_project_options(pipeforge-cli)

foreach(role IN ITEMS client server)
    add_executable(pipeforge-${role} apps/${role}/main.cpp)
    target_link_libraries(pipeforge-${role} PRIVATE pipeforge-cli)
    pipeforge_project_options(pipeforge-${role})
    install(TARGETS pipeforge-${role} RUNTIME DESTINATION . COMPONENT ${role})
    install(FILES "${PIPEFORGE_RUNTIME_NOTICES}" DESTINATION . COMPONENT ${role})
endforeach()

if(BUILD_TESTING)
    pipeforge_test_dependencies()
    add_subdirectory(tests)
endif()

if(PIPEFORGE_BUILD_BENCHMARKS)
    add_executable(pipeforge-protocol-benchmark benchmarks/protocol_benchmark.cpp)
    target_link_libraries(pipeforge-protocol-benchmark PRIVATE pipeforge-protocol nlohmann_json::nlohmann_json)
    pipeforge_project_options(pipeforge-protocol-benchmark)
endif()

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_format.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking maintained C++ files with clang-format 19.1.5")
    find_program(PIPEFORGE_CLANG_TIDY NAMES clang-tidy clang-tidy-19)
    if(PIPEFORGE_CLANG_TIDY)
        add_custom_target(static-analysis
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_tidy.py"
                --build "${CMAKE_BINARY_DIR}" --clang-tidy "${PIPEFORGE_CLANG_TIDY}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Checking maintained C++ code with clang-tidy")
    endif()
endif()
