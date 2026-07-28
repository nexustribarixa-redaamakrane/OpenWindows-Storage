cmake_minimum_required(VERSION 3.10)

add_library(usfs_static STATIC
    src/usfs_superblock.c
    src/usfs_entry.c
    src/usfs_blockio.c
    src/usfs_sync.c
    src/usfs_format.c
    $<TARGET_OBJECTS:ow_common>
)

target_include_directories(usfs_static PUBLIC include ../common/include)

add_custom_target(libusfs.a ALL DEPENDS usfs_static)

# Host Test Executable
add_executable(test_usfs tests/test_usfs.c)
target_link_libraries(test_usfs PRIVATE usfs_static)
add_test(NAME test_usfs COMMAND test_usfs)
