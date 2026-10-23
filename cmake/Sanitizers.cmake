# Instrument the entire build before creating any targets or dependencies.
# Protobuf 36.2 selects a different message layout under ASan, so instrumenting
# only libprotobuf creates an ABI mismatch with libprotoc and its consumers.
if(PIPEFORGE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined
        -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
elseif(PIPEFORGE_WINDOWS_ASAN)
    # RelWithDebInfo avoids incompatible /RTC and /ZI. Apply to dependencies
    # too: Protobuf chooses its message layout using the ASan preprocessor macro.
    add_compile_options(/fsanitize=address /Zi /clang:-fno-omit-frame-pointer)
    add_link_options(/INCREMENTAL:NO)
    # CMake invokes lld-link directly, bypassing clang-cl's sanitizer link
    # driver. Mirror its static-CRT runtime selection explicitly (LLVM 22+).
    if(NOT DEFINED PIPEFORGE_ASAN_RESOURCE_DIR)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
            OUTPUT_VARIABLE PIPEFORGE_ASAN_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
    file(TO_CMAKE_PATH "${PIPEFORGE_ASAN_RESOURCE_DIR}/lib/windows" asan_runtime)
    add_link_options(/INFERASANLIBS:NO)
    if(NOT EXISTS "${asan_runtime}/clang_rt.asan_dynamic-x86_64.lib" OR
        NOT EXISTS "${asan_runtime}/clang_rt.asan_static_runtime_thunk-x86_64.lib")
        message(FATAL_ERROR "The selected clang-cl lacks its matching x64 AddressSanitizer runtime")
    endif()
    add_link_options("${asan_runtime}/clang_rt.asan_dynamic-x86_64.lib"
        "/WHOLEARCHIVE:${asan_runtime}/clang_rt.asan_static_runtime_thunk-x86_64.lib")
endif()
