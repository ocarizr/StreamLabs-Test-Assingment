pipeforge_protocol_dependencies()

add_library(pipeforge-contracts STATIC src/contracts/validation.cpp)
target_include_directories(pipeforge-contracts PUBLIC include)
target_link_libraries(pipeforge-contracts PRIVATE utf8_validity)
pipeforge_project_options(pipeforge-contracts)

set(protocol_schema "${CMAKE_SOURCE_DIR}/proto/pipeforge/v1/protocol.proto")
set(protocol_generated "${CMAKE_BINARY_DIR}/generated/wire")
file(MAKE_DIRECTORY "${protocol_generated}")
add_custom_command(
    OUTPUT "${protocol_generated}/pipeforge/v1/protocol.pb.cc"
        "${protocol_generated}/pipeforge/v1/protocol.pb.h"
    COMMAND $<TARGET_FILE:protobuf::protoc> --cpp_out=${protocol_generated}
        --proto_path=${CMAKE_SOURCE_DIR}/proto "${protocol_schema}"
    DEPENDS protobuf::protoc "${protocol_schema}"
    COMMENT "Generating PipeForge v1 with the pinned protobuf compiler"
    VERBATIM)

# Generated code has its own target so project style/warning policy never leaks
# into upstream output. Linking this object target puts the codec in the archive.
add_library(pipeforge-wire OBJECT "${protocol_generated}/pipeforge/v1/protocol.pb.cc")
target_include_directories(pipeforge-wire SYSTEM PUBLIC "${protocol_generated}")
target_link_libraries(pipeforge-wire PUBLIC protobuf::libprotobuf)
target_compile_features(pipeforge-wire PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(pipeforge-wire PRIVATE -Wno-invalid-offsetof)
endif()

add_library(pipeforge-protocol STATIC src/protocol/codec.cpp src/protocol/framing.cpp)
target_include_directories(pipeforge-protocol PUBLIC include)
target_link_libraries(pipeforge-protocol PUBLIC pipeforge-contracts PRIVATE pipeforge-wire protobuf::libprotobuf utf8_validity)
pipeforge_project_options(pipeforge-protocol)
