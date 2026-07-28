#include <stddef.h>
#include <stdint.h>
#include "../include/ow_mem.h"

void *ow_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void *ow_memset(void *dest, int val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    uint8_t v = (uint8_t)val;
    for (size_t i = 0; i < n; ++i) {
        d[i] = v;
    }
    return dest;
}

int ow_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}
