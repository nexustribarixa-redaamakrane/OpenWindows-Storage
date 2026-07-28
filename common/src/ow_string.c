#include <stddef.h>
#include <stdint.h>
#include "../include/ow_string.h"
#include "../include/ow_mem.h"

int ow_sutf8_name_cmp(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
    size_t min_len = (a_len < b_len) ? a_len : b_len;
    int cmp = ow_memcmp(a, b, min_len);
    if (cmp != 0) {
        return cmp;
    }
    if (a_len < b_len) {
        return -1;
    }
    if (a_len > b_len) {
        return 1;
    }
    return 0;
}

size_t ow_sutf8_name_copy(uint8_t *dest, size_t dest_cap, const uint8_t *src, size_t src_len) {
    if (!dest || dest_cap == 0) {
        return 0;
    }
    size_t copy_len = (src_len < dest_cap) ? src_len : (dest_cap - 1);
    if (src && copy_len > 0) {
        ow_memcpy(dest, src, copy_len);
    }
    dest[copy_len] = 0;
    return copy_len;
}
