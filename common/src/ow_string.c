#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../include/ow_string.h"
#include "../include/ow_mem.h"

/* Decode one SUTF-8 sequence (SuperUnicode 31-bit transport).
 * Returns the number of bytes consumed (>0), 0 on end-of-buffer, or -1 on a
 * malformed / non-canonical / reserved sequence. Rules mirror the canonical
 * SuperUnicode codec:
 *   1-byte  0x00000000 .. 0x0000007F
 *   2-byte  0x00000080 .. 0x000007FF
 *   3-byte  0x00000800 .. 0x0000FFFF   (surrogates rejected)
 *   4-byte  0x00010000 .. 0x0010FFFF
 *   5-byte  0x00110000 .. 0x03FFFFFF
 *   6-byte  0x04000000 .. 0x7FFFFFFF
 */
static int sutf8_decode_one(const uint8_t *s, size_t len, uint32_t *out_cp) {
    if (!s || len == 0) {
        return 0;
    }
    uint8_t u0 = s[0];

    if ((u0 & 0x80U) == 0x00U) {
        *out_cp = (uint32_t)u0;
        return 1;
    }
    if ((u0 & 0xE0U) == 0xC0U) {
        if (len < 2) return -1;
        if ((s[1] & 0xC0U) != 0x80U) return -1;
        uint32_t cp = ((uint32_t)(u0 & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
        if (cp < 0x80U) return -1; /* overlong */
        *out_cp = cp;
        return 2;
    }
    if ((u0 & 0xF0U) == 0xE0U) {
        if (len < 3) return -1;
        if ((s[1] & 0xC0U) != 0x80U || (s[2] & 0xC0U) != 0x80U) return -1;
        uint32_t cp = ((uint32_t)(u0 & 0x0FU) << 12) |
                      ((uint32_t)(s[1] & 0x3FU) << 6) |
                      (uint32_t)(s[2] & 0x3FU);
        if (cp < 0x800U) return -1;              /* overlong */
        if (cp >= 0xD800U && cp <= 0xDFFFU) return -1; /* surrogate */
        *out_cp = cp;
        return 3;
    }
    if ((u0 & 0xF8U) == 0xF0U) {
        if (len < 4) return -1;
        if ((s[1] & 0xC0U) != 0x80U || (s[2] & 0xC0U) != 0x80U ||
            (s[3] & 0xC0U) != 0x80U) return -1;
        uint32_t cp = ((uint32_t)(u0 & 0x07U) << 18) |
                      ((uint32_t)(s[1] & 0x3FU) << 12) |
                      ((uint32_t)(s[2] & 0x3FU) << 6) |
                      (uint32_t)(s[3] & 0x3FU);
        if (cp < 0x10000U || cp > 0x0010FFFFU) return -1; /* overlong / wrong framing */
        *out_cp = cp;
        return 4;
    }
    if ((u0 & 0xFCU) == 0xF8U) {
        if (len < 5) return -1;
        if ((s[1] & 0xC0U) != 0x80U || (s[2] & 0xC0U) != 0x80U ||
            (s[3] & 0xC0U) != 0x80U || (s[4] & 0xC0U) != 0x80U) return -1;
        uint32_t cp = ((uint32_t)(u0 & 0x03U) << 24) |
                      ((uint32_t)(s[1] & 0x3FU) << 18) |
                      ((uint32_t)(s[2] & 0x3FU) << 12) |
                      ((uint32_t)(s[3] & 0x3FU) << 6) |
                      (uint32_t)(s[4] & 0x3FU);
        if (cp < 0x00110000U || cp > 0x03FFFFFFU) return -1;
        *out_cp = cp;
        return 5;
    }
    if ((u0 & 0xFEU) == 0xFCU) {
        if (len < 6) return -1;
        if ((s[1] & 0xC0U) != 0x80U || (s[2] & 0xC0U) != 0x80U ||
            (s[3] & 0xC0U) != 0x80U || (s[4] & 0xC0U) != 0x80U ||
            (s[5] & 0xC0U) != 0x80U) return -1;
        uint32_t cp = ((uint32_t)(u0 & 0x01U) << 30) |
                      ((uint32_t)(s[1] & 0x3FU) << 24) |
                      ((uint32_t)(s[2] & 0x3FU) << 18) |
                      ((uint32_t)(s[3] & 0x3FU) << 12) |
                      ((uint32_t)(s[4] & 0x3FU) << 6) |
                      (uint32_t)(s[5] & 0x3FU);
        if (cp < 0x04000000U) return -1; /* overlong */
        *out_cp = cp;
        return 6;
    }
    return -1; /* 0xFE/0xFF lead bytes and continuations are invalid */
}

bool ow_sutf8_validate(const uint8_t *s, size_t len) {
    if (!s) {
        return false;
    }
    size_t pos = 0;
    uint32_t cp = 0;
    while (pos < len) {
        int n = sutf8_decode_one(s + pos, len - pos, &cp);
        if (n <= 0) {
            return false;
        }
        pos += (size_t)n;
    }
    return true;
}

int ow_sutf8_name_cmp(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
    size_t pa = 0;
    size_t pb = 0;
    while (pa < a_len && pb < b_len) {
        uint32_t ca = 0;
        uint32_t cb = 0;
        int na = sutf8_decode_one(a + pa, a_len - pa, &ca);
        int nb = sutf8_decode_one(b + pb, b_len - pb, &cb);
        if (na <= 0 || nb <= 0) {
            return (na <= 0) ? -1 : 1;
        }
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        pa += (size_t)na;
        pb += (size_t)nb;
    }
    if (pa < a_len) {
        return 1;
    }
    if (pb < b_len) {
        return -1;
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
