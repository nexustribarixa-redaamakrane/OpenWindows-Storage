#ifndef OW_CRYPTO_H
#define OW_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define OW_CHACHA20_KEY_SIZE    32
#define OW_CHACHA20_NONCE_SIZE  12
#define OW_CHACHA20_BLOCK_SIZE  64

/* XOR `len` bytes of `data` with the ChaCha20 keystream derived from
 * key[32], a 32-bit counter and nonce[12]. In-place safe. */
void ow_chacha20_xor(uint32_t counter,
                     const uint8_t key[OW_CHACHA20_KEY_SIZE],
                     const uint8_t nonce[OW_CHACHA20_NONCE_SIZE],
                     uint8_t *data, size_t len);

/* Generate one 64-byte ChaCha20 keystream block for the given counter. */
void ow_chacha20_block(uint32_t counter,
                       const uint8_t key[OW_CHACHA20_KEY_SIZE],
                       const uint8_t nonce[OW_CHACHA20_NONCE_SIZE],
                       uint8_t out[OW_CHACHA20_BLOCK_SIZE]);

#endif /* OW_CRYPTO_H */
