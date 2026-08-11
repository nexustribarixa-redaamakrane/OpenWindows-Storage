#include <stddef.h>
#include <stdint.h>
#include "../include/ow_crypto.h"
#include "../include/ow_mem.h"

#define CHACHA_CONSTANT_0 0x61707865U
#define CHACHA_CONSTANT_1 0x3320646EU
#define CHACHA_CONSTANT_2 0x79622D32U
#define CHACHA_CONSTANT_3 0x6B206574U

static uint32_t rotl32(uint32_t x, unsigned int n) {
    return (x << n) | (x >> (32 - n));
}

static uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void chacha_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d = rotl32(*d ^ *a, 16);
    *c += *d; *b = rotl32(*b ^ *c, 12);
    *a += *b; *d = rotl32(*d ^ *a, 8);
    *c += *d; *b = rotl32(*b ^ *c, 7);
}

static void chacha_init_state(uint32_t state[16],
                              uint32_t counter,
                              const uint8_t key[OW_CHACHA20_KEY_SIZE],
                              const uint8_t nonce[OW_CHACHA20_NONCE_SIZE]) {
    state[0] = CHACHA_CONSTANT_0;
    state[1] = CHACHA_CONSTANT_1;
    state[2] = CHACHA_CONSTANT_2;
    state[3] = CHACHA_CONSTANT_3;
    for (unsigned int i = 0; i < 8; ++i) {
        state[4 + i] = load32_le(key + (4 * i));
    }
    state[12] = counter;
    state[13] = load32_le(nonce + 0);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);
}

void ow_chacha20_block(uint32_t counter,
                       const uint8_t key[OW_CHACHA20_KEY_SIZE],
                       const uint8_t nonce[OW_CHACHA20_NONCE_SIZE],
                       uint8_t out[OW_CHACHA20_BLOCK_SIZE]) {
    uint32_t state[16];
    uint32_t work[16];
    chacha_init_state(state, counter, key, nonce);
    ow_memcpy(work, state, sizeof(work));

    for (unsigned int round = 0; round < 10; ++round) {
        chacha_qr(&work[0], &work[4], &work[8],  &work[12]);
        chacha_qr(&work[1], &work[5], &work[9],  &work[13]);
        chacha_qr(&work[2], &work[6], &work[10], &work[14]);
        chacha_qr(&work[3], &work[7], &work[11], &work[15]);
        chacha_qr(&work[0], &work[5], &work[10], &work[15]);
        chacha_qr(&work[1], &work[6], &work[11], &work[12]);
        chacha_qr(&work[2], &work[7], &work[8],  &work[13]);
        chacha_qr(&work[3], &work[4], &work[9],  &work[14]);
    }

    for (unsigned int i = 0; i < 16; ++i) {
        store32_le(out + (4 * i), work[i] + state[i]);
    }
}

void ow_chacha20_xor(uint32_t counter,
                     const uint8_t key[OW_CHACHA20_KEY_SIZE],
                     const uint8_t nonce[OW_CHACHA20_NONCE_SIZE],
                     uint8_t *data, size_t len) {
    uint8_t stream[OW_CHACHA20_BLOCK_SIZE];
    size_t pos = 0;
    while (pos < len) {
        ow_chacha20_block(counter++, key, nonce, stream);
        size_t chunk = len - pos;
        if (chunk > OW_CHACHA20_BLOCK_SIZE) {
            chunk = OW_CHACHA20_BLOCK_SIZE;
        }
        for (size_t i = 0; i < chunk; ++i) {
            data[pos + i] ^= stream[i];
        }
        pos += chunk;
    }
}
