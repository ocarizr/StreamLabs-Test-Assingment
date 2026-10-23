include(FetchContent)

# GoogleTest v1.17.0, immutable upstream revision. No transitive dependencies.
# FETCHCONTENT_SOURCE_DIR_GOOGLETEST can point to this revision for offline builds.
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
    GIT_PROGRESS FALSE
)
FetchContent_MakeAvailable(googletest)
