#ifndef OW_STRING_H
#define OW_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ow_sutf8_validate(const uint8_t *s, size_t len);
int ow_sutf8_name_cmp(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len);
size_t ow_sutf8_name_copy(uint8_t *dest, size_t dest_cap, const uint8_t *src, size_t src_len);

#endif /* OW_STRING_H */
