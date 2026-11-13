/* runtime.c - freestanding memory helpers used by the compiler's codegen. */
#include "uefi.h"

void *memcpy(void *dst, const void *src, UINTN len)
{
  UINT8 *d = (UINT8 *)dst;
  const UINT8 *s = (const UINT8 *)src;
  while (len >= 8) {
    *(UINT64 *)d = *(const UINT64 *)s;
    d += 8;
    s += 8;
    len -= 8;
  }
  while (len--) {
    *d++ = *s++;
  }
  return dst;
}

void *memmove(void *dst, const void *src, UINTN len)
{
  UINT8 *d = (UINT8 *)dst;
  const UINT8 *s = (const UINT8 *)src;
  if (d == s || len == 0) {
    return dst;
  }
  if (d < s) {
    return memcpy(dst, src, len);
  }
  d += len;
  s += len;
  while (len--) {
    *--d = *--s;
  }
  return dst;
}

void *memset(void *dst, int value, UINTN len)
{
  UINT8 *d = (UINT8 *)dst;
  UINT8 v = (UINT8)value;
  UINT64 pattern = (UINT64)v * 0x0101010101010101ULL;
  while (len >= 8) {
    *(UINT64 *)d = pattern;
    d += 8;
    len -= 8;
  }
  while (len--) {
    *d++ = v;
  }
  return dst;
}

int memcmp(const void *a, const void *b, UINTN len)
{
  const UINT8 *pa = (const UINT8 *)a;
  const UINT8 *pb = (const UINT8 *)b;
  while (len--) {
    if (*pa != *pb) {
      return (int)*pa - (int)*pb;
    }
    pa++;
    pb++;
  }
  return 0;
}
