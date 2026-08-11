cmake_minimum_required(VERSION 3.10)

add_library(usfs_static STATIC
    src/usfs_superblock.c
    src/usfs_entry.c
    src/usfs_bitmap.c
    src/usfs_file.c
    src/usfs_blockio.c
    src/usfs_sync.c
    src/usfs_format.c
    $<TARGET_OBJECTS:ow_common>
)

set_target_properties(usfs_static PROPERTIES
    OUTPUT_NAME usfs
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(usfs_static PUBLIC include ../common/include)

# Freestanding C99 compliance flags for the library itself
target_compile_options(usfs_static PRIVATE -ffreestanding -fno-stack-protector -Wall -Wextra)

add_custom_command(TARGET usfs_static POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:usfs_static>
        ${CMAKE_BINARY_DIR}/libusfs.a
)

# Host Test Executable
add_executable(test_usfs tests/test_usfs.c)
target_link_libraries(test_usfs PRIVATE usfs_static)
add_test(NAME test_usfs COMMAND test_usfs)
