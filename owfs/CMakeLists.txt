cmake_minimum_required(VERSION 3.10)

add_library(owfs_static STATIC
    src/owfs_superblock.c
    src/owfs_inode.c
    src/owfs_catalog.c
    src/owfs_bitmap.c
    src/owfs_blockio.c
    src/owfs_sync.c
    src/owfs_format.c
    $<TARGET_OBJECTS:ow_common>
)

target_include_directories(owfs_static PUBLIC include ../common/include)

add_custom_target(libowfs.a ALL DEPENDS owfs_static)

# Host Test Executable
add_executable(test_owfs tests/test_owfs.c)
target_link_libraries(test_owfs PRIVATE owfs_static)
add_test(NAME test_owfs COMMAND test_owfs)
