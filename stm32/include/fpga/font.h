
#pragma once

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* characters are accessed by using map code
   (0x100 | (char_code & 0x3 << 5) | (char_code & 0x7c >> 2)) + char_base - 0x40
   and suitably setup alternate palettes #4-7.
   #4 must use bit 0 of the color number,
   #5 must use bit 1 of the color number,
   #6 must use bit 2 of the color number,
   #7 must use bit 3 of the color number.
 */

extern uint16_t font_tile_base;
uint32_t _font_find_tile(wchar_t wc);
static inline uint32_t font_get_tile(wchar_t wc, uint8_t pal_sel, uint8_t pal_idx) {
  if (wc < 0x20 || wc >= 0x80) {
    wc = _font_find_tile(wc);
  }
  uint16_t tile = wc;
  tile += font_tile_base * 4;
  return (pal_idx << 21) | (pal_sel << 17) | 0x10000 |
    tile;
}

uint16_t font_upload();

#ifdef __cplusplus
}
#endif
