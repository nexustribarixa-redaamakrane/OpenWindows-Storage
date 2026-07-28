#ifndef OW_MEM_H
#define OW_MEM_H

#include <stddef.h>
#include <stdint.h>

void *ow_memcpy(void *dest, const void *src, size_t n);
void *ow_memset(void *dest, int val, size_t n);
int ow_memcmp(const void *a, const void *b, size_t n);

#endif /* OW_MEM_H */
