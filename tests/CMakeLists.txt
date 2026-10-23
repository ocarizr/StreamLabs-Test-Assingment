include(GoogleTest)
if(WIN32)
    add_executable(pipeforge-pipe-peer integration/pipe_peer.cpp)
    target_include_directories(pipeforge-pipe-peer PRIVATE ../src/transport/windows)
    target_link_libraries(pipeforge-pipe-peer PRIVATE pipeforge-transport)
    pipeforge_project_options(pipeforge-pipe-peer)
    add_executable(pipeforge-pipe-tests integration/windows_pipe_tests.cpp integration/windows_native_tests.cpp
        integration/windows_lifecycle_tests.cpp)
    add_dependencies(pipeforge-pipe-tests pipeforge-pipe-peer)
    target_compile_definitions(pipeforge-pipe-tests PRIVATE PIPEFORGE_PEER_PATH="$<TARGET_FILE:pipeforge-pipe-peer>")
    target_include_directories(pipeforge-pipe-tests PRIVATE ../src/transport/windows unit)
    target_link_libraries(pipeforge-pipe-tests PRIVATE pipeforge-pipe-server GTest::gtest_main Advapi32)
    pipeforge_project_options(pipeforge-pipe-tests)
    gtest_discover_tests(pipeforge-pipe-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS integration TIMEOUT 20)
