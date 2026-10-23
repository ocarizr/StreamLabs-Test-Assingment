include(GoogleTest)
add_executable(pipeforge-cli-tests unit/application_tests.cpp)
target_link_libraries(pipeforge-cli-tests PRIVATE pipeforge-cli GTest::gtest_main)
pipeforge_project_options(pipeforge-cli-tests)
gtest_discover_tests(pipeforge-cli-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS unit)

foreach(role IN ITEMS client server)
    add_test(NAME ${role}-version COMMAND pipeforge-${role} --version)
    set_tests_properties(${role}-version PROPERTIES
        PASS_REGULAR_EXPRESSION "pipeforge-${role} ${PROJECT_VERSION}" TIMEOUT 10 LABELS integration)
    add_test(NAME ${role}-help COMMAND pipeforge-${role} --help)
    set_tests_properties(${role}-help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:" TIMEOUT 10 LABELS integration)
endforeach()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME release-tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests/tools" -v)
set_tests_properties(release-tools PROPERTIES TIMEOUT 60 LABELS unit)
add_test(NAME release-provenance COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests/integration" -v)
set_tests_properties(release-provenance PROPERTIES TIMEOUT 60 LABELS integration)

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME runtime-packages COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/end_to_end/check_packages.py"
        --build "${CMAKE_BINARY_DIR}" --version "${PROJECT_VERSION}" --cmake "${CMAKE_COMMAND}")
    set_tests_properties(runtime-packages PROPERTIES TIMEOUT 60 LABELS end-to-end)
endif()
