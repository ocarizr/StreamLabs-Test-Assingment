include(FetchContent)

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
