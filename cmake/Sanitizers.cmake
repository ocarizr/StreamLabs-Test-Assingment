# Instrument the entire build before creating any targets or dependencies.
# Protobuf 36.2 selects a different message layout under ASan, so instrumenting
# only libprotobuf creates an ABI mismatch with libprotoc and its consumers.
if(PIPEFORGE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined
        -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
endif()
