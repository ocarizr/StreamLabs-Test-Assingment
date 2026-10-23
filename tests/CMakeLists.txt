include(GoogleTest)
add_executable(pipeforge-cli-tests unit/application_tests.cpp)
target_link_libraries(pipeforge-cli-tests PRIVATE pipeforge-cli GTest::gtest_main)
pipeforge_project_options(pipeforge-cli-tests)
gtest_discover_tests(pipeforge-cli-tests DISCOVERY_TIMEOUT 30)

foreach(role IN ITEMS client server)
    add_test(NAME ${role}-version COMMAND pipeforge-${role} --version)
    set_tests_properties(${role}-version PROPERTIES
        PASS_REGULAR_EXPRESSION "pipeforge-${role} ${PROJECT_VERSION}" TIMEOUT 10)
    add_test(NAME ${role}-help COMMAND pipeforge-${role} --help)
    set_tests_properties(${role}-help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:" TIMEOUT 10)
endforeach()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME release-tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests/tools" -v)
set_tests_properties(release-tools PROPERTIES TIMEOUT 60)
