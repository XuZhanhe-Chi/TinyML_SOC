#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  uint8_t v = (uint8_t)c;
  for (size_t i = 0; i < n; ++i) {
    d[i] = v;
  }
  return dst;
}

int __clzsi2(unsigned int x) {
  int count = 0;
  for (int i = 31; i >= 0; --i) {
    if (x & (1u << i)) {
      break;
    }
    count++;
  }
  return count;
}
