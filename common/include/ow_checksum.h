#ifndef OW_CHECKSUM_H
#define OW_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

uint32_t ow_crc32c(uint32_t initial, const void *data, size_t len);
uint64_t ow_fletcher64(const void *data, size_t len);
uint32_t ow_crc32c_struct(const void *data, size_t struct_size, size_t checksum_offset);

#endif /* OW_CHECKSUM_H */
