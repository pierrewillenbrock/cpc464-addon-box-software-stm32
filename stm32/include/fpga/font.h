
#pragma once

#include <stdint.h>

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
static inline uint16_t font_get_tile(char c, uint8_t pal_base) {
  if (c < 0x20 || c >= 0x80) {
    c = 0x7f;
  }
  return (((c & 0x3) << 6) | ((c & 0x7c) >> 2)) + (pal_base << 6) + font_tile_base;
}
//output buffer is 0x60/4*4*8=0x300 elements
void font_compile(uint16_t *buffer);
uint16_t font_upload(uint16_t position_after);

#ifdef __cplusplus
}
#endif
