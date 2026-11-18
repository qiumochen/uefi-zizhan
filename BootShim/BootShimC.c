/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * M3 A' BootShim helper: point the ABL FDT's /chosen initrd at the PONGFAT
 * that the composer placed after the UEFI FD/DTB in the kernel field. This is
 * the iOS path only; the magic/Android path jumps before this is called and
 * keeps the stock initrd. No allocation, no libc, no cache-visible globals.
 */
#include <stdint.h>

#define FDT_MAGIC       0xD00DFEEDu
#define FDT_BEGIN_NODE  1u
#define FDT_END_NODE    2u
#define FDT_PROP        3u
#define FDT_NOP         4u
#define FDT_END         9u

static uint32_t be32 (const void *p) {
  const uint8_t *b = (const uint8_t *)p;
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static void put_be32 (void *p, uint32_t v) {
  uint8_t *b = (uint8_t *)p;
  b[0] = (uint8_t)(v >> 24);
  b[1] = (uint8_t)(v >> 16);
  b[2] = (uint8_t)(v >> 8);
  b[3] = (uint8_t)v;
}

static void put_be64 (void *p, uint64_t v) {
  uint8_t *b = (uint8_t *)p;
  for (unsigned i = 0; i < 8; ++i) {
    b[i] = (uint8_t)(v >> (56 - 8 * i));
  }
}

static int name_is (const char *s, const char *lit, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    if (s[i] != lit[i]) {
      return 0;
    }
  }
  return s[n] == 0;
}

static void clean_range (const void *p, unsigned n) {
#ifdef PONG_FDT_HOST_TEST
  (void)p;
  (void)n;
#else
  uintptr_t a = (uintptr_t)p & ~(uintptr_t)63;
  uintptr_t end = (uintptr_t)p + n;
  while (a < end) {
    __asm__ volatile ("dc civac, %0" : : "r" ((const void *)a) : "memory");
    a += 64;
  }
#endif
}

void pong_fdt_set_initrd (void *fdt, uint64_t start, uint64_t end) {
  const uint8_t *base = (const uint8_t *)fdt;
  const uint8_t *p;
  const uint8_t *limit;
  const char    *strings;
  uint32_t      off_struct, off_strings, size_struct;
  unsigned      depth = 0;
  unsigned      chosen_depth = 0;
  int           in_chosen = 0;

  if (fdt == 0 || be32 (base) != FDT_MAGIC) {
    return;
  }
  off_struct  = be32 (base + 8);
  off_strings = be32 (base + 12);
  size_struct = be32 (base + 36);
  p           = base + off_struct;
  limit       = p + size_struct;
  strings     = (const char *)(base + off_strings);

  while (p + 4 <= limit) {
    uint32_t token = be32 (p);
    p += 4;

    if (token == FDT_BEGIN_NODE) {
      const char *name = (const char *)p;
      unsigned    len  = 0;
      unsigned    padded;
      while (p + len < limit && name[len] != 0) {
        ++len;
      }
      padded = (len + 4u) & ~3u;
      if ((depth == 1) && (len == 6) && name_is (name, "chosen", 6)) {
        in_chosen    = 1;
        chosen_depth = depth;
      }
      ++depth;
      p += padded;
      continue;
    }

    if (token == FDT_END_NODE) {
      if (in_chosen && (depth == chosen_depth + 1)) {
        in_chosen = 0;
      }
      if (depth != 0) {
        --depth;
      }
      continue;
    }

    if (token == FDT_PROP) {
      uint32_t len;
      uint32_t nameoff;
      const char *pname;
      void       *value;
      if (p + 8 > limit) {
        return;
      }
      len     = be32 (p);
      nameoff = be32 (p + 4);
      p += 8;
      pname = strings + nameoff;
      value = (void *)p;
      if (in_chosen) {
        if (name_is (pname, "linux,initrd-start", 18)) {
          if (len == 8) {
            put_be64 (value, start);
            clean_range (value, len);
          } else if (len == 4) {
            put_be32 (value, (uint32_t)start);
            clean_range (value, len);
          }
        } else if (name_is (pname, "linux,initrd-end", 16)) {
          if (len == 8) {
            put_be64 (value, end);
            clean_range (value, len);
          } else if (len == 4) {
            put_be32 (value, (uint32_t)end);
            clean_range (value, len);
          }
        }
      }
      p += (len + 3u) & ~3u;
      continue;
    }

    if ((token == FDT_NOP) || (token == FDT_END)) {
      continue;
    }
    return;
  }
#ifndef PONG_FDT_HOST_TEST
  __asm__ volatile ("dsb sy" : : : "memory");
#endif
}