endif()
add_executable(pipeforge-transport-unit-tests unit/transport_completion_tests.cpp)
target_include_directories(pipeforge-transport-unit-tests PRIVATE ../src/transport)
target_link_libraries(pipeforge-transport-unit-tests PRIVATE pipeforge-transport-core GTest::gtest_main)
pipeforge_project_options(pipeforge-transport-unit-tests)
gtest_discover_tests(pipeforge-transport-unit-tests PROPERTIES LABELS unit)
add_library(pipeforge-fuzz-harness STATIC fuzz/protocol_fuzz.cpp)
target_link_libraries(pipeforge-fuzz-harness PUBLIC pipeforge-protocol)
pipeforge_project_options(pipeforge-fuzz-harness)
add_executable(pipeforge-protocol-tests unit/protocol_tests.cpp unit/fuzz_smoke_tests.cpp)
target_link_libraries(pipeforge-protocol-tests PRIVATE pipeforge-fuzz-harness)
target_link_libraries(pipeforge-protocol-tests PRIVATE pipeforge-protocol protobuf::libprotobuf GTest::gtest_main)
target_include_directories(pipeforge-protocol-tests SYSTEM PRIVATE "${protocol_generated}")
add_dependencies(pipeforge-protocol-tests pipeforge-wire)
pipeforge_project_options(pipeforge-protocol-tests)
gtest_discover_tests(pipeforge-protocol-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS unit)
if(PIPEFORGE_BUILD_FUZZER)
    target_link_options(pipeforge-protocol INTERFACE -fsanitize=fuzzer-no-link)
    target_compile_options(pipeforge-protocol PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_options(pipeforge-contracts PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(pipeforge-contracts INTERFACE -fsanitize=fuzzer-no-link)
    target_compile_options(pipeforge-wire PRIVATE -fsanitize=fuzzer-no-link)
    target_compile_options(libprotobuf PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(libprotobuf INTERFACE -fsanitize=fuzzer-no-link)
    target_compile_options(pipeforge-fuzz-harness PRIVATE -fsanitize=fuzzer-no-link)
    add_executable(pipeforge-protocol-fuzzer fuzz/libfuzzer_entry.cpp)
    target_link_libraries(pipeforge-protocol-fuzzer PRIVATE pipeforge-fuzz-harness)
    pipeforge_project_options(pipeforge-protocol-fuzzer)
    target_compile_options(pipeforge-protocol-fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_options(pipeforge-protocol-fuzzer PRIVATE -fsanitize=fuzzer)
endif()
add_executable(pipeforge-cli-tests unit/application_tests.cpp unit/diagnostics_tests.cpp
    unit/diagnostics_behavior_tests.cpp unit/profiling_disabled_tests.cpp)
set_source_files_properties(unit/profiling_disabled_tests.cpp PROPERTIES COMPILE_DEFINITIONS PIPEFORGE_DISABLE_PROFILING)
target_link_libraries(pipeforge-cli-tests PRIVATE pipeforge-cli GTest::gtest_main)
target_link_libraries(pipeforge-cli-tests PRIVATE nlohmann_json::nlohmann_json)
pipeforge_project_options(pipeforge-cli-tests)
gtest_discover_tests(pipeforge-cli-tests DISCOVERY_TIMEOUT 30
    TEST_FILTER "Application.*:ProfilingDisabled.*" PROPERTIES LABELS unit)
gtest_discover_tests(pipeforge-cli-tests DISCOVERY_TIMEOUT 30
    TEST_FILTER "diagnostics_test.*" PROPERTIES LABELS integration)

foreach(role IN ITEMS client server)
    add_test(NAME ${role}-version COMMAND pipeforge-${role} --version)
    set_tests_properties(${role}-version PROPERTIES
        PASS_REGULAR_EXPRESSION "pipeforge-${role} ${PROJECT_VERSION}" TIMEOUT 10 LABELS integration)
    add_test(NAME ${role}-help COMMAND pipeforge-${role} --help)
    set_tests_properties(${role}-help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:" TIMEOUT 10 LABELS integration)
endforeach()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME sanitizer-configuration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/integration/check_sanitizer_configuration.py"
    --cmake "${CMAKE_COMMAND}" --compiler "${CMAKE_CXX_COMPILER}"
    --build "${CMAKE_BINARY_DIR}" --enabled "$<BOOL:${PIPEFORGE_SANITIZERS}>"
    --windows-asan "$<BOOL:${PIPEFORGE_WINDOWS_ASAN}>")
set_tests_properties(sanitizer-configuration PROPERTIES TIMEOUT 120 LABELS integration)
FetchContent_GetProperties(protobuf)
FetchContent_GetProperties(absl)
add_test(NAME protocol-toolchain COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/integration/check_protocol_toolchain.py"
    --protoc "$<TARGET_FILE:protobuf::protoc>" --compiler "${CMAKE_CXX_COMPILER}"
    --include "${protocol_generated}" --include "${protobuf_SOURCE_DIR}/src"
    --include "${protobuf_SOURCE_DIR}/third_party/utf8_range"
    --include "${absl_SOURCE_DIR}")
set_tests_properties(protocol-toolchain PROPERTIES TIMEOUT 150 LABELS integration)
add_test(NAME release-tools COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests/tools" -v)
set_tests_properties(release-tools PROPERTIES TIMEOUT 60 LABELS unit)
add_test(NAME release-provenance COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_SOURCE_DIR}/tests/integration" -v)
set_tests_properties(release-provenance PROPERTIES TIMEOUT 60 LABELS integration)
add_test(NAME diagnostic-cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/integration/check_diagnostic_cli.py"
    --client "$<TARGET_FILE:pipeforge-client>" --server "$<TARGET_FILE:pipeforge-server>")
set_tests_properties(diagnostic-cli PROPERTIES TIMEOUT 60 LABELS integration)

if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME runtime-packages COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/end_to_end/check_packages.py"
        --build "${CMAKE_BINARY_DIR}" --version "${PROJECT_VERSION}" --cmake "${CMAKE_COMMAND}")
    set_tests_properties(runtime-packages PROPERTIES TIMEOUT 60 LABELS end-to-end)
endif()
add_executable(pipeforge-domain-tests unit/domain_tests.cpp unit/domain_failures_tests.cpp unit/services_tests.cpp)
target_link_libraries(pipeforge-domain-tests PRIVATE pipeforge-services pipeforge-protocol GTest::gtest_main)
pipeforge_project_options(pipeforge-domain-tests)
gtest_discover_tests(pipeforge-domain-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS unit)
add_executable(pipeforge-service-integration-tests integration/service_roundtrip_tests.cpp)
target_include_directories(pipeforge-service-integration-tests PRIVATE unit)
target_link_libraries(pipeforge-service-integration-tests PRIVATE
    pipeforge-services pipeforge-protocol GTest::gtest_main nlohmann_json::nlohmann_json)
pipeforge_project_options(pipeforge-service-integration-tests)
gtest_discover_tests(pipeforge-service-integration-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS integration)
