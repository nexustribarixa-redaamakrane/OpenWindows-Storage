cmake_minimum_required(VERSION 3.10)

add_library(ow_common OBJECT
    src/ow_mem.c
    src/ow_string.c
    src/ow_checksum.c
    src/ow_htl.c
    src/ow_crypto.c
)

target_include_directories(ow_common PUBLIC include)

# Freestanding C99 compliance flags for the library itself
target_compile_options(ow_common PRIVATE -ffreestanding -fno-stack-protector -Wall -Wextra)
