#include <stddef.h>
#include <stdint.h>
#include "../include/ow_checksum.h"

static uint32_t crc32c_byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0x82F63B78UL;
        } else {
            crc = (crc >> 1);
        }
    }
    return crc;
}

uint32_t ow_crc32c(uint32_t initial, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = ~initial;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32c_byte(crc, p[i]);
    }
    return ~crc;
}

uint64_t ow_fletcher64(const void *data, size_t len) {
    /* Byte-wise little-endian accumulation: alignment-safe and handles
       arbitrary lengths (trailing bytes are treated as a zero-padded word). */
    const uint8_t *p = (const uint8_t *)data;
    uint64_t sum1 = 0;
    uint64_t sum2 = 0;
    size_t i = 0;
    while (len >= 4) {
        uint32_t word = (uint32_t)p[i] |
                        ((uint32_t)p[i + 1] << 8) |
                        ((uint32_t)p[i + 2] << 16) |
                        ((uint32_t)p[i + 3] << 24);
        sum1 = (sum1 + word) % 0xFFFFFFFFULL;
        sum2 = (sum2 + sum1) % 0xFFFFFFFFULL;
        i += 4;
        len -= 4;
    }
    if (len > 0) {
        uint32_t word = 0;
        for (size_t k = 0; k < len; ++k) {
            word |= (uint32_t)p[i + k] << (8 * k);
        }
        sum1 = (sum1 + word) % 0xFFFFFFFFULL;
        sum2 = (sum2 + sum1) % 0xFFFFFFFFULL;
    }
    return (sum2 << 32) | sum1;
}

uint32_t ow_crc32c_struct(const void *data, size_t struct_size, size_t checksum_offset) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < struct_size; ++i) {
        uint8_t b = p[i];
        if (i >= checksum_offset && i < (checksum_offset + sizeof(uint32_t))) {
            b = 0;
        }
        crc = crc32c_byte(crc, b);
    }
    return ~crc;
}
