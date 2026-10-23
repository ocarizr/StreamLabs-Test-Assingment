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

include(CTest)
include(cmake/ProjectOptions.cmake)
configure_file(cmake/version.hpp.in generated/pipeforge/version.hpp @ONLY)

add_library(pipeforge-cli STATIC src/cli/application.cpp)
target_include_directories(pipeforge-cli PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")
pipeforge_project_options(pipeforge-cli)

foreach(role IN ITEMS client server)
    add_executable(pipeforge-${role} apps/${role}/main.cpp)
    target_link_libraries(pipeforge-${role} PRIVATE pipeforge-cli)
    pipeforge_project_options(pipeforge-${role})
    install(TARGETS pipeforge-${role} RUNTIME DESTINATION . COMPONENT ${role})
endforeach()

if(BUILD_TESTING)
    include(cmake/Dependencies.cmake)
    add_subdirectory(tests)
endif()

find_package(Python3 3.10 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_format.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking maintained C++ files with clang-format 19.1.5")
endif()
