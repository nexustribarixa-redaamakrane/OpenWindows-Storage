cmake_minimum_required(VERSION 3.10)

add_library(owfs_static STATIC
    src/owfs_superblock.c
    src/owfs_inode.c
    src/owfs_catalog.c
    src/owfs_bitmap.c
    src/owfs_blockmap.c
    src/owfs_file.c
    src/owfs_blockio.c
    src/owfs_sync.c
    src/owfs_format.c
    $<TARGET_OBJECTS:ow_common>
)

set_target_properties(owfs_static PROPERTIES
    OUTPUT_NAME owfs
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(owfs_static PUBLIC include ../common/include)

# Freestanding C99 compliance flags for the library itself
target_compile_options(owfs_static PRIVATE -std=c99 -nostdlib -ffreestanding -fno-stack-protector -Wall -Wextra)

add_custom_command(TARGET owfs_static POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:owfs_static>
        ${CMAKE_BINARY_DIR}/libowfs.a
)

# Host Test Executable
add_executable(test_owfs tests/test_owfs.c)
target_link_libraries(test_owfs PRIVATE owfs_static)
add_test(NAME test_owfs COMMAND test_owfs)
