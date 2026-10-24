cmake_minimum_required(VERSION 3.25)

# Documentation can be generated without compiling or downloading C++
# dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PipeForgeDocumentation LANGUAGES NONE)
endif()

find_package(Doxygen 1.9.8 REQUIRED)
get_filename_component(PIPEFORGE_DOCUMENTATION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PIPEFORGE_API_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/api")
configure_file(Doxyfile.in "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile" @ONLY)

add_custom_target(
    docs
    COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    WORKING_DIRECTORY "${PIPEFORGE_DOCUMENTATION_ROOT}"
    COMMENT "Generating public API documentation with warnings treated as errors"
    VERBATIM
)
