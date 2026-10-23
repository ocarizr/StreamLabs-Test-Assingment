include(FetchContent)

function(pipeforge_protocol_dependencies)
    set(CMAKE_CXX_STANDARD 20)
    # Match the Abseil release recorded by protobuf 36.2's dependencies.cmake.
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    FetchContent_Declare(absl SYSTEM
        URL https://codeload.github.com/abseil/abseil-cpp/tar.gz/refs/tags/20250512.1
        URL_HASH SHA256=9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(absl)
    set_property(DIRECTORY "${absl_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
    set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "" FORCE)
    FetchContent_Declare(protobuf SYSTEM
        URL https://codeload.github.com/protocolbuffers/protobuf/tar.gz/424e1b0a184baa5a3c75061a1febd5f60a0a3707
        URL_HASH SHA256=626026222f38ad25d37f37ecc2d3bf04698addf92c9c5baa0e91710d92419a09
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    # Only the upstream project requests deprecated CMP0141 behavior on CMake 4.
    # Keep project/developer/compiler warnings enabled outside this function.
    set(CMAKE_WARN_DEPRECATED OFF)
    FetchContent_MakeAvailable(protobuf)
    set_property(DIRECTORY "${protobuf_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    # Upstream adds /MP for every MSVC-ABI compiler; Ninja owns parallelism and
    # clang-cl does not implement that option. Do not weaken project diagnostics.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
        get_property(protobuf_targets DIRECTORY "${protobuf_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS protobuf_targets)
            get_target_property(options ${target} COMPILE_OPTIONS)
            if(options)
                list(REMOVE_ITEM options /MP)
                set_property(TARGET ${target} PROPERTY COMPILE_OPTIONS "${options}")
            endif()
        endforeach()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Protobuf generated layouts intentionally use compiler-supported offsetof
        # on non-standard-layout message classes. Limit the exception to upstream.
        target_compile_options(libprotobuf PRIVATE -Wno-invalid-offsetof)
        target_compile_options(libprotoc PRIVATE -Wno-invalid-offsetof)
    elseif(MSVC)
        # The pinned parser's template lambda switches exhaustively on Utf8Type;
        # MSVC still warns about a nonexistent third enum case. Only this source.
        set_property(SOURCE "${protobuf_SOURCE_DIR}/src/google/protobuf/generated_message_tctable_lite.cc"
            DIRECTORY "${protobuf_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS /wd4715)
        # Further pinned-upstream diagnostics: an intentionally deleted union
        # destructor, a nonreturning fatal logger, and upb C enum flag mixing.
        set_property(SOURCE "${protobuf_SOURCE_DIR}/src/google/protobuf/dynamic_message.cc"
            DIRECTORY "${protobuf_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS /wd4624)
        set_property(SOURCE "${protobuf_SOURCE_DIR}/src/google/protobuf/json/internal/unparser.cc"
            DIRECTORY "${protobuf_SOURCE_DIR}" APPEND PROPERTY COMPILE_OPTIONS /wd4715)
        target_compile_options(libprotoc PRIVATE /wd5287)
    endif()
    foreach(target IN ITEMS libprotobuf utf8_validity utf8_range)
        if(PIPEFORGE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined
                -fno-omit-frame-pointer -fno-sanitize-recover=all)
        endif()
    endforeach()
    if(PIPEFORGE_SANITIZERS)
        # protoc also links this instrumented static runtime during generation.
        target_link_options(libprotobuf INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all)
    endif()
    foreach(notice IN ITEMS "${protobuf_SOURCE_DIR}/LICENSE"
            "${protobuf_SOURCE_DIR}/third_party/utf8_range/LICENSE" "${absl_SOURCE_DIR}/LICENSE")
        file(READ "${notice}" content)
        file(APPEND "${PIPEFORGE_RUNTIME_NOTICES}" "\nProtobuf 36.2 / bundled utf8_range / Abseil 20250512.1\n${content}\n")
    endforeach()
endfunction()

function(pipeforge_runtime_dependencies)
    set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
    set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "" FORCE)
    set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)
    set(SPDLOG_NO_EXCEPTIONS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(spdlog
        SYSTEM
        URL https://codeload.github.com/gabime/spdlog/tar.gz/79524ddd08a4ec981b7fea76afd08ee05f83755d
        URL_HASH SHA256=c6dee844e35a54cc80e3ab76aae2310763e0b49c7397549dfa4d4657d9737138
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        SYSTEM
        URL https://codeload.github.com/nlohmann/json/tar.gz/65ee68451d8eb2b5f3a30b410476ab83deb3289b
        URL_HASH SHA256=13ef31d691947940a08909f8e0772f1d7d68e5da1678ee812a49c4bb0c996b2f
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(spdlog nlohmann_json)

    # These runtime components are linked statically; preserve their notices.
    file(READ "${spdlog_SOURCE_DIR}/LICENSE" spdlog_license)
    file(READ "${spdlog_SOURCE_DIR}/include/spdlog/fmt/bundled/fmt.license.rst" fmt_license)
    file(READ "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT" json_license)
    set(notices "${CMAKE_BINARY_DIR}/generated/THIRD-PARTY-NOTICES.txt")
    file(WRITE "${notices}" "spdlog v1.17.0\n${spdlog_license}\nBundled fmt\n${fmt_license}\nnlohmann/json v3.12.0\n${json_license}")
    set(PIPEFORGE_RUNTIME_NOTICES "${notices}" PARENT_SCOPE)
endfunction()

function(pipeforge_test_dependencies)
    # GoogleTest v1.17.0, immutable upstream revision. No transitive dependencies.
    # FETCHCONTENT_SOURCE_DIR_GOOGLETEST can supply this revision offline.
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        SYSTEM
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
        GIT_PROGRESS FALSE
    )
    FetchContent_MakeAvailable(googletest)
endfunction()
